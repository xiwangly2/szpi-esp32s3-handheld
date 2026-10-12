import test from 'node:test';
import assert from 'node:assert/strict';
import { s3Storage } from '../src/s3.mjs';
import { cosStorage, ossStorage } from '../src/other-storage.mjs';
import worker from '../src/worker.mjs';
const info = { size: 4096, contentType: 'video/x-msvideo', md5: 'AAAAAAAAAAAAAAAAAAAAAA==', sha256: 'a'.repeat(64) };

test('S3 signs every uploaded x-amz header instead of moving it into query parameters', async () => {
  const store = s3Storage({ STORAGE_REGION: 'ap-east-1', VIDEO_BUCKET: 'example-bucket',
    STORAGE_ACCESS_KEY: 'AKIAEXAMPLE', STORAGE_SECRET_KEY: 'example-secret-only-for-tests' });
  const ticket = await store.upload('media/clip.avi', info, 900);
  const url = new URL(ticket.url);
  for (const header of ['content-length', 'content-md5', 'content-type', 'if-none-match', 'x-amz-meta-sha256']) {
    assert.ok(url.searchParams.get('X-Amz-SignedHeaders').split(';').includes(header), header);
  }
  assert.equal(url.searchParams.get('x-amz-meta-sha256'), null);
  assert.equal(ticket.headers['x-amz-meta-sha256'], info.sha256);
});
test('native COS and OSS adapters generate HTTPS upload URLs without contacting cloud APIs', async () => {
  const cos = cosStorage({ VIDEO_BUCKET: 'example-1250000000', STORAGE_REGION: 'ap-guangzhou',
    STORAGE_ACCESS_KEY: 'example-id', STORAGE_SECRET_KEY: 'example-secret' });
  const oss = ossStorage({ VIDEO_BUCKET: 'example-bucket', STORAGE_REGION: 'oss-cn-hangzhou',
    STORAGE_ACCESS_KEY: 'example-id', STORAGE_SECRET_KEY: 'example-secret' });
  for (const store of [cos, oss]) {
    const ticket = await store.upload('media/clip.avi', info, 900);
    assert.equal(new URL(ticket.url).protocol, 'https:');
    assert.equal(ticket.headers['Content-MD5'], info.md5);
  }
});
test('Worker R2 upload tickets use the same API and signed headers', async () => {
  const env = { R2_ACCOUNT_ID: 'exampleaccount', VIDEO_BUCKET: 'example-bucket',
    R2_ACCESS_KEY_ID: 'example-id', R2_SECRET_ACCESS_KEY: 'example-secret',
    READ_TOKEN: 'read-'.repeat(8), ADMIN_TOKEN: 'admin-'.repeat(8), MEDIA: { head: async () => null } };
  const result = await worker.fetch(new Request('https://api.example/uploads/clip.avi', {
    method: 'POST', headers: { authorization: `Bearer ${env.ADMIN_TOKEN}` },
    body: JSON.stringify(info),
  }), env);
  assert.equal(result.status, 201);
  const ticket = await result.json();
  const signed = new URL(ticket.url).searchParams.get('X-Amz-SignedHeaders');
  assert.ok(signed.includes('x-amz-meta-sha256'));
});
