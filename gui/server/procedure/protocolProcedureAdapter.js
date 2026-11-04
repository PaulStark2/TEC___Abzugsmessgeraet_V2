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
const { analyzeForcePaths } = require('../results/forcePathAnalysis');
const procedureStore = require('./procedureStore');
const calibrationStore = require('../config/calibrationStore');
const machineConfigStore = require('../config/machineConfigStore');
const { syncCalibrationFromController } = require('../config/syncCalibrationFromController');
const { validateProcedureStartPreconditions } = require('./startPreconditions');
const { validateCoreNotRunningForProcedureStart } = require('./coreProcessGuard');
const {
  derivePointProgress,
  estimateIntraPhaseFill,
  trackPhaseHints,
} = require('./procedureProgress');
const { isFinitePose } = require('../coord/workspaceReferenceOffset');

function referenceOffsetFromPoses(plantPose, machinePose) {
  if (
    !plantPose ||
    !machinePose ||
    ![plantPose.x, plantPose.y, plantPose.z, machinePose.x, machinePose.y, machinePose.z].every(
      Number.isFinite
    )
  ) {
    return { x: 0, y: 0, z: 0 };
  }
  return {
    x: plantPose.x - machinePose.x,
    y: plantPose.y - machinePose.y,
    z: plantPose.z - machinePose.z,
  };
}

async function loadWorkspaceFromPlant(plantClient, machineStatus) {
  if (!plantClient || typeof plantClient.getState !== 'function') {
    return null;
  }
  const result = await plantClient.getState();
  if (!result || !result.ok || !result.state || !result.state.limits) {
    return null;
  }
  const status = machineStatus || {};
  const startPose = [status.x, status.y, status.z].every(Number.isFinite)
    ? { x: status.x, y: status.y, z: status.z }
    : null;
  const coreOffset = isFinitePose(result.referenceOffset)
    ? { x: result.referenceOffset.x, y: result.referenceOffset.y, z: result.referenceOffset.z }
    : referenceOffsetFromPoses(result.pose, startPose);
  return {
    limits: result.state.limits,
    referenceOffset: coreOffset,
    startPose,
  };
}

const POLL_MS = 200;

function createProtocolProcedureAdapter(machineClient, options = {}) {
  const plantClient = options.plantClient || null;
  let pollTimer = null;
  let params = null;
  let points = [];
  let observedRows = [];
  let lastPointIndex = null;
  // After CmdStart, ignore residual Completed/Aborted/Idle from a prior run
  // (e.g. manual M19 calib) until StatusReport shows Running for this process.
  let seenRunningSinceStart = false;
  // Last observed Core procedurePhase (for OffsetOnly completion edge).
  let lastProcedurePhase = null;
  let toolOffsetSyncInFlight = false;
  let toolOffsetSyncedForRun = false;
  let phaseHints = {};

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
        // Residual Completed from ProbeCalib / prior Core runs must not mark the
        // procedure UI complete before this start has observed Running.
        if (!seenRunningSinceStart) {
          const prev = procedureStore.getStatus().state;
          if (prev === 'running' || prev === 'paused') {
            return prev;
          }
          return 'idle';
        }
        return 'completed';
      case 'Aborted':
        if (!seenRunningSinceStart) {
          const prev = procedureStore.getStatus().state;
          if (prev === 'running' || prev === 'paused') {
            return prev;
          }
          return 'idle';
        }
        return 'stopped';
      default:
        return procedureStore.getStatus().state === 'idle' ? 'idle' : procedureStore.getStatus().state;
    }
  }

  function buildResultsFromObserved(finalProcessState) {
    const program = programStore.getProgram();
    const rawCurves = forcePathStore.getCurves();
    const pointNames = {};
    const points = program && Array.isArray(program.testPoints) ? program.testPoints : [];
    for (let i = 0; i < points.length; ++i) {
      pointNames[i] = points[i].name || `Punkt ${i + 1}`;
    }
    const analysis = analyzeForcePaths(rawCurves, {
      programName: program ? program.programName : null,
      pointNames,
    });
    return {
      programName: program ? program.programName : null,
      mode: params ? params.mode : null,
      completedAt: new Date().toISOString(),
      backend: 'protocol',
      params: params ? { ...params } : null,
      dataCompleteness: rawCurves.length > 0 ? 'complete' : 'partial',
      notes: [
        'Protocol-Ergebnisse vom Core-ProcedureController. Kraft-Weg-Kurven nur im Prüfmodus (Test) und nur soweit MsgForcePathChunk empfangen wurde. Spitzenkraft = max |Fz| je Kurve; Mittelwert und Stichproben-Standardabweichung über Iterationen je Ein-/Ausgliedern.',
        ...notesForParams(params),
      ],
      processState: finalProcessState,
      procedurePhase: machineClient.getStatus().procedurePhase || null,
      analysis,
      curves: analysis.curves,
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

    // OffsetOnly (Procedure M19): when ProbeCalibrate finishes, Core continues
    // Running into MoveSafePose / later phases. Persist toolOffset only — never
    // rewrite probeCenter (persistProbeCenter=false).
    const currentPhase = machineStatus.procedurePhase || null;
    if (
      connected &&
      seenRunningSinceStart &&
      processState === 'Running' &&
      lastProcedurePhase === 'ProbeCalibrate' &&
      currentPhase &&
      currentPhase !== 'ProbeCalibrate' &&
      !toolOffsetSyncedForRun &&
      !toolOffsetSyncInFlight
    ) {
      toolOffsetSyncedForRun = true;
      toolOffsetSyncInFlight = true;
      syncCalibrationFromController(machineClient, { persistProbeCenter: false })
        .then((result) => {
          if (!result || !result.ok) {
            console.warn(
              '[Procedure] OffsetOnly tool-offset sync failed:',
              (result && result.error) || 'unknown'
            );
            return;
          }
          if (result.toolOffsetChangeAnomaly) {
            console.warn(
              `[Procedure] toolOffset change anomaly: delta=${Number(result.toolOffsetDeltaMm).toFixed(3)} mm ` +
                `(tol=${Number(result.toolOffsetChangeToleranceMm).toFixed(3)} mm)`
            );
            procedureStore.patch({
              toolOffsetAnomaly: (result.calibration && result.calibration.toolOffsetAnomaly) || null,
            });
          } else {
            procedureStore.patch({ toolOffsetAnomaly: null });
          }
        })
        .catch((err) => {
          console.warn('[Procedure] OffsetOnly tool-offset sync error:', err && err.message);
        })
        .finally(() => {
          toolOffsetSyncInFlight = false;
        });
    }
    lastProcedurePhase = currentPhase;

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

    const nextProcedureState = mapGuiState(processState, connected);
    // Only treat Core Completed as procedure completion after this start saw Running.
    const progressProcessState =
      processState === 'Completed' && !seenRunningSinceStart ? 'Idle' : processState;

    if (seenRunningSinceStart && processState === 'Running') {
      phaseHints = trackPhaseHints(phaseHints, machineStatus.procedurePhase);
    }

    const totalIterations = params && params.iterations ? params.iterations : 1;
    const currentIteration = Number.isFinite(Number(machineStatus.currentIteration))
      ? Number(machineStatus.currentIteration)
      : 0;
    const targetForce = params && Number.isFinite(Number(params.insertionForce))
      ? Number(params.insertionForce)
      : 0;
    const intraFill = estimateIntraPhaseFill({
      procedurePhase: machineStatus.procedurePhase,
      machine: machineStatus,
      targetForce,
      motionState: machineStatus.motionState,
    });
    const derived = derivePointProgress({
      totalPoints,
      index0Based: currentPointIndex,
      processState: progressProcessState,
      procedureState: nextProcedureState,
      iterationIndex: currentIteration,
      totalIterations,
      procedurePhase: machineStatus.procedurePhase,
      mode: params && params.mode === 'aging' ? 'aging' : 'test',
      phaseHints,
      intraFill,
    });

    let nextState = mapGuiState(processState, connected);

    // Controller hold at the bore center: process stays Running, machine is Paused.
    if (connected && processState === 'Running' && machineStatus.machineState === 'Paused') {
      nextState = 'paused';
    }

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
        currentIteration,
        totalIterations,
        currentPointIndex: derived.currentPointIndex,
        pointsDone: derived.pointsDone,
        totalPoints: derived.totalPoints,
        percent: derived.percent,
        isComplete: derived.isComplete,
        procedurePhase: machineStatus.procedurePhase || null,
        phasePrev: derived.phasePrev,
        phaseCurrent: derived.phaseCurrent,
        phaseNext: derived.phaseNext,
        phasePercent: derived.phasePercent,
        milestoneIndex: derived.milestoneIndex,
        milestoneCount: derived.milestoneCount,
        milestoneLabels: derived.milestoneLabels,
        scope: derived.scope,
        machineState: machineStatus.machineState || null,
        motionState: machineStatus.motionState || null,
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
      resultsStore.pushResults(buildResultsFromObserved('Completed'));
      procedureStore.patch({
        state: 'completed',
        completedAt: new Date().toISOString(),
        error: null,
        progress: {
          currentIteration: totalIterations,
          totalIterations,
          currentPointIndex: totalPoints,
          pointsDone: totalPoints,
          totalPoints,
          percent: 100,
          isComplete: true,
          phasePrev: 'Zur Endpose',
          phaseCurrent: 'Abgeschlossen',
          phaseNext: '—',
          phasePercent: 100,
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
      resultsStore.pushResults(buildResultsFromObserved('Aborted'));
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

    const machineCfg = machineConfigStore.load();
    const rangeN = Number(machineCfg.force_range_N);
    const forceLimitN = Number.isFinite(rangeN) && rangeN > 0 ? rangeN : 30;
    if (!Number.isFinite(insertionForce) || !(insertionForce > 0)) {
      return { ok: false, error: 'insertionForce must be a positive number' };
    }
    if (insertionForce > forceLimitN) {
      return { ok: false, error: `insertionForce must be <= ${forceLimitN} N` };
    }
    if (!Number.isInteger(iterations) || iterations < 1) {
      return { ok: false, error: 'iterations must be an integer >= 1' };
    }

    const calib = calibrationStore.load();
    const workspace = await loadWorkspaceFromPlant(
      plantClient,
      typeof machineClient.getStatus === 'function' ? machineClient.getStatus() : {}
    );
    const preflight = validateProcedureStartPreconditions({
      calibration: calib,
      machineConfig: machineCfg,
      points: selection.points,
      workspace: workspace || undefined,
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
    phaseHints = {};
    // Keep last-run history; only clear live force-path buffer for the new run.
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

    const mcs = calib.machineCoordinateSystem;
    if (mcs && mcs.valid && typeof machineClient.restoreMcs === 'function') {
      const home = mcs.homePosition || { x: 0, y: 0, z: 0 };
      const restored = await runStartCommand('CmdRestoreMcs', () =>
        machineClient.restoreMcs(home.x, home.y, home.z)
      );
      if (!restored || !restored.ok) {
        const detail = (restored && restored.error) || 'CmdRestoreMcs failed';
        return {
          ok: false,
          error: `CmdRestoreMcs: ${detail}`,
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
    lastProcedurePhase = null;
    toolOffsetSyncedForRun = false;
    toolOffsetSyncInFlight = false;
    phaseHints = {};
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
        phasePrev: '—',
        phaseCurrent: '—',
        phaseNext: '—',
        phasePercent: 0,
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


  async function applyProcedureDecision(action) {
    if (!isLive()) {
      return { ok: false, error: 'Machine connection unavailable' };
    }
    if (typeof machineClient.sendProcedureDecision !== 'function') {
      return { ok: false, error: 'sendProcedureDecision not supported' };
    }
    const result = await machineClient.sendProcedureDecision(action);
    if (!result || !result.ok) {
      return { ok: false, error: (result && result.error) || 'procedure decision failed' };
    }
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
    const calib = calibrationStore.load();
    procedureStore.patch({
      backend: 'protocol',
      capabilities: capabilities(),
      toolOffsetAnomaly: calib.toolOffsetAnomaly || null,
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

  function resetUi() {
    const current = procedureStore.getStatus();
    if (current.state === 'running' || current.state === 'paused') {
      return {
        ok: false,
        error: 'Prozedur läuft noch — bitte zuerst stoppen.',
        status: current,
      };
    }
    clearPoll();
    const keptParams = current.params;
    const keptCaps = current.capabilities;
    procedureStore.reset();
    procedureStore.patch({
      backend: 'protocol',
      capabilities: keptCaps || capabilities(),
      params: keptParams,
      state: 'idle',
      processState: null,
      connected: isLive(),
      error: null,
      startedAt: null,
      completedAt: null,
      progress: {
        currentIteration: 0,
        totalIterations: keptParams && keptParams.iterations ? keptParams.iterations : 0,
        currentPointIndex: 0,
        pointsDone: 0,
        totalPoints: 0,
        percent: 0,
        isComplete: false,
        phasePrev: '—',
        phaseCurrent: '—',
        phaseNext: '—',
        phasePercent: 0,
      },
      notes: [],
    });
    phaseHints = {};
    seenRunningSinceStart = false;
    lastProcedurePhase = null;
    lastPointIndex = null;
    return { ok: true, status: procedureStore.getStatus() };
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
    resetUi,
    applyProcedureDecision,
    getStatus,
    dispose,
  };
}

module.exports = {
  createProtocolProcedureAdapter,
};
