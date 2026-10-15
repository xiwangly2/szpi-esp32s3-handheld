import test from 'node:test';
import assert from 'node:assert/strict';
import { createInformationProvider, parseFeed, parseInfoConfig } from '../src/information.mjs';
import { createMediaService } from '../src/service.mjs';

const config = JSON.stringify({ weather: { label: 'Test', latitude: 22, longitude: 114 },
  feeds: [{ label: 'Firmware', url: 'https://github.com/espressif/esp32-camera/releases.atom' }], statusProviders: ['github'] });
const rss = '<rss><channel><item><title>Hello &amp; world</title><description><![CDATA[<b>Small</b> update]]></description></item></channel></rss>';
function fixture(url) {
  if (url.includes('forecast')) return Response.json({ current: { temperature_2m: 26.5, apparent_temperature: 27,
    relative_humidity_2m: 60, weather_code: 1, wind_speed_10m: 10 } });
  if (url.includes('status.json')) return Response.json({ status: { indicator: 'none', description: 'All systems operational' } });
  return new Response(rss);
}

test('RSS and Atom are parsed as bounded plain text', () => {
  assert.equal(parseFeed(rss, 'RSS')[0].title, 'Hello & world');
  assert.match(parseFeed(rss, 'RSS')[0].detail, /Small update/);
  const atom = '<feed xmlns="http://www.w3.org/2005/Atom"><entry><title>版本</title><summary>正文</summary></entry></feed>';
  assert.equal(parseFeed(atom, 'Atom')[0].title, '版本');
  const many = `<rss><channel>${'<item><title>One</title></item>'.repeat(100)}</channel></rss>`;
  assert.equal(parseFeed(many, 'RSS').length, 6);
});
test('malformed, DTD, deeply nested and oversized XML fail closed', () => {
  for (const xml of ['<rss>', '<html/>', '<!DOCTYPE rss [<!ENTITY x SYSTEM "file:///etc/passwd">]><rss/>',
    '<rss>' + '<a>'.repeat(33) + '</a>'.repeat(33) + '</rss>', 'x'.repeat(128 * 1024 + 1)])
    assert.throws(() => parseFeed(xml, 'test'));
});
test('only reviewed public HTTPS RSS hosts and fixed status services are accepted', () => {
  assert.deepEqual(parseInfoConfig(''), {});
  for (const url of ['http://github.com/a', 'https://127.0.0.1/a', 'https://github.com.evil.test/a',
    'https://user:pass@github.com/a', 'https://github.com:444/a', 'https://github.com/a?redirect=elsewhere'])
    assert.throws(() => parseInfoConfig(JSON.stringify({ feeds: [{ label: 'bad', url }] })));
  assert.throws(() => parseInfoConfig('{"weather":{"label":"x","latitude":91,"longitude":0}}'));
  assert.throws(() => parseInfoConfig('{"statusProviders":["constructor"]}'));
  assert.throws(() => parseInfoConfig('{"feeds":{}}'));
});
test('normalized response, cache and single flight avoid repeated upstream work', async () => {
  let count = 0, now = 1000;
  const info = createInformationProvider(config, { now: () => now, fetcher: async (url, options) => {
    count++; assert.equal(options.redirect, 'error'); assert.ok(options.signal); return fixture(url);
  } });
  const [a, b] = await Promise.all([info(), info()]);
  assert.deepEqual(a, b); assert.equal(count, 3); assert.equal(a.version, 1);
  assert.ok(a.sections.every(s => s.state === 'ok'));
  assert.match(a.sections[0].items[0].title, /26.5/);
  await info(); assert.equal(count, 3);
  now += 301; await info(); assert.equal(count, 6);
  assert.ok(Buffer.byteLength(JSON.stringify(a)) <= 16384);
});
test('partial upstream outage preserves last success only within stale TTL', async () => {
  let now = 1000, fail = false;
  const info = createInformationProvider(config, { now: () => now, fetcher: async url => {
    if (fail && url.includes('github.com/')) throw new Error('private token must not escape');
    return fixture(url);
  } });
  await info(); fail = true; now += 301;
  const partial = await info();
  assert.equal(partial.sections[1].state, 'stale'); assert.equal(partial.sections[1].updatedAt, 1000);
  assert.equal(partial.sections[0].state, 'ok');
  assert.ok(!JSON.stringify(partial).includes('private token'));
  now += 1801; assert.equal((await info()).sections[1].state, 'error');
});
test('unconfigured sections do not fetch; large chunked bodies are cancelled', async () => {
  const blank = createInformationProvider('', { fetcher: () => { throw new Error('must not fetch'); } });
  assert.ok((await blank()).sections.every(s => s.state === 'unconfigured'));
  let cancelled = false;
  const info = createInformationProvider('{"statusProviders":["github"]}', { fetcher: async () => new Response(new ReadableStream({
    pull(controller) { controller.enqueue(new Uint8Array(16385)); }, cancel() { cancelled = true; },
  })) });
  assert.equal((await info()).sections[2].state, 'error'); assert.equal(cancelled, true);
});
test('/info authenticates before collecting and accepts only GET', async () => {
  let calls = 0;
  const readToken = 'r'.repeat(32), adminToken = 'a'.repeat(32);
  const service = createMediaService({ store: {}, readToken, adminToken, information: async () => { calls++; return { version: 1 }; } });
  assert.equal((await service(new Request('https://example.test/info'))).status, 401);
  assert.equal(calls, 0);
  const headers = { authorization: `Bearer ${readToken}` };
  assert.equal((await service(new Request('https://example.test/info', { headers }))).status, 200);
  assert.equal((await service(new Request('https://example.test/info', { headers, method: 'POST' }))).status, 405);
  assert.equal(calls, 1);
});
