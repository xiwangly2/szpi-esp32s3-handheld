import test from 'node:test';
import assert from 'node:assert/strict';
import { deploymentOptions, checkPlan } from '../tools/deploy-aws.mjs';

test('deploy defaults to plan only and explicitly selects Terraform/OpenTofu', () => {
  assert.deepEqual(deploymentOptions([], {}), { apply: false, executable: 'terraform' });
  assert.deepEqual(deploymentOptions(['--apply', '--engine=tofu'], {}), { apply: true, executable: 'tofu' });
  assert.equal(deploymentOptions([], { TERRAFORM_BIN: 'C:/tools/tofu.exe' }).executable, 'C:/tools/tofu.exe');
  for (const args of [['--destroy'], ['--apply', '--plan-only'], ['--engine=tofu', '--engine=terraform']])
    assert.throws(() => deploymentOptions(args, {}));
});
test('saved plan rejects destroy and replacement, while reporting additive changes', () => {
  const plan = actions => ({ resource_changes: actions.map(a => ({ change: { actions: a } })) });
  assert.deepEqual(checkPlan(plan([['create'], ['update'], ['no-op']])), { create: 1, update: 1 });
  assert.throws(() => checkPlan(plan([['delete']])));
  assert.throws(() => checkPlan(plan([['create', 'delete']])));
});
