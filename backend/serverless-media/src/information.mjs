import { SaxesParser } from 'saxes';

const RSS_HOSTS = new Set(['github.com', 'www.espressif.com', 'blog.cloudflare.com', 'aws.amazon.com']);
const STATUS = {
  github: ['GitHub', 'https://www.githubstatus.com/api/v2/status.json'],
  cloudflare: ['Cloudflare', 'https://www.cloudflarestatus.com/api/v2/status.json'],
};
const text = (value, limit = 160) => Array.from(typeof value === 'string' ? value : '')
  .join('').replace(/<[^>]*>/g, ' ').replace(/[\u0000-\u001f\u007f]/g, ' ').replace(/\s+/g, ' ').trim()
  .slice(0, limit).replace(/[\uD800-\uDBFF]$/, '');
const epoch = () => Math.floor(Date.now() / 1000);

export function parseInfoConfig(raw = '') {
  if (!raw) return {};
  if (typeof raw !== 'string' || raw.length > 4096) throw new Error('Invalid info configuration');
  const config = JSON.parse(raw);
  if (!config || Array.isArray(config) || typeof config !== 'object') throw new Error('Invalid info configuration');
  if (config.weather) {
    const w = config.weather;
    if (!Number.isFinite(w.latitude) || Math.abs(w.latitude) > 90 ||
        !Number.isFinite(w.longitude) || Math.abs(w.longitude) > 180 || !text(w.label, 40))
      throw new Error('Invalid weather location');
  }
  if (config.feeds !== undefined && (!Array.isArray(config.feeds) || config.feeds.length > 2))
    throw new Error('At most two feeds');
  for (const feed of config.feeds || []) {
    const url = new URL(feed.url);
    // No caller-supplied proxy targets, redirects, custom ports or credentials.
    // Extend this deployment-time allowlist only after reviewing the upstream.
    if (url.protocol !== 'https:' || !RSS_HOSTS.has(url.hostname) || url.port || url.username || url.password ||
        url.hash || url.search || url.href.length > 256 || !text(feed.label, 40))
      throw new Error('Feed URL is not on the public HTTPS allowlist');
  }
  if (config.statusProviders !== undefined && (!Array.isArray(config.statusProviders) ||
      config.statusProviders.length > 2 || config.statusProviders.some(p => !Object.hasOwn(STATUS, p))))
    throw new Error('Unsupported status provider');
  return config;
}

async function readUpstream(url, fetcher, limit = 128 * 1024) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 4500);
  let reader;
  try {
    const response = await fetcher(url, { signal: controller.signal, redirect: 'error', headers: {
      Accept: 'application/json, application/atom+xml, application/rss+xml, application/xml, text/xml',
      'User-Agent': 'szpi-handheld-information/1.0',
    } });
    if (!response.ok || Number(response.headers.get('content-length')) > limit || !response.body)
      throw new Error('Upstream unavailable');
    reader = response.body.getReader();
    const decoder = new TextDecoder('utf-8', { fatal: true });
    let body = '', size = 0;
    for (;;) {
      const { value, done } = await reader.read();
      if (done) break;
      size += value.byteLength;
      if (size > limit) throw new Error('Upstream too large');
      body += decoder.decode(value, { stream: true });
    }
    return body + decoder.decode();
  } finally {
    if (reader) { try { await reader.cancel(); } catch {} reader.releaseLock(); }
    clearTimeout(timer);
    controller.abort();
  }
}

export function parseFeed(xml, label) {
  if (new TextEncoder().encode(xml).length > 128 * 1024) throw new Error('Feed too large');
  const parser = new SaxesParser({ xmlns: true });
  const stack = [], items = [];
  let item = null, itemDepth = 0, capture = '', fieldDepth = 0, buffer = '', validRoot = false;
  parser.on('doctype', () => { throw new Error('DTD is not supported'); });
  parser.on('opentag', node => {
    stack.push(node.local);
    if (stack.length > 32) throw new Error('Feed too deeply nested');
    if (stack.length === 1) validRoot = ['rss', 'feed', 'RDF'].includes(node.local);
    if (!item && ['item', 'entry'].includes(node.local)) { item = {}; itemDepth = stack.length; }
    else if (item && stack.length === itemDepth + 1 && ['title', 'summary', 'description', 'pubDate', 'updated'].includes(node.local)) {
      capture = node.local; fieldDepth = stack.length; buffer = '';
    }
  });
  const append = value => { if (capture) buffer = (buffer + value).slice(0, 2048); };
  parser.on('text', append); parser.on('cdata', append);
  parser.on('closetag', () => {
    if (capture && stack.length === fieldDepth) { item[capture] = text(buffer, 240); capture = ''; }
    if (item && stack.length === itemDepth) {
      if (item.title && items.length < 6) items.push({ title: text(item.title, 80),
        detail: text([label, item.pubDate || item.updated, item.summary || item.description].filter(Boolean).join('\n'), 240) });
      item = null;
    }
    stack.pop();
  });
  parser.write(xml).close();
  if (!validRoot) throw new Error('Not RSS or Atom');
  return items;
}

function weatherName(code) {
  if (code === 0) return '晴';
  if ([1, 2, 3].includes(code)) return '多云';
  if ([45, 48].includes(code)) return '雾';
  if ([51, 53, 55, 56, 57].includes(code)) return '毛毛雨';
  if ([61, 63, 65, 66, 67, 80, 81, 82].includes(code)) return '雨';
  if ([71, 73, 75, 77, 85, 86].includes(code)) return '雪';
  if ([95, 96, 99].includes(code)) return '雷暴';
  return '天气未知';
}

export function createInformationProvider(raw, { fetcher = fetch, now = epoch } = {}) {
  const config = parseInfoConfig(raw);
  let cached, expires = 0, pending;
  const previous = new Map();
  async function section(id, title, source, enabled, load) {
    const base = { id, title, source, state: 'unconfigured', updatedAt: 0, items: [] };
    if (!enabled) return base;
    try {
      const result = { ...base, state: 'ok', updatedAt: now(), items: await load() };
      previous.set(id, result); return result;
    } catch {
      const old = previous.get(id);
      return old && now() - old.updatedAt <= 1800 ? { ...old, state: 'stale' } : { ...base, state: 'error' };
    }
  }
  async function collect() {
    const sections = await Promise.all([
      section('weather', '天气', 'Open-Meteo.com (CC BY 4.0)', !!config.weather, async () => {
        const w = config.weather;
        const query = new URLSearchParams({ latitude: String(w.latitude), longitude: String(w.longitude),
          current: 'temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m', timezone: 'GMT' });
        const body = JSON.parse(await readUpstream(`https://api.open-meteo.com/v1/forecast?${query}`, fetcher, 16384));
        const c = body.current;
        if (!c || !['temperature_2m', 'relative_humidity_2m', 'apparent_temperature', 'weather_code', 'wind_speed_10m']
          .every(key => Number.isFinite(c[key]))) throw new Error('Weather data missing');
        return [{ title: `${text(w.label, 40)}  ${c.temperature_2m.toFixed(1)} C`,
          detail: `${weatherName(c.weather_code)}\n体感 ${c.apparent_temperature.toFixed(1)} C\n湿度 ${c.relative_humidity_2m}%  风速 ${c.wind_speed_10m} km/h` }];
      }),
      section('rss', 'RSS', 'RSS / Atom', !!config.feeds?.length, async () => {
        const results = await Promise.all(config.feeds.map(async f => parseFeed(await readUpstream(f.url, fetcher), text(f.label, 40))));
        // Keep the wire response small even with two busy feeds.
        return results.flatMap(rows => rows.slice(0, config.feeds.length > 1 ? 3 : 6));
      }),
      section('services', '服务状态', 'Official status pages', !!config.statusProviders?.length, async () =>
        Promise.all(config.statusProviders.map(async provider => {
          const [name, url] = STATUS[provider];
          const data = JSON.parse(await readUpstream(url, fetcher, 16384));
          if (!data.status || !['none', 'minor', 'major', 'critical', 'maintenance'].includes(data.status.indicator))
            throw new Error('Status data missing');
          const names = { none: '正常', minor: '部分异常', major: '严重异常', critical: '严重中断', maintenance: '维护中' };
          return { title: `${name}: ${names[data.status.indicator]}`, detail: text(data.status.description, 240) };
        }))),
    ]);
    const result = { version: 1, updatedAt: now(), refreshAfter: 300, sections };
    if (new TextEncoder().encode(JSON.stringify(result)).length > 16384) throw new Error('Info response too large');
    cached = result;
    expires = now() + (sections.some(s => ['error', 'stale'].includes(s.state)) ? 60 : 300);
    return result;
  }
  return async () => {
    if (cached && now() < expires) return cached;
    // Coalesce concurrent requests within a warm function/Worker instance.
    pending ??= collect().finally(() => { pending = null; });
    return pending;
  };
}
