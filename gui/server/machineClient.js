'use strict';

/**
 * GUI-side machine status contract (semantic alignment with Core M5).
 * Not imported from C++; no dependency on core/.
 *
 * @typedef {'Idle' | 'Active' | 'Paused' | 'Stopped' | 'Fault'} MachineState
 * @typedef {'Idle' | 'Moving' | 'AtTarget'} MotionState
 *
 * @typedef {Object} MachineStatus
 * @property {MachineState} machineState
 * @property {MotionState} motionState
 * @property {number} x
 * @property {number} y
 * @property {number} z
 *
 * MachineClient contract (G2):
 * - getStatus() → MachineStatus snapshot (authority is the client impl, not the browser)
 * - tick(dtSeconds)? → advance mock/sim time (optional; ProtocolMachineClient later may omit)
 * - close()? → release resources
 *
 * Later (G12): ProtocolMachineClient behind the same factory key `protocol`.
 */

const { createMockMachineClient } = require('./mock/mockMachineClient');

/**
 * Create a MachineClient by name.
 * @param {string} [kind] defaults to env MACHINE_CLIENT or 'mock'
 * @returns {{ getStatus: () => MachineStatus, tick?: (dt: number) => void, close?: () => void }}
 */
function createMachineClient(kind) {
  const selected = (kind || process.env.MACHINE_CLIENT || 'mock').toLowerCase();

  switch (selected) {
    case 'mock':
      return createMockMachineClient();
    case 'protocol':
      // Placeholder for G12 — keep factory shape stable.
      throw new Error(
        'ProtocolMachineClient is not implemented yet (planned for G12). Use MACHINE_CLIENT=mock.'
      );
    default:
      throw new Error(`Unknown MACHINE_CLIENT="${selected}". Supported in G2: mock`);
  }
}

module.exports = {
  createMachineClient,
};
