// Offline UI fixture, not a device configurator. No credentials or hardware access.
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
const assets = new URL('../firmware/handheld/main/web/', import.meta.url);
let saved = false, configured = false, profile = { url: '', tokenSet: false };
const session = 'fixture-session-0123456789abcdef';
const server = createServer(async (req, res) => {
  const send = (code, object) => { res.writeHead(code, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' }); res.end(JSON.stringify(object)); };
  const files = { '/': ['info_setup.html', 'text/html'], '/setup.css': ['info_setup.css', 'text/css'], '/setup.js': ['info_setup.js', 'text/javascript'] };
  if (files[req.url] && req.method === 'GET') {
    const [file, type] = files[req.url];
    res.writeHead(200, { 'Content-Type': `${type}; charset=utf-8`, 'Cache-Control': 'no-store',
      'Content-Security-Policy': "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'" });
    res.end(await readFile(new URL(file, assets))); return;
  }
  let raw = ''; for await (const chunk of req) { raw += chunk; if (raw.length > 1024) return send(413, {}); }
  let body; try { body = raw ? JSON.parse(raw) : null; } catch { return send(400, {}); }
  if (req.url === '/api/pair' && req.method === 'POST') {
    if (body?.pin !== '123456') return send(403, {});
    saved = false; return send(200, { session });
  }
  if (req.headers['x-setup-token'] !== session) return send(403, {});
  if (req.url === '/api/config' && req.method === 'GET') return send(200, { ...profile, state: saved ? 'saved' : 'ready' });
  if (req.url === '/api/config' && req.method === 'POST') {
    profile = { url: body.url, tokenSet: body.action === 'replace' || (body.action === 'keep' && configured) };
    configured = profile.tokenSet; saved = true; return send(202, { state: 'saving' });
  }
  send(404, {});
});
server.listen(0, '127.0.0.1', () => console.log(`UI fixture: http://127.0.0.1:${server.address().port} (PIN 123456)`));
