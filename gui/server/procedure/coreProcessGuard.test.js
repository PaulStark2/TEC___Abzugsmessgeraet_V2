'use strict';

const assert = require('assert');
const {
  isCoreProcessRunning,
  classifyRunningPhase,
  formatProcedurePhaseLabel,
  describeRunningCoreProcess,
  validateCoreNotRunningForProcedureStart,
  isProcedureStartButtonDisabled,
} = require('./coreProcessGuard');

function testIdleAllowsStart() {
  const r = validateCoreNotRunningForProcedureStart({
    processState: 'Idle',
    procedurePhase: 'Idle',
  });
  assert.strictEqual(r.ok, true);
}

function testCompletedAllowsStart() {
  const r = validateCoreNotRunningForProcedureStart({
    processState: 'Completed',
    procedurePhase: 'Completed',
  });
  assert.strictEqual(r.ok, true);
}

function testM19RunningBlocksWithMessage() {
  const r = validateCoreNotRunningForProcedureStart({
    processState: 'Running',
    procedurePhase: 'ProbeCalibrate',
  });
  assert.strictEqual(r.ok, false);
  assert.strictEqual(r.code, 'core_process_running');
  assert.strictEqual(r.kind, 'probe_calib');
  assert.ok(r.error.includes('Messtaster-Kalibrierung'));
  assert.ok(!r.error.toLowerCase().includes('invalid state'));
}

function testProcedurePhaseBlocksWithProcedureMessage() {
  for (const phase of ['ProbeApproach', 'PointApproach', 'Clamp', 'TestOrAge']) {
    const r = validateCoreNotRunningForProcedureStart({
      processState: 'Running',
      procedurePhase: phase,
    });
    assert.strictEqual(r.ok, false, phase);
    assert.strictEqual(r.kind, 'procedure', phase);
    assert.strictEqual(r.error, 'Die Prüfprozedur läuft bereits.', phase);
  }
}

function testUnknownPhaseGeneralMessage() {
  const r = validateCoreNotRunningForProcedureStart({
    processState: 'Running',
    procedurePhase: 'WeirdFuturePhase',
  });
  assert.strictEqual(r.ok, false);
  assert.strictEqual(r.kind, 'other');
  assert.ok(r.error.includes('anderer Maschinenprozess'));
}

function testButtonDisabledWhileRunning() {
  assert.strictEqual(
    isProcedureStartButtonDisabled({ procedureState: 'idle', coreProcessState: 'Running' }),
    true
  );
  assert.strictEqual(
    isProcedureStartButtonDisabled({ procedureState: 'running', coreProcessState: 'Idle' }),
    true
  );
}

function testButtonEnabledAfterCompleted() {
  assert.strictEqual(
    isProcedureStartButtonDisabled({ procedureState: 'idle', coreProcessState: 'Completed' }),
    false
  );
  assert.strictEqual(
    isProcedureStartButtonDisabled({ procedureState: 'idle', coreProcessState: 'Idle' }),
    false
  );
}

function testLabels() {
  assert.strictEqual(isCoreProcessRunning('Running'), true);
  assert.strictEqual(isCoreProcessRunning('Completed'), false);
  assert.strictEqual(classifyRunningPhase('ProbeCalibrate'), 'probe_calib');
  assert.strictEqual(classifyRunningPhase('PointApproach'), 'procedure');
  assert.strictEqual(formatProcedurePhaseLabel('ProbeCalibrate'), 'Messtaster kalibrieren');
  assert.strictEqual(
    describeRunningCoreProcess('ProbeCalibrate').processLabel,
    'Kalibrierung läuft'
  );
  assert.strictEqual(describeRunningCoreProcess('ProbeApproach').processLabel, 'Prüfprozedur läuft');
}

testIdleAllowsStart();
testCompletedAllowsStart();
testM19RunningBlocksWithMessage();
testProcedurePhaseBlocksWithProcedureMessage();
testUnknownPhaseGeneralMessage();
testButtonDisabledWhileRunning();
testButtonEnabledAfterCompleted();
testLabels();
console.log('coreProcessGuard.test.js: PASS');
