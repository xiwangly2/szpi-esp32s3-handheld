export const MAX_BYTES = 256 * 1024 * 1024;
const MIME = 'video/x-msvideo';
const NAME = /^[A-Za-z0-9][A-Za-z0-9._-]{0,95}\.avi$/i;
const json = (data, status = 200) => Response.json(data, { status, headers: { 'Cache-Control': 'no-store' } });

async function equalToken(input, expected) {
  if (typeof input !== 'string' || input.length > 512 || typeof expected !== 'string' || expected.length < 24) return false;
  const encoder = new TextEncoder();
  const [a, b] = await Promise.all([input, expected].map(value => crypto.subtle.digest('SHA-256', encoder.encode(value))));
  const left = new Uint8Array(a), right = new Uint8Array(b);
  let mismatch = 0;
  for (let i = 0; i < left.length; i++) mismatch |= left[i] ^ right[i];
  return mismatch === 0;
}

async function readSmallJson(request) {
  if (Number(request.headers.get('content-length')) > 2048) throw new Error('large');
  const reader = request.body?.getReader();
  if (!reader) throw new Error('empty');
  const chunks = [];
  let size = 0;
  try {
    for (;;) {
      const { value, done } = await reader.read();
      if (done) break;
      size += value.byteLength;
      if (size > 2048) { await reader.cancel(); throw new Error('large'); }
      chunks.push(value);
    }
  } finally { reader.releaseLock(); }
  const data = new Uint8Array(size);
  let offset = 0;
  for (const chunk of chunks) { data.set(chunk, offset); offset += chunk.length; }
  return JSON.parse(new TextDecoder().decode(data));
}

export function createMediaService({ store, readToken, adminToken }) {
  if (!store || readToken?.length < 24 || adminToken?.length < 24 || !readToken || !adminToken || readToken === adminToken) {
    throw new Error('Storage and separate read/admin tokens are required');
  }
  return async request => {
    const url = new URL(request.url);
    if (url.pathname === '/health' && request.method === 'GET') return json({ ok: true, service: 'szpi-media', version: 1 });
    const authorization = request.headers.get('authorization') || '';
    const bearer = authorization.startsWith('Bearer ') ? authorization.slice(7) : '';
    const upload = url.pathname.startsWith('/uploads/');
    const token = bearer || (upload ? '' : url.searchParams.get('key') || '');
    const admin = await equalToken(token, adminToken);
    if (!(admin || (!upload && await equalToken(token, readToken)))) return json({ error: 'Unauthorized' }, 401);
    const match = /^\/(media|uploads)\/([^/]+)$/.exec(url.pathname);
    if (!match) return json({ error: 'Not found' }, 404);
    let name;
    try { name = decodeURIComponent(match[2]); } catch { return json({ error: 'Invalid filename' }, 400); }
    if (!NAME.test(name)) return json({ error: 'Use a simple ASCII .avi filename' }, 400);
    const key = `media/${name}`;
    try {
      if (match[1] === 'media' && ['GET', 'HEAD'].includes(request.method)) {
        const object = await store.head(key);
        if (!object) return json({ error: 'Video not found' }, 404);
        if (!Number.isSafeInteger(object.size) || object.size < 12 || object.size > MAX_BYTES) return json({ error: 'Unsupported size' }, 413);
        if (request.method === 'HEAD') return new Response(null, { headers: {
          'Content-Length': String(object.size), 'Content-Type': MIME, 'Cache-Control': 'private, no-store',
        } });
        const location = await store.download(key, 3600);
        return new Response(null, { status: 307, headers: { Location: location, 'Cache-Control': 'private, no-store' } });
      }
      if (match[1] === 'uploads' && request.method === 'POST') {
        if (!admin) return json({ error: 'Upload requires the admin token' }, 403);
        let data;
        try { data = await readSmallJson(request); } catch { return json({ error: 'Expected a small JSON upload manifest' }, 400); }
        if (!data || !Number.isSafeInteger(data.size) || data.size < 12 || data.size > MAX_BYTES ||
            !/^[a-f0-9]{64}$/i.test(data.sha256 || '') || !/^[A-Za-z0-9+/]{22}==$/.test(data.md5 || '')) {
          return json({ error: 'Require size (12..256 MiB), SHA256 hex and MD5 base64' }, 400);
        }
        if (await store.head(key)) return json({ error: 'File exists; use a versioned name' }, 409);
        const ticket = await store.upload(key, { size: data.size, sha256: data.sha256, md5: data.md5, contentType: MIME }, 900);
        return json({ file: name, method: 'PUT', expiresIn: 900, ...ticket }, 201);
      }
      return json({ error: 'Method not allowed' }, 405);
    } catch (error) {
      console.error('Media storage operation failed', error?.name || 'Error');
      return json({ error: 'Storage unavailable' }, 502);
    }
  };
}
