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
 * - tick(dtSeconds)? → advance mock time only (never used for sim/protocol)
 * - moveTo(x,y,z) / pause() / resume() / stop() → { ok, status?, error? } | Promise
 * - close()? → release resources
 *
 * MACHINE_CLIENT=mock      → MockMachineClient (offline JS mock)
 * MACHINE_CLIENT=sim       → SimMachineClient (M8.5 TCP/JSON :8765)
 * MACHINE_CLIENT=protocol  → ProtocolMachineClient (M12 binary TCP :8766)
 */

const { createMockMachineClient } = require('./mock/mockMachineClient');
const { createSimMachineClient } = require('./sim/simMachineClient');
const { createProtocolMachineClient } = require('./protocol/protocolMachineClient');

/**
 * @param {string} [kind]
 */
function createMachineClient(kind) {
  const selected = (kind || process.env.MACHINE_CLIENT || 'mock').toLowerCase();
  console.log(`[MACHINE] client mode = ${selected}`);

  let client;
  switch (selected) {
    case 'mock':
      client = createMockMachineClient();
      break;
    case 'sim':
      client = createSimMachineClient();
      break;
    case 'protocol':
      client = createProtocolMachineClient();
      break;
    default:
      throw new Error(`Unknown MACHINE_CLIENT="${selected}". Supported: mock, sim, protocol`);
  }

  // Runtime identity for diagnostics (must not be spoofable by Core status).
  client.machineClientMode = selected;
  client.implementationName =
    selected === 'mock'
      ? 'MockMachineClient'
      : selected === 'sim'
        ? 'SimMachineClient'
        : 'ProtocolMachineClient';

  return client;
}

module.exports = {
  createMachineClient,
};
