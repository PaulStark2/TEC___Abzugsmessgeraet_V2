/**
 * Homing-gate state / callback regression tests (no browser DOM).
 * Covers startup prompt decisions, MCS gate, and procedure continue-after-homing.
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

  assert.strictEqual(
    evaluateStartupHoming({ connected: true, isHomed: false, mcsValid: false }),
    'needFullCalibration'
  );

  assert.strictEqual(isValidLiveStatus({ connected: true, isHomed: false, mcsValid: true }), true);
  assert.strictEqual(needsHoming({ connected: true, isHomed: false, mcsValid: true }), true);
  assert.strictEqual(
    evaluateStartupHoming({ connected: true, isHomed: false, mcsValid: true }),
    'prompt'
  );

  assert.strictEqual(
    evaluateStartupHoming({ connected: true, isHomed: true, mcsValid: true }),
    'markHomed'
  );
  assert.strictEqual(
    evaluateStartupHoming({ connected: true, isHomed: false, mcsValid: true }, { alreadyHandled: true }),
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

  await controller.onStatus({ connected: true, isHomed: false, mcsValid: true });
  assert.strictEqual(prompts.length, 1);

  await controller.onStatus({ connected: true, isHomed: false, mcsValid: true });
  assert.strictEqual(prompts.length, 1);
  assert.strictEqual(homes.length, 0);
}

async function testStartupNoHomeWithoutMcs() {
  const { createStartupHomingController, MCS_REQUIRED_MESSAGE } = await loadGate();
  const homes = [];
  let needFull = 0;
  const controller = createStartupHomingController({
    startHoming: async () => {
      homes.push(1);
    },
    onNeedFullCalibration: async () => {
      needFull += 1;
    },
  });

  await controller.onStatus({ connected: true, isHomed: false, mcsValid: false });
  assert.strictEqual(needFull, 1);
  assert.strictEqual(homes.length, 0);
  assert.ok(MCS_REQUIRED_MESSAGE.includes('Maschinenkoordinatensystem'));
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

  await controller.onStatus({ connected: true, isHomed: true, mcsValid: true });
  assert.strictEqual(prompts, 0);
  assert.strictEqual(controller.getState().alreadyHandled, true);
}

async function testProcedureContinuesAfterHoming() {
  const { runWithHomingGate } = await loadGate();
  const calls = [];
  let isHomed = false;
  let homingState = 'None';

  const result = await runWithHomingGate({
    status: { connected: true, isHomed: false, mcsValid: true },
    prompt: async () => 'home',
    startHoming: async () => {
      calls.push('home');
      isHomed = true;
      homingState = 'Homed';
    },
    getStatus: async () => ({
      connected: true,
      isHomed,
      mcsValid: true,
      homingState,
    }),
    action: async () => {
      calls.push('action');
      return 'ok';
    },
    intervalMs: 10,
    timeoutMs: 500,
  });

  assert.strictEqual(result.result, 'ran');
  assert.deepStrictEqual(calls, ['home', 'action']);
}

async function testProcedureBlockedWithoutMcs() {
  const { runWithHomingGate } = await loadGate();
  let homes = 0;
  let actions = 0;
  const result = await runWithHomingGate({
    status: { connected: true, isHomed: false, mcsValid: false },
    prompt: async () => 'cancel',
    startHoming: async () => {
      homes += 1;
    },
    getStatus: async () => ({ connected: true, isHomed: false, mcsValid: false }),
    action: async () => {
      actions += 1;
    },
  });
  assert.strictEqual(result.result, 'mcs_required');
  assert.strictEqual(homes, 0);
  assert.strictEqual(actions, 0);
}

async function testProcedureStartsDirectlyWhenHomed() {
  const { runWithHomingGate } = await loadGate();
  let homes = 0;
  const result = await runWithHomingGate({
    status: { connected: true, isHomed: true, mcsValid: true },
    startHoming: async () => {
      homes += 1;
    },
    getStatus: async () => ({ connected: true, isHomed: true, mcsValid: true }),
    action: async () => 'started',
  });
  assert.strictEqual(result.result, 'ran');
  assert.strictEqual(result.actionResult, 'started');
  assert.strictEqual(homes, 0);
}

async function testCancelDoesNotStartProcedure() {
  const { runWithHomingGate } = await loadGate();
  let actions = 0;
  const result = await runWithHomingGate({
    status: { connected: true, isHomed: false, mcsValid: true },
    prompt: async () => 'cancel',
    startHoming: async () => {},
    getStatus: async () => ({ connected: true, isHomed: false, mcsValid: true }),
    action: async () => {
      actions += 1;
    },
  });
  assert.strictEqual(result.result, 'cancelled');
  assert.strictEqual(actions, 0);
}

async function testHomingFailureDoesNotStartProcedure() {
  const { runWithHomingGate } = await loadGate();
  let actions = 0;
  const result = await runWithHomingGate({
    status: { connected: true, isHomed: false, mcsValid: true },
    prompt: async () => 'home',
    startHoming: async () => {},
    getStatus: async () => ({
      connected: true,
      isHomed: false,
      mcsValid: true,
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
  await testStartupNoHomeWithoutMcs();
  await testStartupControllerNoPromptWhenHomed();
  await testProcedureContinuesAfterHoming();
  await testProcedureBlockedWithoutMcs();
  await testProcedureStartsDirectlyWhenHomed();
  await testCancelDoesNotStartProcedure();
  await testHomingFailureDoesNotStartProcedure();
  console.log('homing_gate_tests.js: PASS');
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
