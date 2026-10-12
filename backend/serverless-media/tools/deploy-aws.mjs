import { execFileSync } from 'node:child_process';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { randomBytes } from 'node:crypto';
import { resolve } from 'node:path';

const region = process.env.AWS_REGION || 'ap-east-1';
const directory = resolve('infra/aws');
await mkdir('.deploy', { recursive: true });
const secretFile = resolve('.deploy/aws-secrets.json');
let secrets;
try { secrets = JSON.parse(await readFile(secretFile, 'utf8')); }
catch (error) {
  if (error.code !== 'ENOENT') throw error;
  secrets = { readToken: randomBytes(32).toString('hex'), adminToken: randomBytes(32).toString('hex') };
  await writeFile(secretFile, JSON.stringify(secrets), { mode: 0o600 });
}
const env = { ...process.env, TF_VAR_region: region, TF_VAR_read_token: secrets.readToken, TF_VAR_admin_token: secrets.adminToken };
const terraform = process.env.TERRAFORM_BIN || 'terraform';
const run = (...args) => execFileSync(terraform, [`-chdir=${directory}`, ...args], { env, stdio: 'inherit' });
run('init', '-input=false');
run('validate');
run('plan', '-input=false', '-out=deploy.tfplan');
if (process.argv.includes('--plan-only')) {
  console.log('Plan saved. No cloud resources have been changed.');
  process.exit(0);
}
run('apply', '-input=false', 'deploy.tfplan');
const outputs = JSON.parse(execFileSync(terraform, [`-chdir=${directory}`, 'output', '-json'], { env, encoding: 'utf8' }));
const result = { region, apiUrl: outputs.api_url.value, bucket: outputs.bucket.value, functionName: outputs.function_name.value };
await writeFile('.deploy/aws.json', JSON.stringify(result, null, 2));
await writeFile('.deploy/video.ini', `url=${result.apiUrl}/media/BadApple.avi?key=${secrets.readToken}\n`, { mode: 0o600 });
console.log(JSON.stringify(result, null, 2));
console.log('Device URL saved privately to .deploy/video.ini. Tokens were not printed.');
