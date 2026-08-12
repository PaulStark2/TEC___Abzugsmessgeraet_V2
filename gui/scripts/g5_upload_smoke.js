'use strict';

/**
 * G5 upload smoke — self-contained: starts a fresh GUI server, then tears it down.
 * Usage: node scripts/g5_upload_smoke.js
 */

const { startSmokeServer } = require('./lib/smokeServer');

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

async function run() {
  const server = await startSmokeServer();
  const { request, stop } = server;

  try {
    // Fresh server: no program loaded
    const cur0 = await request('GET', '/api/program/current', null);
    assert(cur0.status === 404, 'GET /api/program/current should be 404 before load');
    assert(cur0.body.ok === false, 'no program loaded should return ok=false');
    assert(cur0.body.error === 'No program loaded', 'error message mismatch');

    // Invalid upload: missing testPoints
    const inv = await request('POST', '/api/program/upload', { version: 1, programName: 'X' });
    assert(inv.status === 400, 'invalid upload should be HTTP 400');
    assert(inv.body.ok === false, 'invalid upload ok=false');
    assert(Array.isArray(inv.body.errors), 'invalid upload should include errors[]');

    // Valid upload: 1 point
    const valid = await request('POST', '/api/program/upload', {
      version: 1,
      programName: 'Zugtest_01',
      testPoints: [{ name: 'Punkt A', sequenceNumber: 1, x: 10.0, y: 20.0, z: -5.0 }],
    });
    assert(valid.status === 200, 'valid upload should be HTTP 200');
    assert(valid.body.ok === true, 'valid upload ok=true');
    assert(valid.body.summary.count === 1, 'summary.count should be 1');
    assert(valid.body.summary.programName === 'Zugtest_01', 'summary.programName should match');

    const cur1 = await request('GET', '/api/program/current', null);
    assert(cur1.status === 200, 'GET current after upload should be 200');
    assert(cur1.body.ok === true, 'current ok=true');
    assert(cur1.body.current.meta.version === 1, 'current version');
    assert(cur1.body.current.testPoints.length === 1, 'current point count');
    assert(cur1.body.current.testPoints[0].name === 'Punkt A', 'current point name');

    // Invalid upload must not overwrite store (duplicate sequenceNumber)
    const before = cur1.body.current;
    const inv2 = await request('POST', '/api/program/upload', {
      testPoints: [
        { name: 'A', sequenceNumber: 0, x: 0, y: 0, z: 0 },
        { name: 'B', sequenceNumber: 0, x: 1, y: 1, z: 1 },
      ],
    });
    assert(inv2.status === 400, 'duplicate upload should be HTTP 400');
    assert(inv2.body.ok === false, 'duplicate upload ok=false');

    const cur2 = await request('GET', '/api/program/current', null);
    assert(cur2.status === 200, 'store should still exist after invalid upload');
    assert(cur2.body.ok === true, 'ok true after unchanged store');
    assert(cur2.body.current.meta.version === before.meta.version, 'store version unchanged');
    assert(
      cur2.body.current.testPoints.length === before.testPoints.length,
      'store length unchanged'
    );
    assert(
      cur2.body.current.testPoints[0].name === before.testPoints[0].name,
      'store content unchanged'
    );

    console.log('G5 upload smoke: PASS');
  } finally {
    await stop();
  }
}

run().catch((err) => {
  console.error('G5 upload smoke FAIL:', err.message);
  process.exit(1);
});
