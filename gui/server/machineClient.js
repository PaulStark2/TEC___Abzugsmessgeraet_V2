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
 * - tick(dtSeconds)? → advance mock time only (never used for MACHINE_CLIENT=sim)
 * - moveTo(x,y,z) / pause() / resume() / stop() → { ok, status?, error? }
 * - close()? → release resources
 *
 * G3.5: MACHINE_CLIENT=sim → SimMachineClient (TCP to simulation_interactive).
 * G12: ProtocolMachineClient after Core M12/M13 (not this pre-protocol TCP).
 */

const { createMockMachineClient } = require('./mock/mockMachineClient');
const { createSimMachineClient } = require('./sim/simMachineClient');

/**
 * @param {string} [kind]
 */
function createMachineClient(kind) {
  const selected = (kind || process.env.MACHINE_CLIENT || 'mock').toLowerCase();

  switch (selected) {
    case 'mock':
      return createMockMachineClient();
    case 'sim':
      return createSimMachineClient();
    case 'protocol':
      throw new Error(
        'ProtocolMachineClient is not implemented yet (planned for G12). Use MACHINE_CLIENT=mock or sim.'
      );
    default:
      throw new Error(`Unknown MACHINE_CLIENT="${selected}". Supported: mock, sim`);
  }
}

module.exports = {
  createMachineClient,
};
