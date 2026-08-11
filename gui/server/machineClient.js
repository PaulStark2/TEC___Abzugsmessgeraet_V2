'use strict';

/**
 * GUI-side machine status/command contract (semantic alignment with Core M5).
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
 * MachineClient contract:
 * - getStatus() → MachineStatus
 * - tick(dtSeconds)? → advance mock time
 * - moveTo(x,y,z) / pause() / resume() / stop() → { ok, status?, error? } (G3)
 * - close()? → release resources
 *
 * Later (G12): ProtocolMachineClient behind factory key `protocol`.
 */

const { createMockMachineClient } = require('./mock/mockMachineClient');

/**
 * @param {string} [kind]
 */
function createMachineClient(kind) {
  const selected = (kind || process.env.MACHINE_CLIENT || 'mock').toLowerCase();

  switch (selected) {
    case 'mock':
      return createMockMachineClient();
    case 'protocol':
      throw new Error(
        'ProtocolMachineClient is not implemented yet (planned for G12). Use MACHINE_CLIENT=mock.'
      );
    default:
      throw new Error(`Unknown MACHINE_CLIENT="${selected}". Supported: mock`);
  }
}

module.exports = {
  createMachineClient,
};
