'use strict';

/**
 * G8 unit tests — mock procedure adapter (no HTTP server).
 */

const programStore = require('../server/program/programStore');
const selectionStore = require('../server/program/selectionStore');
const resultsStore = require('../server/results/resultsStore');
const procedureStore = require('../server/procedure/procedureStore');
const { createMockMachineClient } = require('../server/mock/mockMachineClient');
const { createMockProcedureAdapter } = require('../server/procedure/mockProcedureAdapter');

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

async function waitUntil(predicate, timeoutMs, label) {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    if (predicate()) return;
    await sleep(50);
  }
  throw new Error(`Timeout: ${label}`);
}

async function run() {
  programStore.clearProgram();
  selectionStore.clear();
  resultsStore.clear();
  procedureStore.reset();

  const machine = createMockMachineClient();
  const adapter = createMockProcedureAdapter(machine);

  // Reject without selection
  let r = adapter.start({ mode: 'test', insertionForce: 10, iterations: 1 });
  assert(!r.ok, 'start without selection fails');

  programStore.setProgram({
    version: 1,
    programName: 'G8Unit',
    testPoints: [
      { name: 'A', sequenceNumber: 1, x: 1, y: 0, z: 0 },
      { name: 'B', sequenceNumber: 2, x: 2, y: 0, z: 0 },
    ],
  });
  selectionStore.setDraft({ selectAll: true });
  selectionStore.confirmSelection();

  r = adapter.start({ mode: 'aging', insertionForce: 12.5, iterations: 2 });
  assert(r.ok, 'start ok');
  assert(r.status.state === 'running', 'running');
  assert(r.status.capabilities.iterationsSupported === true, 'mock iterations supported');

  await waitUntil(() => adapter.getStatus().state === 'completed', 10000, 'complete');
  const results = resultsStore.getResults();
  assert(results, 'results exist');
  assert(results.backend === 'mock', 'mock backend');
  assert(results.rows.length === 4, `expected 4 rows, got ${results.rows.length}`);
  assert(results.params.mode === 'aging', 'mode stored');
  assert(results.params.iterations === 2, 'iterations stored');

  // pause / resume / stop
  resultsStore.clear();
  procedureStore.reset();
  r = adapter.start({ mode: 'test', insertionForce: 5, iterations: 5 });
  assert(r.ok, 'start for pause');
  await sleep(80);
  r = adapter.pause();
  assert(r.ok && r.status.state === 'paused', 'paused');
  await sleep(200);
  assert(adapter.getStatus().state === 'paused', 'stays paused');
  r = adapter.resume();
  assert(r.ok && r.status.state === 'running', 'resumed');
  r = adapter.stop();
  assert(r.ok && r.status.state === 'stopped', 'stopped');

  adapter.dispose();
  console.log('G8 procedure unit tests: PASS');
}

run().catch((err) => {
  console.error('G8 procedure unit tests FAIL:', err.message);
  process.exit(1);
});
