'use strict';

/**
 * G8 mock procedure smoke — isolated ephemeral GUI server.
 */

const { startSmokeServer } = require('./lib/smokeServer');

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

async function waitProcedure(request, predicate, timeoutMs, label) {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    const s = await request('GET', '/api/procedure/status', null);
    if (predicate(s.body.status)) return s.body.status;
    await sleep(100);
  }
  throw new Error(`Timeout waiting for ${label}`);
}

async function run() {
  const server = await startSmokeServer({ machineClient: 'mock' });
  const { request, stop } = server;

  try {
    const upload = await request('POST', '/api/program/upload', {
      version: 1,
      programName: 'G8Smoke',
      testPoints: [
        { name: 'P1', sequenceNumber: 1, x: 0, y: 0, z: 0 },
        { name: 'P2', sequenceNumber: 2, x: 5, y: 0, z: 0 },
      ],
    });
    assert(upload.status === 200 && upload.body.ok, 'upload');

    const put = await request('PUT', '/api/program/selection', { selectAll: true });
    assert(put.status === 200 && put.body.ok, 'select');
    const confirm = await request('POST', '/api/program/selection/confirm', null);
    assert(confirm.status === 200 && confirm.body.ok, 'confirm');

    const start = await request('POST', '/api/procedure/start', {
      mode: 'test',
      insertionForce: 20,
      iterations: 2,
    });
    assert(start.status === 200 && start.body.ok, 'procedure start');
    assert(start.body.status.backend === 'mock', 'backend mock');

    const done = await waitProcedure(
      request,
      (s) => s.state === 'completed',
      15000,
      'completed'
    );
    assert(done.progress.percent === 100, 'percent 100');

    const results = await request('GET', '/api/results/current', null);
    assert(results.status === 200 && results.body.ok, 'results current');
    assert(results.body.results.rows.length === 4, '2 points × 2 iterations');
    assert(results.body.results.dataCompleteness === 'full', 'full mock data');

    console.log('G8 procedure smoke: PASS');
  } finally {
    await stop();
  }
}

run().catch((err) => {
  console.error('G8 procedure smoke FAIL:', err.message);
  process.exit(1);
});
