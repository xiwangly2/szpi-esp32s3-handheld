<?php
$configFile = __DIR__ . '/config.php';
$imageApiConfig = is_file($configFile) ? (array)require $configFile : [];
$apiKey = (string)($imageApiConfig['api_key'] ?? getenv('IMAGE_API_KEY') ?: 'change-me');

const DEFAULT_TYPE = 'images';
const ESP_WIDTH = 320;
const ESP_HEIGHT = 240;
const ESP_QUALITY = 72;

disableOutputCompression();

$requestKey = isset($_REQUEST['key']) ? (string)$_REQUEST['key'] : '';
if (function_exists('hash_equals') ? !hash_equals($apiKey, $requestKey) : $apiKey !== $requestKey) {
    http_response_code(403);
    exit;
}

$types = [
    'images' => ['dir' => __DIR__ . '/images/', 'url' => '/images/'],
    'bz' => ['dir' => __DIR__ . '/bz/', 'url' => '/bz/'],
    'background' => ['dir' => __DIR__ . '/background/', 'url' => '/background/'],
];

$type = isset($_REQUEST['type']) ? (string)$_REQUEST['type'] : DEFAULT_TYPE;
if (!isset($types[$type])) {
    $type = DEFAULT_TYPE;
}

$dir = $types[$type]['dir'];
$jsonDir = $types[$type]['url'];
$return = isset($_REQUEST['return']) ? (string)$_REQUEST['return'] : '';
$espMode = isset($_REQUEST['esp']) && $_REQUEST['esp'] !== '0';
$resizeRequested = $espMode || isset($_REQUEST['w']) || isset($_REQUEST['h']) || isset($_REQUEST['q']) || isset($_REQUEST['fit']);

$fileName = pickImageFile($dir, isset($_REQUEST['file']) ? (string)$_REQUEST['file'] : '');
if ($fileName === null) {
    http_response_code(404);
    exit;
}

$file = $dir . $fileName;
$info = @getimagesize($file);
if ($info === false || empty($info['mime'])) {
    http_response_code(415);
    exit;
}

$sourceWidth = (int)$info[0];
$sourceHeight = (int)$info[1];
$sourceMime = (string)$info['mime'];
$width = clampInt(isset($_REQUEST['w']) ? $_REQUEST['w'] : ($espMode ? ESP_WIDTH : $sourceWidth), 1, 1920);
$height = clampInt(isset($_REQUEST['h']) ? $_REQUEST['h'] : ($espMode ? ESP_HEIGHT : $sourceHeight), 1, 1920);
$quality = clampInt(isset($_REQUEST['q']) ? $_REQUEST['q'] : ($espMode ? ESP_QUALITY : 82), 1, 95);
$fit = isset($_REQUEST['fit']) ? (string)$_REQUEST['fit'] : 'crop';
if (!in_array($fit, ['crop', 'contain', 'stretch'], true)) {
    $fit = 'crop';
}

if ($return === 'print') {
    if ($resizeRequested) {
        outputResizedJpeg($file, $sourceMime, $sourceWidth, $sourceHeight, $width, $height, $quality, $fit);
    }
    outputOriginal($file, $sourceMime);
}

$baseUrl = baseUrl();
$originalUrl = $baseUrl . encodePath($jsonDir . $fileName);

if ($return === 'json') {
    $imgUrl = $originalUrl;
    $mime = $sourceMime;
    $outWidth = $sourceWidth;
    $outHeight = $sourceHeight;

    if ($resizeRequested) {
        $imgUrl = scriptUrl() . '?' . http_build_query([
            'key' => $apiKey,
            'type' => $type,
            'return' => 'print',
            'file' => $fileName,
            'w' => $width,
            'h' => $height,
            'q' => $quality,
            'fit' => $fit,
        ], '', '&', PHP_QUERY_RFC3986);
        $mime = 'image/jpeg';
        $outWidth = $width;
        $outHeight = $height;
    }

    header('Content-Type: application/json; charset=utf-8');
    echo json_encode([
        'code' => '200',
        'imgurl' => $imgUrl,
        'mime' => $mime,
        'width' => $outWidth,
        'height' => $outHeight,
        'source_mime' => $sourceMime,
        'source_width' => $sourceWidth,
        'source_height' => $sourceHeight,
        'source_bytes' => @filesize($file) ?: 0,
    ], JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
    exit;
}

header('Location: ' . $originalUrl, true, 302);
exit;

function pickImageFile($dir, $requested)
{
    if (!is_dir($dir)) {
        return null;
    }

    $files = [];
    foreach (scandir($dir) ?: [] as $entry) {
        if ($entry === '.' || $entry === '..') {
            continue;
        }
        $path = $dir . $entry;
        if (!is_file($path)) {
            continue;
        }
        if (@getimagesize($path) === false) {
            continue;
        }
        $files[] = $entry;
    }

    if ($files === []) {
        return null;
    }

    $requested = basename($requested);
    if ($requested !== '' && in_array($requested, $files, true)) {
        return $requested;
    }

    return $files[mt_rand(0, count($files) - 1)];
}

function outputOriginal($file, $mime)
{
    header('Content-Type: ' . $mime);
    header('Content-Length: ' . (string)filesize($file));
    header('Cache-Control: public, max-age=86400');
    if ($_SERVER['REQUEST_METHOD'] !== 'HEAD') {
        readfile($file);
    }
    exit;
}

function outputResizedJpeg($file, $mime, $sourceWidth, $sourceHeight, $width, $height, $quality, $fit)
{
    if (!extension_loaded('gd')) {
        http_response_code(500);
        header('Content-Type: text/plain; charset=utf-8');
        echo 'PHP GD extension is required for resize mode.';
        exit;
    }

    $source = createImage($file, $mime);
    if (!$source) {
        http_response_code(415);
        exit;
    }

    $dest = imagecreatetruecolor($width, $height);
    imagefill($dest, 0, 0, imagecolorallocate($dest, 0, 0, 0));

    if ($fit === 'stretch') {
        imagecopyresampled($dest, $source, 0, 0, 0, 0, $width, $height, $sourceWidth, $sourceHeight);
    } else {
        $scale = $fit === 'contain'
            ? min($width / $sourceWidth, $height / $sourceHeight)
            : max($width / $sourceWidth, $height / $sourceHeight);
        $drawWidth = max(1, (int)round($sourceWidth * $scale));
        $drawHeight = max(1, (int)round($sourceHeight * $scale));
        $dstX = (int)floor(($width - $drawWidth) / 2);
        $dstY = (int)floor(($height - $drawHeight) / 2);
        imagecopyresampled($dest, $source, $dstX, $dstY, 0, 0, $drawWidth, $drawHeight, $sourceWidth, $sourceHeight);
    }

    imageinterlace($dest, false);
    ob_start();
    imagejpeg($dest, null, $quality);
    $payload = (string)ob_get_clean();

    imagedestroy($source);
    imagedestroy($dest);

    header('Content-Type: image/jpeg');
    header('Cache-Control: public, max-age=86400');
    header('Connection: close');
    if ($_SERVER['REQUEST_METHOD'] !== 'HEAD') {
        echo $payload;
    }
    exit;
}

function createImage($file, $mime)
{
    switch ($mime) {
        case 'image/jpeg':
            return @imagecreatefromjpeg($file);
        case 'image/png':
            return @imagecreatefrompng($file);
        case 'image/gif':
            return @imagecreatefromgif($file);
        case 'image/webp':
            return function_exists('imagecreatefromwebp') ? @imagecreatefromwebp($file) : false;
        default:
            return false;
    }
}

function clampInt($value, $min, $max)
{
    $value = filter_var($value, FILTER_VALIDATE_INT);
    if ($value === false) {
        $value = $min;
    }
    return max($min, min($max, (int)$value));
}

function baseUrl()
{
    $proto = isset($_SERVER['HTTP_X_FORWARDED_PROTO']) ? $_SERVER['HTTP_X_FORWARDED_PROTO'] : null;
    $scheme = $proto ?: ((!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off') ? 'https' : 'http');
    $host = isset($_SERVER['HTTP_HOST']) ? $_SERVER['HTTP_HOST'] : (isset($_SERVER['SERVER_NAME']) ? $_SERVER['SERVER_NAME'] : 'localhost');
    return $scheme . '://' . $host;
}

function scriptUrl()
{
    $script = isset($_SERVER['SCRIPT_NAME']) ? $_SERVER['SCRIPT_NAME'] : '/image.php';
    return baseUrl() . $script;
}

function encodePath($path)
{
    $parts = array_map('rawurlencode', explode('/', $path));
    return implode('/', $parts);
}

function disableOutputCompression()
{
    @ini_set('zlib.output_compression', '0');
    if (function_exists('apache_setenv')) {
        @apache_setenv('no-gzip', '1');
    }
}
