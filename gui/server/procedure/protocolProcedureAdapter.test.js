'use strict';

/**
 * Regression: stale Cached Completed (e.g. after M19 calib) must not finish a
 * freshly started procedure. Completion requires Running → Completed.
 *
 * Also: start while Core process Running (M19) must not send the start chain.
 */

const assert = require('assert');
const programStore = require('../program/programStore');
const selectionStore = require('../program/selectionStore');
const resultsStore = require('../results/resultsStore');
const procedureStore = require('./procedureStore');
const calibrationStore = require('../config/calibrationStore');
const { createProtocolProcedureAdapter } = require('./protocolProcedureAdapter');

function createMockClient(initialStatus) {
  let status = {
    connected: true,
    processState: 'Idle',
    procedurePhase: 'Idle',
    currentPointIndex: null,
    programPointCount: 0,
    x: 0,
    y: 0,
    z: 0,
    fx: 0,
    fy: 0,
    fz: 0,
    machineState: 'Idle',
    faultReason: 'None',
    spreaderState: 'Retracted',
    isHomed: true,
    communicationWatchdogEnabled: false,
    forceSampleFresh: true,
    plateOriginValid: true,
    probeCenterValid: true,
    ...initialStatus,
  };

  const sent = [];

  return {
    sent,
    getStatus: () => ({ ...status }),
    setStatus(partial) {
      status = { ...status, ...partial };
    },
    async setMachineConfig() {
      sent.push('CmdSetMachineConfig');
      return { ok: true, resultCode: 0 };
    },
    async setPlateOrigin() {
      sent.push('CmdSetPlateOrigin');
      return { ok: true, resultCode: 0 };
    },
    async setProbeCenter() {
      sent.push('CmdSetProbeCenter');
      return { ok: true, resultCode: 0 };
    },
    async setTestConfiguration() {
      sent.push('CmdSetTestConfiguration');
      return { ok: true, resultCode: 0 };
    },
    async loadProgram(points) {
      sent.push('CmdLoadProgram');
      status.programPointCount = points.length;
      return { ok: true, resultCode: 0 };
    },
    async start() {
      sent.push('CmdStart');
      // Intentionally do NOT update processState here — mirrors real client
      // where CmdStart Ack does not refresh StatusReport cache.
      return { ok: true, resultCode: 0 };
    },
    async pause() {
      return { ok: true };
    },
    async resume() {
      return { ok: true };
    },
    async stop() {
      sent.push('CmdStop');
      return { ok: true };
    },
  };
}

function withPreparedStores(fn) {
  return async () => {
    programStore.clearProgram();
    selectionStore.clear();
    resultsStore.clear();
    procedureStore.reset();

    const previousCalib = calibrationStore.load();
    calibrationStore.save({
      ...previousCalib,
      plateOrigin: { x: 0, y: 0, z: 0 },
      plateOriginValid: true,
      probeCenter: { x: 40, y: 40, z: 10 },
      probeCenterValid: true,
      toolOffset: { x: 0, y: 0, z: 0 },
    });

    try {
      programStore.setProgram({
        version: 1,
        programName: 'LatchUnit',
        testPoints: [{ name: 'A', sequenceNumber: 1, x: 1, y: 0, z: 0 }],
      });
      selectionStore.setDraft({ selectAll: true });
      selectionStore.confirmSelection();
      await fn();
    } finally {
      calibrationStore.save(previousCalib);
      programStore.clearProgram();
      selectionStore.clear();
      resultsStore.clear();
      procedureStore.reset();
    }
  };
}

async function testStaleCompletedDoesNotFinishNewStart() {
  await withPreparedStores(async () => {
    const client = createMockClient({
      processState: 'Completed',
      procedurePhase: 'Completed',
    });
    const adapter = createProtocolProcedureAdapter(client);

    const started = await adapter.start({
      mode: 'test',
      insertionForce: 10,
      iterations: 1,
    });
    assert.strictEqual(started.ok, true, 'start must succeed');
    assert.strictEqual(
      started.status.state,
      'running',
      'stale Completed must not mark procedure completed immediately'
    );
    assert.strictEqual(started.status.progress.isComplete, false);
    assert.ok(client.sent.includes('CmdStart'));

    const mid = adapter.getStatus();
    assert.strictEqual(mid.state, 'running', 'second observe with stale Completed stays running');
    assert.strictEqual(mid.progress.isComplete, false);

    client.setStatus({ processState: 'Running', procedurePhase: 'ProbeApproach' });
    const running = adapter.getStatus();
    assert.strictEqual(running.state, 'running');
    assert.strictEqual(running.processState, 'Running');

    client.setStatus({ processState: 'Completed', procedurePhase: 'Completed' });
    const done = adapter.getStatus();
    assert.strictEqual(done.state, 'completed', 'Running → Completed must complete');
    assert.strictEqual(done.progress.isComplete, true);

    adapter.dispose();
  })();
}

async function testStartWhileIdleSendsFullChain() {
  await withPreparedStores(async () => {
    const client = createMockClient({ processState: 'Idle', procedurePhase: 'Idle' });
    const adapter = createProtocolProcedureAdapter(client);
    const started = await adapter.start({ mode: 'test', insertionForce: 10, iterations: 1 });
    assert.strictEqual(started.ok, true);
    assert.deepStrictEqual(client.sent, [
      'CmdSetMachineConfig',
      'CmdSetPlateOrigin',
      'CmdSetProbeCenter',
      'CmdSetTestConfiguration',
      'CmdLoadProgram',
      'CmdStart',
    ]);
    adapter.dispose();
  })();
}

async function testStartAfterM19CompletedSendsFullChain() {
  await withPreparedStores(async () => {
    const client = createMockClient({
      processState: 'Completed',
      procedurePhase: 'Completed',
      machineState: 'Stopped',
    });
    const adapter = createProtocolProcedureAdapter(client);
    const started = await adapter.start({ mode: 'test', insertionForce: 10, iterations: 1 });
    assert.strictEqual(started.ok, true);
    assert.ok(client.sent.includes('CmdStart'));
    assert.ok(!client.sent.includes('CmdStop'));
    adapter.dispose();
  })();
}

async function testStartDuringM19SendsNoCommands() {
  await withPreparedStores(async () => {
    const client = createMockClient({
      processState: 'Running',
      procedurePhase: 'ProbeCalibrate',
    });
    const adapter = createProtocolProcedureAdapter(client);
    const started = await adapter.start({ mode: 'test', insertionForce: 10, iterations: 1 });
    assert.strictEqual(started.ok, false);
    assert.strictEqual(started.code, 'core_process_running');
    assert.ok(started.error.includes('Messtaster-Kalibrierung'));
    assert.ok(!String(started.error).toLowerCase().includes('invalid state'));
    assert.deepStrictEqual(client.sent, [], 'no start-chain commands while M19 Running');
    assert.strictEqual(client.getStatus().processState, 'Running');
    assert.strictEqual(client.getStatus().procedurePhase, 'ProbeCalibrate');
    adapter.dispose();
  })();
}

async function main() {
  await testStaleCompletedDoesNotFinishNewStart();
  await testStartWhileIdleSendsFullChain();
  await testStartAfterM19CompletedSendsFullChain();
  await testStartDuringM19SendsNoCommands();
  console.log('protocolProcedureAdapter.test.js: PASS');
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
