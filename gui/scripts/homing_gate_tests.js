/**
 * Homing-gate state / callback regression tests (no browser DOM).
 * Covers startup prompt decisions and procedure continue-after-homing.
 */

const assert = require('assert');

async function loadGate() {
  return import('../public/js/homingGate.js');
}

async function testStartupPromptOnlyAfterLiveNotHomed() {
  const {
    evaluateStartupHoming,
    isValidLiveStatus,
    needsHoming,
  } = await loadGate();

  assert.strictEqual(isValidLiveStatus({ connected: false, isHomed: false }), false);
  assert.strictEqual(evaluateStartupHoming({ connected: false, isHomed: false }), 'skip');
  assert.strictEqual(evaluateStartupHoming(null), 'skip');

  assert.strictEqual(isValidLiveStatus({ connected: true, isHomed: false }), true);
  assert.strictEqual(needsHoming({ connected: true, isHomed: false }), true);
  assert.strictEqual(evaluateStartupHoming({ connected: true, isHomed: false }), 'prompt');

  assert.strictEqual(evaluateStartupHoming({ connected: true, isHomed: true }), 'markHomed');
  assert.strictEqual(
    evaluateStartupHoming({ connected: true, isHomed: false }, { alreadyHandled: true }),
    'skip'
  );
}

async function testStartupControllerOnce() {
  const { createStartupHomingController } = await loadGate();
  const prompts = [];
  const homes = [];

  const controller = createStartupHomingController({
    prompt: async (status) => {
      prompts.push(status);
      return 'cancel';
    },
    startHoming: async () => {
      homes.push(1);
    },
  });

  await controller.onStatus({ connected: false, isHomed: false });
  assert.strictEqual(prompts.length, 0);

  await controller.onStatus({ connected: true, isHomed: false });
  assert.strictEqual(prompts.length, 1);

  await controller.onStatus({ connected: true, isHomed: false });
  assert.strictEqual(prompts.length, 1);
  assert.strictEqual(homes.length, 0);
}

async function testStartupControllerNoPromptWhenHomed() {
  const { createStartupHomingController } = await loadGate();
  let prompts = 0;
  const controller = createStartupHomingController({
    prompt: async () => {
      prompts += 1;
      return 'cancel';
    },
    startHoming: async () => {},
  });

  await controller.onStatus({ connected: true, isHomed: true });
  assert.strictEqual(prompts, 0);
  assert.strictEqual(controller.getState().alreadyHandled, true);
}

async function testProcedureContinuesAfterHoming() {
  const { runWithHomingGate } = await loadGate();
  const calls = [];
  let isHomed = false;
  let homingState = 'None';

  const result = await runWithHomingGate({
    status: { connected: true, isHomed: false },
    prompt: async () => 'home',
    startHoming: async () => {
      calls.push('home');
      homingState = 'HomingZ';
      setTimeout(() => {
        isHomed = true;
        homingState = 'Homed';
      }, 30);
    },
    getStatus: async () => ({ connected: true, isHomed, homingState }),
    action: async () => {
      calls.push('startProcedure');
      return { ok: true };
    },
    intervalMs: 10,
    timeoutMs: 2000,
  });

  assert.strictEqual(result.result, 'ran');
  assert.deepStrictEqual(calls, ['home', 'startProcedure']);
}

async function testProcedureStartsDirectlyWhenHomed() {
  const { runWithHomingGate } = await loadGate();
  let homes = 0;
  let actions = 0;
  const result = await runWithHomingGate({
    status: { connected: true, isHomed: true },
    prompt: async () => {
      throw new Error('prompt must not run');
    },
    startHoming: async () => {
      homes += 1;
    },
    getStatus: async () => ({ connected: true, isHomed: true }),
    action: async () => {
      actions += 1;
    },
  });
  assert.strictEqual(result.result, 'ran');
  assert.strictEqual(homes, 0);
  assert.strictEqual(actions, 1);
}

async function testCancelDoesNotStartProcedure() {
  const { runWithHomingGate } = await loadGate();
  let actions = 0;
  let homes = 0;
  const result = await runWithHomingGate({
    status: { connected: true, isHomed: false },
    prompt: async () => 'cancel',
    startHoming: async () => {
      homes += 1;
    },
    getStatus: async () => ({ connected: true, isHomed: false }),
    action: async () => {
      actions += 1;
    },
  });
  assert.strictEqual(result.result, 'cancelled');
  assert.strictEqual(homes, 0);
  assert.strictEqual(actions, 0);
}

async function testHomingFailureDoesNotStartProcedure() {
  const { runWithHomingGate, classifyHomingOutcome } = await loadGate();
  assert.strictEqual(
    classifyHomingOutcome({ isHomed: false, homingState: 'HomingError' }),
    'failed'
  );

  let actions = 0;
  const result = await runWithHomingGate({
    status: { connected: true, isHomed: false },
    prompt: async () => 'home',
    startHoming: async () => {},
    getStatus: async () => ({
      connected: true,
      isHomed: false,
      homingState: 'HomingError',
    }),
    action: async () => {
      actions += 1;
    },
    intervalMs: 10,
    timeoutMs: 500,
  });

  assert.strictEqual(result.result, 'failed');
  assert.strictEqual(actions, 0);
}

async function main() {
  await testStartupPromptOnlyAfterLiveNotHomed();
  await testStartupControllerOnce();
  await testStartupControllerNoPromptWhenHomed();
  await testProcedureContinuesAfterHoming();
  await testProcedureStartsDirectlyWhenHomed();
  await testCancelDoesNotStartProcedure();
  await testHomingFailureDoesNotStartProcedure();
  console.log('homing_gate_tests.js: PASS');
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
