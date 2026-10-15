import { execFileSync } from 'node:child_process';
import { mkdir, readFile, writeFile, copyFile } from 'node:fs/promises';
import { randomBytes } from 'node:crypto';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseInfoConfig } from '../src/information.mjs';

export function deploymentOptions(args, env = process.env) {
  const allowed = new Set(['--plan-only', '--apply', '--engine=terraform', '--engine=tofu']);
  if (args.some(arg => !allowed.has(arg)) || (args.includes('--apply') && args.includes('--plan-only')) ||
      args.filter(a => a.startsWith('--engine=')).length > 1) throw new Error('Use --plan-only or --apply, and optional --engine=terraform|tofu');
  return { apply: args.includes('--apply'), executable: args.find(a => a.startsWith('--engine='))?.slice(9) || env.TERRAFORM_BIN || 'terraform' };
}

export function checkPlan(plan) {
  if (!Array.isArray(plan.resource_changes)) throw new Error('Plan did not contain resource changes');
  const changes = plan.resource_changes;
  if (changes.some(c => c.change?.actions?.includes('delete'))) throw new Error('Refusing a destructive/replacement plan; review manually');
  return { create: changes.filter(c => c.change.actions.includes('create')).length,
    update: changes.filter(c => c.change.actions.includes('update')).length };
}

async function optionalJson(file) {
  try { return JSON.parse(await readFile(file, 'utf8')); }
  catch (error) { if (error.code === 'ENOENT') return null; throw error; }
}

async function main() {
  const options = deploymentOptions(process.argv.slice(2));
  const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
  const directory = resolve(root, 'infra/aws'), privateDir = resolve(root, '.deploy');
  await mkdir(privateDir, { recursive: true });
  const previous = await optionalJson(resolve(privateDir, 'aws.json'));
  const region = process.env.AWS_REGION || previous?.region || 'ap-east-1';
  const secretFile = resolve(privateDir, 'aws-secrets.json');
  let secrets = await optionalJson(secretFile);
  const state = await optionalJson(resolve(directory, 'terraform.tfstate'));
  if (!secrets) {
    if (state?.resources?.length || previous) throw new Error('Existing deployment found but secrets are missing; restore them instead of rotating silently');
    secrets = { readToken: randomBytes(32).toString('hex'), adminToken: randomBytes(32).toString('hex') };
    await writeFile(secretFile, JSON.stringify(secrets), { mode: 0o600, flag: 'wx' });
  }
  if (!/^[a-f0-9]{64}$/i.test(secrets.readToken || '') || !/^[a-f0-9]{64}$/i.test(secrets.adminToken || '') ||
      secrets.readToken === secrets.adminToken) throw new Error('Invalid saved tokens');
  const info = await optionalJson(resolve(privateDir, 'info.json')) ?? await optionalJson(resolve(root, 'info.example.json'));
  const informationConfig = JSON.stringify(info);
  parseInfoConfig(informationConfig);
  if (state) {
    const backupDir = resolve(privateDir, 'state-backups');
    await mkdir(backupDir, { recursive: true });
    await copyFile(resolve(directory, 'terraform.tfstate'), resolve(backupDir, `${Date.now()}.tfstate`));
  }
  const env = { ...process.env, TF_VAR_region: region, TF_VAR_read_token: secrets.readToken,
    TF_VAR_admin_token: secrets.adminToken, TF_VAR_information_config: informationConfig };
  const run = (...args) => execFileSync(options.executable, [`-chdir=${directory}`, ...args], { env, stdio: 'inherit' });
  const capture = (...args) => JSON.parse(execFileSync(options.executable, [`-chdir=${directory}`, ...args], { env, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024 }));
  run('init', '-input=false'); run('validate');
  run('plan', '-input=false', '-lock-timeout=60s', '-out=deploy.tfplan');
  console.log('Plan resource changes:', checkPlan(capture('show', '-json', 'deploy.tfplan')));
  if (!options.apply) { console.log('Plan only. Run again with --apply after reviewing resources and costs.'); return; }
  run('apply', '-input=false', 'deploy.tfplan');
  const outputs = capture('output', '-json');
  const result = { region, apiUrl: outputs.api_url.value, bucket: outputs.bucket.value, functionName: outputs.function_name.value };
  await writeFile(resolve(privateDir, 'aws.json'), JSON.stringify(result, null, 2), { mode: 0o600 });
  await writeFile(resolve(privateDir, 'video.ini'), `url=${result.apiUrl}/media/BadApple.avi?key=${secrets.readToken}\n`, { mode: 0o600 });
  await writeFile(resolve(privateDir, 'info.ini'), `url=${result.apiUrl}/info\ntoken=${secrets.readToken}\n`, { mode: 0o600 });
  console.log(JSON.stringify(result, null, 2));
  console.log('Private device configs saved to .deploy/video.ini and .deploy/info.ini. Tokens were not printed.');
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  await main();
}
