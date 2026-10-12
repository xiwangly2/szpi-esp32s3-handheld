import { createReadStream } from 'node:fs';
import { readFile, stat } from 'node:fs/promises';
import { basename } from 'node:path';
import { createHash } from 'node:crypto';
import { parseStringPromise } from 'xml2js';

const file = process.argv[2];
if (!file) throw new Error('Usage: npm run upload -- path/to/converted.avi');
const deployment = JSON.parse(await readFile('.deploy/aws.json', 'utf8'));
const secrets = JSON.parse(await readFile('.deploy/aws-secrets.json', 'utf8'));
const size = (await stat(file)).size;
const sha = createHash('sha256'), md5 = createHash('md5');
for await (const chunk of createReadStream(file)) { sha.update(chunk); md5.update(chunk); }
const manifest = { size, sha256: sha.digest('hex'), md5: md5.digest('base64') };
const response = await fetch(`${deployment.apiUrl}/uploads/${encodeURIComponent(basename(file))}`, {
  method: 'POST', headers: { Authorization: `Bearer ${secrets.adminToken}`, 'Content-Type': 'application/json' }, body: JSON.stringify(manifest),
});
if (!response.ok) throw new Error(`Upload ticket failed: HTTP ${response.status} ${await response.text()}`);
const ticket = await response.json();
const upload = await fetch(ticket.url, { method: 'PUT', headers: ticket.headers, body: createReadStream(file), duplex: 'half' });
if (!upload.ok) {
  const detail = await parseStringPromise(await upload.text()).catch(() => ({}));
  throw new Error(`Object upload failed: HTTP ${upload.status} ${detail.Error?.Code?.[0] || ''} ${detail.Error?.Message?.[0] || ''}`);
}
console.log(JSON.stringify({ file: basename(file), size, sha256: manifest.sha256, uploaded: true }));
