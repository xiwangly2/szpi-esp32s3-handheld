'use strict';
const $ = id => document.getElementById(id);
let session = '', busy = false, savedUrl = '', tokenSet = false;
const notice = (message, error = false) => { $('notice').textContent = message; $('notice').classList.toggle('error', error); };
const lock = value => { busy = value; document.querySelectorAll('button,input').forEach(control => { control.disabled = value; }); };

async function request(path, data) {
  const response = await fetch(path, { method: data ? 'POST' : 'GET', cache: 'no-store', credentials: 'omit',
    headers: { ...(data ? { 'Content-Type': 'application/json' } : {}), ...(session ? { 'X-Setup-Token': session } : {}) },
    ...(data ? { body: JSON.stringify(data) } : {}), signal: AbortSignal.timeout(7000) });
  if (!response.ok) {
    if (response.status === 403) throw new Error('配对码错误或连接已过期，请查看设备屏幕。');
    if (response.status === 409) throw new Error('设备正在保存，请稍后重试。');
    throw new Error('配置无效或设备暂不可用。');
  }
  return response.json();
}

$('pair-form').addEventListener('submit', async event => {
  event.preventDefault(); if (busy) return; lock(true); notice('正在配对…');
  try {
    session = (await request('/api/pair', { pin: $('pin').value })).session;
    const config = await request('/api/config');
    savedUrl = config.url; tokenSet = config.tokenSet;
    $('url').value = config.url; $('token-state').textContent = config.tokenSet ? '已保存，留空保留' : '尚未设置';
    $('pairing').hidden = true; $('settings').hidden = false; $('pin').value = ''; notice('');
  } catch (error) { session = ''; notice(error.message || '设备连接中断。', true); }
  finally { lock(false); }
});

async function save(data) {
  if (busy) return; lock(true); notice('正在写入设备…');
  let saved = false;
  try {
    await request('/api/config', data);
    for (let attempt = 0; attempt < 20; attempt++) {
      await new Promise(resolve => setTimeout(resolve, 250));
      const result = await request('/api/config');
      if (result.state === 'saved') { saved = true; break; }
      if (result.state === 'failed') throw new Error('设备保存失败，请在设备上确认当前配置。');
    }
    if (!saved) throw new Error('保存结果尚未确认，请查看设备。');
    $('token').value = ''; session = ''; $('connection').textContent = '已保存';
    notice('已保存。设备将关闭配置入口并更新信息台。');
  } catch (error) { notice(error.message || '连接中断，请查看设备上的保存结果。', true); }
  finally { if (!saved) lock(false); }
}

$('config-form').addEventListener('submit', event => {
  event.preventDefault(); const token = $('token').value.trim();
  if ($('clear-token').checked && token) { notice('清除令牌与填写新令牌不能同时选择。', true); return; }
  if (tokenSet && $('url').value.trim() !== savedUrl && !token && !$('clear-token').checked) {
    notice('更换服务地址时，请重新填写令牌或选择清除。', true); return;
  }
  save({ url: $('url').value.trim(), token, action: $('clear-token').checked ? 'clear' : token ? 'replace' : 'keep' });
});
$('reset').addEventListener('click', () => {
  if (confirm('清除设备已保存的信息台地址和令牌？')) save({ url: '', token: '', action: 'clear' });
});
$('reveal').addEventListener('click', () => {
  const reveal = $('token').type === 'password'; $('token').type = reveal ? 'text' : 'password';
  $('reveal').textContent = reveal ? '隐藏' : '显示'; $('reveal').setAttribute('aria-pressed', String(reveal));
  $('reveal').setAttribute('aria-label', reveal ? '隐藏令牌' : '显示令牌');
});
$('config-file').addEventListener('change', async event => {
  const file = event.target.files[0]; if (!file) return;
  try {
    if (file.size > 4096) throw new Error('配置文件超过 4 KiB。');
    const config = {};
    for (const raw of (await file.text()).split(/\r?\n/)) {
      const line = raw.trim(); if (!line || line.startsWith('#') || line.startsWith(';')) continue;
      const match = /^(url|token)=(.*)$/.exec(line);
      if (match) { if (Object.hasOwn(config, match[1])) throw new Error('配置包含重复字段。'); config[match[1]] = match[2].trim(); }
    }
    if (!config.url || config.url.length >= 384 || !config.url.startsWith('https://') ||
        config.token === undefined || config.token.length > 96 || !/^[A-Za-z0-9_-]*$/.test(config.token)) throw new Error('不是有效的信息台配置。');
    $('url').value = config.url; $('token').value = config.token; $('clear-token').checked = false;
    notice('已导入，尚未保存到设备。');
  } catch (error) { notice(error.message, true); }
  finally { event.target.value = ''; }
});
