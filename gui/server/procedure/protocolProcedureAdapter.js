'use strict';

/**
 * Protocol G8 adapter — thin HMI orchestrator over Core TestExecutor.
 *
 * Path: GUI → ProtocolMachineClient → M12 → simulation_bridge → TestExecutor
 *
 * Supported by Core today:
 * - Load selected points (CmdLoadProgram)
 * - TestConfiguration (mode, target force, iterations)
 * - Plate origin (CmdSetPlateOrigin)
 * - Start / pause / resume / stop process
 * - Observe processState / procedurePhase / currentPointIndex from StatusReport
 * - Force-path chunks in Test mode
 */

const selectionStore = require('../program/selectionStore');
const programStore = require('../program/programStore');
const resultsStore = require('../results/resultsStore');
const forcePathStore = require('../results/forcePathStore');
const procedureStore = require('./procedureStore');
const calibrationStore = require('../config/calibrationStore');
const machineConfigStore = require('../config/machineConfigStore');
const { validateProcedureStartPreconditions } = require('./startPreconditions');
const { validateCoreNotRunningForProcedureStart } = require('./coreProcessGuard');
const { derivePointProgress } = require('./procedureProgress');

const POLL_MS = 200;

function createProtocolProcedureAdapter(machineClient) {
  let pollTimer = null;
  let params = null;
  let points = [];
  let observedRows = [];
  let lastPointIndex = null;
  // After CmdStart, ignore residual Completed/Aborted/Idle from a prior run
  // (e.g. manual M19 calib) until StatusReport shows Running for this process.
  let seenRunningSinceStart = false;

  function capabilities() {
    return {
      iterationsSupported: true,
      modeSupported: true,
      insertionForceSupported: true,
      simulatedForces: true,
      procedurePhaseSupported: true,
      forcePathSupported: true,
    };
  }

  function notesForParams(p) {
    const list = [
      'Protocol-Backend: Prüf-/Alterungsablauf läuft im Core-ProcedureController.',
      'Eingliederkraft → Phase 1/2 (target_force_N). Iterationen → nur Phase 3. Phase 2 endet bei Krafttoleranz (MachineConfig).',
    ];
    if (p && p.mode === 'aging') {
      list.push('Alterung: Phase-3-Zyklen ohne Kraft-Weg-Aufzeichnung, nur Safety-Überwachung.');
    }
    if (p && p.mode === 'test') {
      list.push('Prüfung: Phase-3-Zyklen mit Kraft-Weg-Kurven (MsgForcePathChunk).');
    }
    return list;
  }

  function isLive() {
    if (!machineClient || typeof machineClient.getStatus !== 'function') {
      return false;
    }
    const s = machineClient.getStatus();
    // ProtocolMachineClient sets connected=false when offline / not sessionReady.
    if (Object.prototype.hasOwnProperty.call(s, 'connected')) {
      return Boolean(s.connected);
    }
    return true;
  }

  function clearPoll() {
    if (pollTimer) {
      clearInterval(pollTimer);
      pollTimer = null;
    }
  }

  function mapGuiState(processState, connected) {
    if (!connected) {
      return 'unavailable';
    }
    switch (processState) {
      case 'Running':
        return procedureStore.getStatus().state === 'paused' ? 'paused' : 'running';
      case 'Completed':
        return 'completed';
      case 'Aborted':
        return 'stopped';
      default:
        return procedureStore.getStatus().state === 'idle' ? 'idle' : procedureStore.getStatus().state;
    }
  }

  function buildResultsFromObserved(finalProcessState) {
    const program = programStore.getProgram();
    return {
      programName: program ? program.programName : null,
      mode: params ? params.mode : null,
      completedAt: new Date().toISOString(),
      backend: 'protocol',
      params: params ? { ...params } : null,
      dataCompleteness: forcePathStore.getCurves().length > 0 ? 'complete' : 'partial',
      notes: [
        'Protocol-Ergebnisse vom Core-ProcedureController. Kraft-Weg-Kurven nur im Prüfmodus und nur soweit MsgForcePathChunk empfangen wurde.',
        ...notesForParams(params),
      ],
      processState: finalProcessState,
      procedurePhase: machineClient.getStatus().procedurePhase || null,
      curves: forcePathStore.getCurves(),
      rows: observedRows.map((row) => ({ ...row })),
    };
  }

  function observeStatus() {
    const machineStatus = machineClient.getStatus();
    const connected = isLive();
    const rawProcessState = machineStatus.processState || 'Idle';
    const prev = procedureStore.getStatus();
    const activeRun = prev.state === 'running' || prev.state === 'paused';

    if (rawProcessState === 'Running') {
      seenRunningSinceStart = true;
    }

    // Stale terminal/idle from a previous Core process (e.g. CmdStartProbeCalib)
    // must not complete a freshly started procedure before Running is observed.
    let processState = rawProcessState;
    if (
      activeRun &&
      !seenRunningSinceStart &&
      (rawProcessState === 'Completed' ||
        rawProcessState === 'Aborted' ||
        rawProcessState === 'Idle')
    ) {
      processState = 'Running';
    }

    const totalPoints = points.length || Number(machineStatus.programPointCount) || 0;
    let currentPointIndex =
      machineStatus.currentPointIndex === null || machineStatus.currentPointIndex === undefined
        ? lastPointIndex === null
          ? 0
          : lastPointIndex
        : Number(machineStatus.currentPointIndex);

    if (
      connected &&
      processState === 'Running' &&
      seenRunningSinceStart &&
      machineStatus.currentPointIndex !== null &&
      machineStatus.currentPointIndex !== undefined &&
      machineStatus.currentPointIndex !== lastPointIndex
    ) {
      const idx = Number(machineStatus.currentPointIndex);
      const point = points[idx] || null;
      observedRows.push({
        sequenceNumber: point ? point.sequenceNumber : idx + 1,
        name: point ? point.name : `Punkt ${idx + 1}`,
        x: point ? point.x : machineStatus.x,
        y: point ? point.y : machineStatus.y,
        z: point ? point.z : machineStatus.z,
        iteration: 1,
        fx: Number.isFinite(Number(machineStatus.fx)) ? Number(machineStatus.fx) : null,
        fy: Number.isFinite(Number(machineStatus.fy)) ? Number(machineStatus.fy) : null,
        fz: Number.isFinite(Number(machineStatus.fz)) ? Number(machineStatus.fz) : null,
        pass: null,
        source: 'controller-status',
      });
      lastPointIndex = idx;
      currentPointIndex = idx;
    }

    const nextProcedureState =
      processState === 'Completed' && seenRunningSinceStart
        ? 'completed'
        : mapGuiState(processState, connected);
    const derived = derivePointProgress({
      totalPoints,
      index0Based: currentPointIndex,
      processState,
      procedureState: nextProcedureState,
    });

    let nextState = mapGuiState(processState, connected);

    // Preserve paused UI state while Core process stays Running and Machine is Paused.
    if (prev.state === 'paused' && connected && processState === 'Running') {
      nextState = 'paused';
    }

    // Offline while a procedure was active → unavailable (no local motion fiction).
    if (!connected && (prev.state === 'running' || prev.state === 'paused')) {
      nextState = 'unavailable';
    }

    procedureStore.patch({
      backend: 'protocol',
      capabilities: capabilities(),
      notes: notesForParams(params),
      params,
      connected,
      processState,
      state: nextState,
      progress: {
        currentIteration: 0,
        totalIterations: params && params.iterations ? params.iterations : 1,
        currentPointIndex: derived.currentPointIndex,
        pointsDone: derived.pointsDone,
        totalPoints: derived.totalPoints,
        percent: derived.percent,
        isComplete: derived.isComplete,
        procedurePhase: machineStatus.procedurePhase || null,
        machineState: machineStatus.machineState || null,
        faultReason: machineStatus.faultReason || null,
        spreaderState: machineStatus.spreaderState || null,
        isHomed: machineStatus.isHomed === true,
        communicationWatchdogEnabled: machineStatus.communicationWatchdogEnabled === true,
        forceSampleFresh: machineStatus.forceSampleFresh === true,
      },
      error: connected
        ? prev.error
        : 'Controller/Simulation nicht erreichbar — keine Prozedursteuerung möglich',
    });

    // Completion only on observed Running → Completed for this start.
    if (
      connected &&
      processState === 'Completed' &&
      seenRunningSinceStart &&
      prev.state !== 'completed'
    ) {
      clearPoll();
      resultsStore.setResults(buildResultsFromObserved('Completed'));
      procedureStore.patch({
        state: 'completed',
        completedAt: new Date().toISOString(),
        error: null,
        progress: {
          currentIteration: params && params.iterations ? params.iterations : 1,
          totalIterations: params && params.iterations ? params.iterations : 1,
          currentPointIndex: totalPoints,
          pointsDone: totalPoints,
          totalPoints,
          percent: 100,
          isComplete: true,
        },
      });
    }

    if (
      connected &&
      processState === 'Aborted' &&
      seenRunningSinceStart &&
      prev.state !== 'stopped' &&
      prev.state !== 'idle'
    ) {
      clearPoll();
      resultsStore.setResults(buildResultsFromObserved('Aborted'));
      procedureStore.patch({
        state: 'stopped',
        completedAt: new Date().toISOString(),
      });
    }
  }

  function startPoll() {
    clearPoll();
    pollTimer = setInterval(() => {
      try {
        observeStatus();
      } catch (err) {
        procedureStore.patch({
          state: 'error',
          error: err.message || String(err),
        });
        clearPoll();
      }
    }, POLL_MS);
  }

  function snapshotProcess() {
    const s = typeof machineClient.getStatus === 'function' ? machineClient.getStatus() : {};
    return {
      processState: s.processState,
      procedurePhase: s.procedurePhase,
      machineState: s.machineState,
      isHomed: s.isHomed,
      plateOriginValid: s.plateOriginValid,
      probeCenterValid: s.probeCenterValid,
      programPointCount: s.programPointCount,
    };
  }

  function ackCodeName(result) {
    if (!result) return 'none';
    if (Object.prototype.hasOwnProperty.call(result, 'resultCode')) {
      const map = { 0: 'Ok', 1: 'Rejected', 2: 'InvalidState', 3: 'ProgramEmpty', 4: 'FaultLatched' };
      return map[result.resultCode] || `code=${result.resultCode}`;
    }
    if (result.ok) return 'Ok';
    if (result.error && String(result.error).includes('Invalid state')) return 'InvalidState';
    if (result.error && String(result.error).includes('Program empty')) return 'ProgramEmpty';
    return result.ok ? 'Ok' : 'Error';
  }

  async function runStartCommand(label, fn) {
    const before = snapshotProcess();
    console.log(`[ProcedureStart] sending ${label} | processState=${before.processState} phase=${before.procedurePhase}`);
    const result = await fn();
    const after = snapshotProcess();
    const code = ackCodeName(result);
    console.log(
      `[ProcedureStart] received ${label} -> ${code} | processState=${after.processState} ` +
        `phase=${after.procedurePhase} isHomed=${after.isHomed} plateValid=${after.plateOriginValid} ` +
        `probeValid=${after.probeCenterValid} programPoints=${after.programPointCount}`
    );
    if (!result || !result.ok) {
      console.error(`[ProcedureStart] FAIL at ${label}:`, (result && result.error) || 'unknown', after);
    }
    return result;
  }

  async function start(body) {
    const selection = selectionStore.getSelection();
    if (!selection || !selection.confirmed || !selection.points || selection.points.length === 0) {
      return { ok: false, error: 'Confirmed point selection required' };
    }

    if (!isLive()) {
      procedureStore.patch({
        state: 'unavailable',
        backend: 'protocol',
        connected: false,
        capabilities: capabilities(),
        notes: notesForParams(body || {}),
        error: 'Controller/Simulation nicht erreichbar',
      });
      return { ok: false, error: 'Machine connection unavailable' };
    }

    if (typeof machineClient.loadProgram !== 'function' || typeof machineClient.start !== 'function') {
      return {
        ok: false,
        error: 'Protocol client missing loadProgram/start — Controller-Anbindung unvollständig',
      };
    }

    const mode = body && body.mode === 'aging' ? 'aging' : 'test';
    const insertionForce = Number(body && body.insertionForce);
    const iterations = Number(body && body.iterations);

    if (!Number.isFinite(insertionForce) || !(insertionForce > 0)) {
      return { ok: false, error: 'insertionForce must be a positive number' };
    }
    if (insertionForce > 50) {
      return { ok: false, error: 'insertionForce must be <= 50 N' };
    }
    if (!Number.isInteger(iterations) || iterations < 1) {
      return { ok: false, error: 'iterations must be an integer >= 1' };
    }

    const calib = calibrationStore.load();
    const machineCfg = machineConfigStore.load();
    const preflight = validateProcedureStartPreconditions({
      calibration: calib,
      machineConfig: machineCfg,
      points: selection.points,
    });
    if (!preflight.ok) {
      return { ok: false, error: preflight.error };
    }

    const current = procedureStore.getStatus();
    if (current.state === 'running' || current.state === 'paused') {
      return { ok: false, error: 'Procedure already active' };
    }

    // Core process may be Running (e.g. manual M19) while the GUI procedure store is still idle.
    // Refuse before any CmdSet* / CmdLoad / CmdStart — do not auto-stop the running process.
    const coreBusy = validateCoreNotRunningForProcedureStart(snapshotProcess());
    if (!coreBusy.ok) {
      console.log('[ProcedureStart] blocked: core process still Running', {
        procedurePhase: coreBusy.procedurePhase,
        kind: coreBusy.kind,
      });
      return {
        ok: false,
        error: coreBusy.error,
        code: coreBusy.code,
        status: procedureStore.getStatus(),
      };
    }

    points = selection.points.map((p) => ({ ...p }));
    params = { mode, insertionForce, iterations };
    observedRows = [];
    lastPointIndex = null;
    resultsStore.clear();
    forcePathStore.clear();

    console.log('[ProcedureStart] begin chain', {
      mode,
      insertionForce,
      iterations,
      pointCount: points.length,
      maxPointZ: Math.max(...points.map((p) => Number(p.z)).filter(Number.isFinite)),
      safe_z_buffer: machineCfg.safe_z_buffer,
      plateOrigin: calib.plateOrigin,
      probeCenter: calib.probeCenter,
      snapshot: snapshotProcess(),
    });

    if (typeof machineClient.setMachineConfig === 'function') {
      const cfgPush = await runStartCommand('CmdSetMachineConfig', () =>
        machineClient.setMachineConfig(machineCfg)
      );
      if (!cfgPush || !cfgPush.ok) {
        const detail = (cfgPush && cfgPush.error) || 'CmdSetMachineConfig failed';
        return {
          ok: false,
          error: `CmdSetMachineConfig: ${detail}`,
          status: procedureStore.getStatus(),
        };
      }
    }

    if (typeof machineClient.setPlateOrigin === 'function') {
      const origin = await runStartCommand('CmdSetPlateOrigin', () =>
        machineClient.setPlateOrigin(calib.plateOrigin.x, calib.plateOrigin.y, calib.plateOrigin.z)
      );
      if (!origin || !origin.ok) {
        const detail = (origin && origin.error) || 'CmdSetPlateOrigin failed';
        return {
          ok: false,
          error: `CmdSetPlateOrigin: ${detail}`,
          status: procedureStore.getStatus(),
        };
      }
    }

    if (typeof machineClient.setProbeCenter === 'function') {
      const probe = await runStartCommand('CmdSetProbeCenter', () =>
        machineClient.setProbeCenter(calib.probeCenter.x, calib.probeCenter.y, calib.probeCenter.z)
      );
      if (!probe || !probe.ok) {
        const detail = (probe && probe.error) || 'CmdSetProbeCenter failed';
        return {
          ok: false,
          error: `CmdSetProbeCenter: ${detail}`,
          status: procedureStore.getStatus(),
        };
      }
    }

    if (typeof machineClient.setTestConfiguration === 'function') {
      const cfg = await runStartCommand('CmdSetTestConfiguration', () =>
        machineClient.setTestConfiguration({
          mode: mode === 'aging' ? 1 : 0,
          target_force_N: insertionForce,
          iterations,
          active_point_index: 0,
        })
      );
      if (!cfg || !cfg.ok) {
        const detail = (cfg && cfg.error) || 'CmdSetTestConfiguration failed';
        return {
          ok: false,
          error: `CmdSetTestConfiguration: ${detail}`,
          status: procedureStore.getStatus(),
        };
      }
    }

    const load = await runStartCommand('CmdLoadProgram', () =>
      machineClient.loadProgram(points.map((p) => ({ x: p.x, y: p.y, z: p.z })))
    );
    if (!load || !load.ok) {
      const detail = (load && load.error) || 'CmdLoadProgram failed';
      return {
        ok: false,
        error: `CmdLoadProgram: ${detail}`,
        status: procedureStore.getStatus(),
      };
    }

    const started = await runStartCommand('CmdStart', () => machineClient.start());
    if (!started || !started.ok) {
      const detail = (started && started.error) || 'CmdStart failed';
      return {
        ok: false,
        error: `CmdStart: ${detail}`,
        status: procedureStore.getStatus(),
      };
    }

    console.log('[ProcedureStart] chain complete Ok', snapshotProcess());

    seenRunningSinceStart = false;
    procedureStore.patch({
      state: 'running',
      backend: 'protocol',
      params,
      capabilities: capabilities(),
      notes: notesForParams(params),
      processState: 'Running',
      connected: true,
      error: null,
      startedAt: new Date().toISOString(),
      completedAt: null,
      progress: {
        currentIteration: 0,
        totalIterations: iterations,
        currentPointIndex: 0,
        pointsDone: 0,
        totalPoints: points.length,
        percent: 0,
        isComplete: false,
      },
    });

    startPoll();
    // May still see stale Cached Completed from prior M19/process — must not latch.
    observeStatus();
    return { ok: true, status: procedureStore.getStatus() };
  }

  async function pause() {
    if (!isLive()) {
      return { ok: false, error: 'Machine connection unavailable' };
    }
    const result = await machineClient.pause();
    if (!result || !result.ok) {
      return { ok: false, error: (result && result.error) || 'pause failed' };
    }
    procedureStore.patch({ state: 'paused' });
    observeStatus();
    return { ok: true, status: procedureStore.getStatus() };
  }

  async function resume() {
    if (!isLive()) {
      return { ok: false, error: 'Machine connection unavailable' };
    }
    const result = await machineClient.resume();
    if (!result || !result.ok) {
      return { ok: false, error: (result && result.error) || 'resume failed' };
    }
    procedureStore.patch({ state: 'running' });
    observeStatus();
    return { ok: true, status: procedureStore.getStatus() };
  }

  async function stop() {
    const current = procedureStore.getStatus();
    if (current.state !== 'running' && current.state !== 'paused' && current.state !== 'unavailable') {
      return { ok: true, status: current };
    }
    if (!isLive()) {
      clearPoll();
      procedureStore.patch({
        state: 'unavailable',
        connected: false,
        error: 'Controller/Simulation nicht erreichbar — Stop konnte nicht an den Controller gesendet werden',
      });
      return { ok: false, error: 'Machine connection unavailable', status: procedureStore.getStatus() };
    }
    const result = await machineClient.stop();
    if (!result || !result.ok) {
      return { ok: false, error: (result && result.error) || 'stop failed' };
    }
    observeStatus();
    return { ok: true, status: procedureStore.getStatus() };
  }

  function getStatus() {
    // Protocol adapter always exposes Core TestConfiguration capabilities.
    procedureStore.patch({
      backend: 'protocol',
      capabilities: capabilities(),
    });
    // Refresh live connection flag on every poll from UI.
    if (params || procedureStore.getStatus().backend === 'protocol') {
      try {
        observeStatus();
      } catch {
        // ignore transient read errors
      }
    }
    return procedureStore.getStatus();
  }

  function dispose() {
    clearPoll();
  }

  return {
    kind: 'protocol',
    start,
    pause,
    resume,
    stop,
    getStatus,
    dispose,
  };
}

module.exports = {
  createProtocolProcedureAdapter,
};
