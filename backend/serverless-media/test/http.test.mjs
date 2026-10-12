import test from 'node:test';
import assert from 'node:assert/strict';
import { Readable } from 'node:stream';
import { httpAdapter } from '../src/http.mjs';

test('HTTP container adapter preserves method, headers, body and response status', async () => {
  const incoming = Readable.from([Buffer.from('{"size":12}')]);
  Object.assign(incoming, { method: 'POST', url: '/uploads/clip.avi', headers: { authorization: 'Bearer example' } });
  const outgoing = {
    writeHead(status, headers) { this.status = status; this.headers = headers; },
    end(body) { this.body = body; },
  };
  await httpAdapter(async request => {
    assert.equal(request.method, 'POST');
    assert.equal(request.headers.get('authorization'), 'Bearer example');
    assert.deepEqual(await request.json(), { size: 12 });
    return Response.json({ method: 'PUT' }, { status: 201 });
  })(incoming, outgoing);
  assert.equal(outgoing.status, 201);
  assert.deepEqual(JSON.parse(outgoing.body), { method: 'PUT' });
});
test('HTTP adapter bounds upload manifests before invoking the service', async () => {
  const incoming = Readable.from([Buffer.alloc(3000)]);
  Object.assign(incoming, { method: 'POST', url: '/uploads/clip.avi', headers: {} });
  const outgoing = { writeHead(status) { this.status = status; }, end() {} };
  await httpAdapter(() => { throw new Error('must not be invoked'); })(incoming, outgoing);
  assert.equal(outgoing.status, 413);
});
