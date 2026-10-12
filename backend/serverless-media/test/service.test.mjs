import test from 'node:test';
import assert from 'node:assert/strict';
import { createMediaService, MAX_BYTES } from '../src/service.mjs';

const readToken = 'read-'.repeat(8), adminToken = 'admin-'.repeat(8);
const objects = new Map([['media/clip.avi', { size: 4096 }]]);
const store = {
  head: async key => objects.get(key) || null,
  download: async key => `https://storage.example/${key}?signed=1`,
  upload: async key => ({ url: `https://storage.example/${key}`, headers: { 'Content-Type': 'video/x-msvideo' } }),
};
const run = createMediaService({ store, readToken, adminToken });
const req = (path, token = readToken, method = 'GET', body) => new Request(`https://api.example${path}`, {
  method, headers: token ? { authorization: `Bearer ${token}` } : {}, ...(body !== undefined ? { body: JSON.stringify(body) } : {}),
});
const manifest = { size: 4096, sha256: 'a'.repeat(64), md5: 'AAAAAAAAAAAAAAAAAAAAAA==' };

test('health is public; storage requires credentials', async () => {
  assert.equal((await run(req('/health', null))).status, 200);
  assert.equal((await run(req('/media/clip.avi', null))).status, 401);
  assert.equal((await run(req('/media/clip.avi', 'wrong'))).status, 401);
});
test('download redirects; device query key works; HEAD has size', async () => {
  const response = await run(req(`/media/clip.avi?key=${readToken}`, null));
  assert.equal(response.status, 307);
  assert.equal(response.headers.get('location'), 'https://storage.example/media/clip.avi?signed=1');
  assert.equal(await response.text(), '');
  const head = await run(req('/media/clip.avi', readToken, 'HEAD'));
  assert.equal(head.headers.get('content-length'), '4096');
});
test('missing, oversized, and invalid paths are rejected', async () => {
  assert.equal((await run(req('/media/missing.avi'))).status, 404);
  objects.set('media/large.avi', { size: MAX_BYTES + 1 });
  assert.equal((await run(req('/media/large.avi'))).status, 413);
  for (const path of ['/media/a%2fb.avi', '/media/foo.php', '/media/%00.avi', '/media/%ZZ.avi']) {
    assert.equal((await run(req(path))).status, 400);
  }
});
test('read-only keys cannot upload, and query-string admin upload keys are rejected', async () => {
  assert.equal((await run(req('/uploads/new.avi', readToken, 'POST', manifest))).status, 401);
  assert.equal((await run(req(`/uploads/new.avi?key=${adminToken}`, null, 'POST', manifest))).status, 401);
});
test('upload returns a direct storage ticket, refuses existing files', async () => {
  assert.equal((await run(req('/uploads/clip.avi', adminToken, 'POST', manifest))).status, 409);
  const response = await run(req('/uploads/new.avi', adminToken, 'POST', manifest));
  assert.equal(response.status, 201);
  assert.equal((await response.json()).method, 'PUT');
});
test('bad and oversized upload manifests never reach storage', async () => {
  for (const body of [null, {}, { ...manifest, size: 0 }, { ...manifest, size: MAX_BYTES + 1 },
    { ...manifest, sha256: 'invalid' }, { ...manifest, md5: '' }, { text: 'a'.repeat(3000) }]) {
    assert.equal((await run(req('/uploads/new.avi', adminToken, 'POST', body))).status, 400);
  }
});
test('tokens must be separate; storage exceptions do not expose details', async () => {
  assert.throws(() => createMediaService({ store, readToken, adminToken: readToken }));
  const fail = createMediaService({ store: { head: async () => { throw new Error('secret backend details'); } }, readToken, adminToken });
  const result = await fail(req('/media/clip.avi'));
  assert.equal(result.status, 502);
  assert.ok(!(await result.text()).includes('secret'));
});
