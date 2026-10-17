'use strict';

/**
 * Regression: stale Cached Completed (e.g. after M19 calib) must not finish a
 * freshly started procedure. Completion requires Running → Completed.
 *
 * Also: start while Core process Running (M19) must not send the start chain.
 */

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const programStore = require('../program/programStore');
const selectionStore = require('../program/selectionStore');
const resultsStore = require('../results/resultsStore');
const procedureStore = require('./procedureStore');
const calibrationStore = require('../config/calibrationStore');
const machineConfigStore = require('../config/machineConfigStore');
const { createProtocolProcedureAdapter } = require('./protocolProcedureAdapter');

const cfgFile = path.join(__dirname, '..', 'data', 'machine-config.json');

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
  let machineConfig = {
    tool_offset_x: 0,
    tool_offset_y: 0,
    tool_offset_z: 0,
    probe_center_x: 40,
    probe_center_y: 40,
    probe_center_z: 10,
  };

  return {
    sent,
    getStatus: () => ({ ...status }),
    setStatus(partial) {
      status = { ...status, ...partial };
    },
    setMachineConfigPayload(partial) {
      machineConfig = { ...machineConfig, ...partial };
    },
    async getMachineConfig() {
      sent.push('CmdGetMachineConfig');
      return { ok: true, config: { ...machineConfig } };
    },
    async setMachineConfig() {
      sent.push('CmdSetMachineConfig');
      return { ok: true, resultCode: 0 };
    },
    async restoreMcs() {
      sent.push('CmdRestoreMcs');
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
    const previousCfg = fs.existsSync(cfgFile) ? fs.readFileSync(cfgFile, 'utf8') : null;
    calibrationStore.save({
      ...previousCalib,
      machineCoordinateSystem: {
        valid: true,
        origin: 'positiveMaxSwitches',
        homePosition: { x: -2, y: -2, z: -2 },
      },
      plateOrigin: { x: -45, y: -43, z: -33 },
      plateOriginValid: true,
      probeCenter: { x: -60, y: -60, z: -40 },
      probeCenterValid: true,
      toolOffset: { x: 0, y: 0, z: 0 },
    });
    machineConfigStore.save({
      ...machineConfigStore.load(),
      probe_offset_reference_x: 40,
      probe_offset_reference_y: 40,
      probe_offset_reference_z: 10,
      tool_offset_x: 0,
      tool_offset_y: 0,
      tool_offset_z: 0,
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
      if (previousCfg !== null) {
        fs.writeFileSync(cfgFile, previousCfg, 'utf8');
      } else if (fs.existsSync(cfgFile)) {
        fs.unlinkSync(cfgFile);
      }
      programStore.clearProgram();
      selectionStore.clear();
      resultsStore.clear();
      procedureStore.reset();
    }
  };
}

async function testIdlePollIgnoresStaleCoreCompleted() {
  await withPreparedStores(async () => {
    const client = createMockClient({
      processState: 'Completed',
      procedurePhase: 'Completed',
    });
    const adapter = createProtocolProcedureAdapter(client);
    const status = adapter.getStatus();
    assert.strictEqual(
      status.state,
      'idle',
      'Core Completed from prior ProbeCalib must not show procedure as completed before start'
    );
    assert.strictEqual(status.progress.isComplete, false);
    adapter.dispose();
  })();
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
      'CmdRestoreMcs',
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

async function testOffsetOnlyCompletionPersistsToolOnly() {
  await withPreparedStores(async () => {
    const client = createMockClient({ processState: 'Idle', procedurePhase: 'Idle' });
    client.setMachineConfigPayload({
      tool_offset_x: -0.2,
      tool_offset_y: 0.1,
      tool_offset_z: -0.1,
      probe_center_x: 100.2,
      probe_center_y: 119.9,
      probe_center_z: 40.1,
    });
    const adapter = createProtocolProcedureAdapter(client);
    const started = await adapter.start({ mode: 'test', insertionForce: 10, iterations: 1 });
    assert.strictEqual(started.ok, true);

    client.setStatus({ processState: 'Running', procedurePhase: 'ProbeCalibrate' });
    adapter.getStatus();

    client.setStatus({ processState: 'Running', procedurePhase: 'MoveSafePose' });
    adapter.getStatus();

    // Allow async tool-only sync to finish.
    await new Promise((r) => setTimeout(r, 50));

    assert.ok(client.sent.includes('CmdGetMachineConfig'), 'OffsetOnly must GetMachineConfig');
    const calib = calibrationStore.load();
    const cfg = machineConfigStore.load();
    assert.strictEqual(calib.probeCenter.x, -60);
    assert.strictEqual(calib.probeCenter.y, -60);
    assert.strictEqual(calib.probeCenter.z, -40);
    assert.ok(Math.abs(calib.toolOffset.x - -0.2) < 1e-9);
    assert.ok(Math.abs(calib.toolOffset.y - 0.1) < 1e-9);
    assert.ok(Math.abs(calib.toolOffset.z - -0.1) < 1e-9);
    assert.ok(Math.abs(cfg.tool_offset_x - -0.2) < 1e-9);
    // Legacy reference still present but unused for reconstruction.
    assert.strictEqual(cfg.probe_offset_reference_x, 40);

    adapter.dispose();
  })();
}

async function main() {
  await testIdlePollIgnoresStaleCoreCompleted();
  await testStaleCompletedDoesNotFinishNewStart();
  await testStartWhileIdleSendsFullChain();
  await testStartAfterM19CompletedSendsFullChain();
  await testStartDuringM19SendsNoCommands();
  await testOffsetOnlyCompletionPersistsToolOnly();
  console.log('protocolProcedureAdapter.test.js: PASS');
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
