'use strict';

/**
 * Mock-only G8 procedure simulator (HMI offline path).
 *
 * Explicitly NOT a Core TestExecutor and must never be used for MACHINE_CLIENT=protocol.
 * Simulates iterations, point sequence, force samples, pause/resume/stop, and results.
 */

const programStore = require('../program/programStore');
const selectionStore = require('../program/selectionStore');
const resultsStore = require('../results/resultsStore');
const procedureStore = require('./procedureStore');

const STEP_MS = 150;

function clamp(n, min, max) {
  return Math.min(max, Math.max(min, n));
}

function createMockProcedureAdapter(machineClient) {
  let timer = null;
  let points = [];
  let rowAccumulator = [];
  let iterationIndex = 0;
  let pointIndex = 0;
  let params = null;
  let phase = 'idle'; // idle | moving | sampling

  function clearTimer() {
    if (timer) {
      clearTimeout(timer);
      timer = null;
    }
  }

  function capabilities() {
    return {
      iterationsSupported: true,
      modeSupported: true,
      insertionForceSupported: true,
      simulatedForces: true,
    };
  }

  function notes() {
    return [
      'Mock-Backend: komplette Prozedur wird in der GUI-Schicht simuliert (kein Core-TestExecutor).',
      'Modus, Eingliederungskraft und Iterationen gelten nur für diesen Mock-Pfad.',
    ];
  }

  function updateProgress(extra = {}) {
    const totalIterations = params ? params.iterations : 0;
    const totalPoints = points.length;
    const donePoints = iterationIndex * totalPoints + pointIndex;
    const totalSteps = Math.max(1, totalIterations * totalPoints);
    const percent = clamp(Math.round((donePoints / totalSteps) * 100), 0, 100);
    procedureStore.patch({
      backend: 'mock',
      capabilities: capabilities(),
      notes: notes(),
      params,
      progress: {
        currentIteration: Math.min(iterationIndex + 1, totalIterations || 0),
        totalIterations,
        currentPointIndex: pointIndex,
        totalPoints,
        percent: procedureStore.getStatus().state === 'completed' ? 100 : percent,
      },
      ...extra,
    });
  }

  function sampleForces(point, iteration) {
    const base = Number(params.insertionForce) || 0;
    const jitter = ((point.sequenceNumber + iteration) % 7) * 0.1;
    const fx = Number((base * 0.05 + jitter).toFixed(3));
    const fy = Number((base * 0.04 + jitter * 0.5).toFixed(3));
    const fz = Number((base + jitter).toFixed(3));
    const pass = Math.abs(fz) <= base * 1.25 + 1;
    return { fx, fy, fz, pass };
  }

  function finishCompleted() {
    clearTimer();
    phase = 'idle';
    const program = programStore.getProgram();
    resultsStore.setResults({
      programName: program ? program.programName : null,
      mode: params.mode,
      completedAt: new Date().toISOString(),
      backend: 'mock',
      params: { ...params },
      dataCompleteness: 'full',
      notes: [
        'Mock-Ergebnisse: Kräfte und Pass/Fail sind simuliert, nicht von Sensoren gemessen.',
      ],
      rows: rowAccumulator,
    });
    updateProgress({
      state: 'completed',
      processState: 'Completed',
      completedAt: new Date().toISOString(),
      error: null,
      progress: {
        currentIteration: params.iterations,
        totalIterations: params.iterations,
        currentPointIndex: points.length,
        totalPoints: points.length,
        percent: 100,
      },
    });
  }

  function finishStopped(message) {
    clearTimer();
    phase = 'idle';
    updateProgress({
      state: 'stopped',
      processState: 'Aborted',
      completedAt: new Date().toISOString(),
      error: message || null,
    });
  }

  function scheduleNext(fn) {
    clearTimer();
    timer = setTimeout(() => {
      timer = null;
      fn();
    }, STEP_MS);
  }

  function processStep() {
    const status = procedureStore.getStatus();
    if (status.state !== 'running') {
      return;
    }

    if (iterationIndex >= params.iterations) {
      finishCompleted();
      return;
    }

    if (pointIndex >= points.length) {
      iterationIndex += 1;
      pointIndex = 0;
      if (iterationIndex >= params.iterations) {
        finishCompleted();
        return;
      }
    }

    const point = points[pointIndex];
    updateProgress({ state: 'running', processState: 'Running', error: null });

    if (phase === 'idle' || phase === 'moving') {
      phase = 'moving';
      if (machineClient && typeof machineClient.moveTo === 'function') {
        const moveResult = machineClient.moveTo(point.x, point.y, point.z);
        if (moveResult && moveResult.ok === false) {
          finishStopped(moveResult.error || 'moveTo failed during mock procedure');
          return;
        }
      }
      phase = 'sampling';
      scheduleNext(() => processStep());
      return;
    }

    // phase === 'sampling': record mock forces once, then advance
    const forces = sampleForces(point, iterationIndex + 1);
    rowAccumulator.push({
      sequenceNumber: point.sequenceNumber,
      name: point.name,
      x: point.x,
      y: point.y,
      z: point.z,
      iteration: iterationIndex + 1,
      fx: forces.fx,
      fy: forces.fy,
      fz: forces.fz,
      pass: forces.pass,
      source: 'mock-simulated',
    });
    pointIndex += 1;
    phase = 'idle';
    updateProgress();
    scheduleNext(() => processStep());
  }

  function start(body) {
    const selection = selectionStore.getSelection();
    if (!selection || !selection.confirmed || !selection.points || selection.points.length === 0) {
      return { ok: false, error: 'Confirmed point selection required' };
    }

    const mode = body && body.mode === 'aging' ? 'aging' : 'test';
    const insertionForce = Number(body && body.insertionForce);
    const iterations = Number(body && body.iterations);

    if (!Number.isFinite(insertionForce) || insertionForce < 0) {
      return { ok: false, error: 'insertionForce must be a non-negative number' };
    }
    if (!Number.isInteger(iterations) || iterations < 1) {
      return { ok: false, error: 'iterations must be an integer >= 1' };
    }

    const current = procedureStore.getStatus();
    if (current.state === 'running' || current.state === 'paused') {
      return { ok: false, error: 'Procedure already active' };
    }

    clearTimer();
    points = selection.points.map((p) => ({ ...p }));
    rowAccumulator = [];
    iterationIndex = 0;
    pointIndex = 0;
    phase = 'idle';
    params = { mode, insertionForce, iterations };
    resultsStore.clear();

    procedureStore.patch({
      state: 'running',
      backend: 'mock',
      params,
      processState: 'Running',
      capabilities: capabilities(),
      notes: notes(),
      error: null,
      startedAt: new Date().toISOString(),
      completedAt: null,
      connected: true,
    });
    updateProgress();

    setImmediate(() => processStep());
    return { ok: true, status: procedureStore.getStatus() };
  }

  function pause() {
    const current = procedureStore.getStatus();
    if (current.state !== 'running') {
      return { ok: false, error: 'Procedure is not running' };
    }
    clearTimer();
    if (machineClient && typeof machineClient.pause === 'function') {
      machineClient.pause();
    }
    updateProgress({ state: 'paused', processState: 'Running' });
    return { ok: true, status: procedureStore.getStatus() };
  }

  function resume() {
    const current = procedureStore.getStatus();
    if (current.state !== 'paused') {
      return { ok: false, error: 'Procedure is not paused' };
    }
    if (machineClient && typeof machineClient.resume === 'function') {
      machineClient.resume();
    }
    updateProgress({ state: 'running', processState: 'Running' });
    setImmediate(() => processStep());
    return { ok: true, status: procedureStore.getStatus() };
  }

  function stop() {
    const current = procedureStore.getStatus();
    if (current.state !== 'running' && current.state !== 'paused') {
      return { ok: true, status: current };
    }
    clearTimer();
    phase = 'idle';
    if (machineClient && typeof machineClient.stop === 'function') {
      machineClient.stop();
    }
    finishStopped('Stopped by operator');
    return { ok: true, status: procedureStore.getStatus() };
  }

  function getStatus() {
    return procedureStore.getStatus();
  }

  function dispose() {
    clearTimer();
  }

  return {
    kind: 'mock',
    start,
    pause,
    resume,
    stop,
    getStatus,
    dispose,
  };
}

module.exports = {
  createMockProcedureAdapter,
};
