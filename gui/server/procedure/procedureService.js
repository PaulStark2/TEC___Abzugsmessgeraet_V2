'use strict';

/**
 * Procedure service — selects mock vs protocol adapter.
 * Never invents controller logic for protocol; sim (8765) is unsupported for G8.
 */

const procedureStore = require('./procedureStore');
const { createMockProcedureAdapter } = require('./mockProcedureAdapter');
const { createProtocolProcedureAdapter } = require('./protocolProcedureAdapter');

function createProcedureService(machineClient) {
  const mode = (
    machineClient.machineClientMode ||
    process.env.MACHINE_CLIENT ||
    'mock'
  ).toLowerCase();

  let adapter = null;

  if (mode === 'mock') {
    adapter = createMockProcedureAdapter(machineClient);
  } else if (mode === 'protocol') {
    adapter = createProtocolProcedureAdapter(machineClient);
  } else {
    adapter = {
      kind: 'unsupported',
      async start() {
        procedureStore.patch({
          state: 'unavailable',
          backend: 'unsupported',
          connected: false,
          error:
            'G8 Prozedur wird für MACHINE_CLIENT=sim nicht unterstützt. Bitte Mock oder Protocol verwenden.',
          notes: [
            'simulation_interactive (M8.5) hat keinen TestExecutor. Protocol-Pfad (8766) oder Mock nutzen.',
          ],
          capabilities: {
            iterationsSupported: false,
            modeSupported: false,
            insertionForceSupported: false,
            simulatedForces: false,
          },
        });
        return {
          ok: false,
          error: 'Procedure not supported for MACHINE_CLIENT=sim',
          status: procedureStore.getStatus(),
        };
      },
      async pause() {
        return { ok: false, error: 'Procedure not supported for MACHINE_CLIENT=sim' };
      },
      async resume() {
        return { ok: false, error: 'Procedure not supported for MACHINE_CLIENT=sim' };
      },
      async stop() {
        return { ok: true, status: procedureStore.getStatus() };
      },
      getStatus() {
        return procedureStore.getStatus();
      },
      dispose() {},
    };
  }

  return {
    getStatus() {
      return adapter.getStatus();
    },
    async start(body) {
      return adapter.start(body);
    },
    async pause() {
      return adapter.pause();
    },
    async resume() {
      return adapter.resume();
    },
    async stop() {
      return adapter.stop();
    },
    dispose() {
      adapter.dispose();
    },
  };
}

module.exports = {
  createProcedureService,
};
