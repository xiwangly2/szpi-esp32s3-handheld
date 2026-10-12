<?php
declare(strict_types=1);

function fail(int $status, string $message): void {
    http_response_code($status);
    header('Content-Type: application/json; charset=utf-8');
    echo json_encode(['error' => $message], JSON_UNESCAPED_SLASHES);
    exit;
}

$config = is_file(__DIR__ . '/config.php') ? require __DIR__ . '/config.php' : [];
$key = (string)(getenv('VIDEO_API_KEY') ?: ($config['api_key'] ?? ''));
if ($key === '' || $key === 'change-me') {
    fail(503, 'Configure VIDEO_API_KEY or config.php first.');
}
$authorization = $_SERVER['HTTP_AUTHORIZATION'] ?? '';
$provided = str_starts_with($authorization, 'Bearer ') ? substr($authorization, 7) : ($_GET['key'] ?? '');
if (!is_string($provided) || !hash_equals($key, $provided)) {
    fail(401, 'Unauthorized');
}
$root = (string)(getenv('VIDEO_MEDIA_DIR') ?: ($config['media_dir'] ?? ''));
if ($root === '') { fail(503, 'Configure a private VIDEO_MEDIA_DIR or media_dir first.'); }
$maximum = min(256 * 1024 * 1024, max(12, (int)($config['max_bytes'] ?? 256 * 1024 * 1024)));
$method = $_SERVER['REQUEST_METHOD'] ?? 'GET';
if (!in_array($method, ['GET', 'HEAD', 'POST'], true)) {
    header('Allow: GET, HEAD, POST'); fail(405, 'Unsupported method');
}
if (!is_dir($root) && !mkdir($root, 0750, true) && !is_dir($root)) {
    fail(500, 'Media directory unavailable');
}
$root = realpath($root);
if ($root === false) { fail(500, 'Media directory unavailable'); }

function valid_name(mixed $name): bool {
    return is_string($name) && strlen($name) <= 100 &&
        preg_match('/\A[A-Za-z0-9][A-Za-z0-9._-]*\.avi\z/i', $name) === 1;
}

if ($method === 'POST') {
    $upload = $_FILES['file'] ?? null;
    if (!is_array($upload) || ($upload['error'] ?? UPLOAD_ERR_NO_FILE) !== UPLOAD_ERR_OK ||
        !is_string($upload['tmp_name'] ?? null) || !is_uploaded_file($upload['tmp_name'])) {
        fail(400, 'Upload a converted AVI in the file field; check PHP upload limits.');
    }
    $name = $upload['name'] ?? '';
    if (!valid_name($name)) { fail(400, 'Use a simple ASCII .avi filename.'); }
    $size = filesize($upload['tmp_name']);
    if ($size === false || $size < 12 || $size > $maximum) { fail(413, 'Video exceeds size limit'); }
    $header = file_get_contents($upload['tmp_name'], false, null, 0, 12);
    if ($header === false || substr($header, 0, 4) !== 'RIFF' || substr($header, 8, 4) !== 'AVI ') {
        fail(415, 'Expected an AVI file');
    }
    $path = $root . DIRECTORY_SEPARATOR . $name;
    // Serialize uploads; existing media is never overwritten by a repeated POST.
    $lock = fopen($root . DIRECTORY_SEPARATOR . '.upload.lock', 'c');
    if ($lock === false || !flock($lock, LOCK_EX)) { fail(500, 'Upload lock unavailable'); }
    if (file_exists($path) || is_link($path)) { fail(409, 'File exists; use a new filename.'); }
    $partial = $root . DIRECTORY_SEPARATOR . '.upload-' . bin2hex(random_bytes(12)) . '.part';
    if (!move_uploaded_file($upload['tmp_name'], $partial)) { fail(500, 'Unable to store upload'); }
    if (!rename($partial, $path)) { unlink($partial); fail(500, 'Unable to finish upload'); }
    flock($lock, LOCK_UN); fclose($lock);
    http_response_code(201);
    header('Content-Type: application/json; charset=utf-8');
    echo json_encode(['file' => $name, 'bytes' => $size, 'sha256' => hash_file('sha256', $path)]);
    exit;
}

$name = $_GET['file'] ?? 'BadApple.avi';
if (!valid_name($name)) { fail(400, 'Invalid filename'); }
$path = realpath($root . DIRECTORY_SEPARATOR . $name);
if ($path === false || dirname($path) !== $root || !is_file($path)) { fail(404, 'Video not found'); }
$size = filesize($path);
if ($size === false || $size < 12 || $size > $maximum) { fail(413, 'Video exceeds size limit'); }
$file = fopen($path, 'rb');
if ($file === false) { fail(500, 'Unable to read video'); }
$start = 0; $end = $size - 1;
$range = $_SERVER['HTTP_RANGE'] ?? '';
if ($range !== '') {
    if (!preg_match('/\Abytes=(\d*)-(\d*)\z/', $range, $parts) || ($parts[1] === '' && $parts[2] === '')) {
        header("Content-Range: bytes */$size"); fail(416, 'Invalid range');
    }
    if ($parts[1] === '') {
        $start = max(0, $size - (int)$parts[2]);
    } else {
        $start = (int)$parts[1];
        if ($parts[2] !== '') { $end = min($end, (int)$parts[2]); }
    }
    if ($start > $end || $start >= $size) { header("Content-Range: bytes */$size"); fail(416, 'Range outside video'); }
    http_response_code(206);
    header("Content-Range: bytes $start-$end/$size");
}
ini_set('zlib.output_compression', '0');
header('Content-Type: video/x-msvideo');
header('X-Content-Type-Options: nosniff');
header('Cache-Control: private, no-transform');
header('Accept-Ranges: bytes');
header('Content-Length: ' . ($end - $start + 1));
if ($method === 'HEAD') { fclose($file); exit; }
set_time_limit(0);
fseek($file, $start);
$remaining = $end - $start + 1;
while ($remaining > 0 && !connection_aborted()) {
    $chunk = fread($file, min(65536, $remaining));
    if ($chunk === false || $chunk === '') { break; }
    echo $chunk;
    $remaining -= strlen($chunk);
}
fclose($file);
