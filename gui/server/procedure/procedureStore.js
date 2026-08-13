'use strict';

/**
 * In-memory procedure state for G8 (UI orchestration metadata).
 * Does not own motion/process authority — adapters update this snapshot.
 */

function createEmptyStatus() {
  return {
    state: 'idle', // idle | running | paused | completed | stopped | error | unavailable
    backend: null, // mock | protocol | unsupported
    params: null,
    progress: {
      currentIteration: 0,
      totalIterations: 0,
      currentPointIndex: 0,
      totalPoints: 0,
      percent: 0,
    },
    processState: null,
    capabilities: {
      iterationsSupported: false,
      modeSupported: false,
      insertionForceSupported: false,
      simulatedForces: false,
    },
    notes: [],
    error: null,
    startedAt: null,
    completedAt: null,
    connected: null,
  };
}

let status = createEmptyStatus();

function reset() {
  status = createEmptyStatus();
}

function getStatus() {
  return {
    ...status,
    params: status.params ? { ...status.params } : null,
    progress: { ...status.progress },
    capabilities: { ...status.capabilities },
    notes: [...status.notes],
  };
}

function patch(partial) {
  status = {
    ...status,
    ...partial,
    params: partial.params !== undefined ? partial.params : status.params,
    progress: partial.progress
      ? { ...status.progress, ...partial.progress }
      : status.progress,
    capabilities: partial.capabilities
      ? { ...status.capabilities, ...partial.capabilities }
      : status.capabilities,
    notes: partial.notes !== undefined ? [...partial.notes] : status.notes,
  };
  return getStatus();
}

module.exports = {
  reset,
  getStatus,
  patch,
  createEmptyStatus,
};
