'use strict';

/**
 * G9 results smoke — depends on G8 mock completion via isolated server.
 */

const { startSmokeServer } = require('./lib/smokeServer');
const { rawJob } = require('../server/program/sampleJob');

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
  const { request, stop, base } = server;

  try {
    await request('DELETE', '/api/results/current', null);
    const empty = await request('GET', '/api/results/current', null);
    assert(empty.status === 404, 'no results initially');

    await request(
      'POST',
      '/api/program/upload',
      rawJob([{ toothNumber: 11, x: 1, y: 2, z: 3 }], { buildJobNumber: 'G9Smoke' })
    );
    await request('PUT', '/api/program/selection', { selectAll: true });
    await request('POST', '/api/program/selection/confirm', null);
    await request('POST', '/api/procedure/start', {
      mode: 'test',
      insertionForce: 8,
      iterations: 1,
    });

    await waitProcedure(request, (s) => s.state === 'completed', 10000, 'completed');

    const current = await request('GET', '/api/results/current', null);
    assert(current.status === 200, 'results available');
    assert(Array.isArray(current.body.results.rows), 'rows array');

    const exported = await request('GET', '/api/results/export', null);
    assert(exported.status === 200, 'export 200');
    const text = typeof exported.body === 'string' ? exported.body : String(exported.body);
    assert(text.startsWith('PK'), 'export is a zip');

    console.log('G9 results smoke: PASS', { base });
  } finally {
    await stop();
  }
}

run().catch((err) => {
  console.error('G9 results smoke FAIL:', err.message);
  process.exit(1);
});
