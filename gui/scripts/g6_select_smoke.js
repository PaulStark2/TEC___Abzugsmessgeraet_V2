'use strict';

/**
 * G6 selection smoke — self-contained: starts a fresh GUI server, then tears it down.
 * Usage: node scripts/g6_select_smoke.js
 */

const { startSmokeServer } = require('./lib/smokeServer');

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

async function run() {
  const server = await startSmokeServer();
  const { request, stop } = server;

  try {
    const sel0 = await request('GET', '/api/program/selection', null);
    assert(sel0.status === 404, 'selection without program is 404');

    const upload = await request('POST', '/api/program/upload', {
      version: 1,
      programName: 'G6Smoke',
      testPoints: [
        { name: 'P1', sequenceNumber: 1, x: 0, y: 0, z: 0 },
        { name: 'P2', sequenceNumber: 2, x: 1, y: 1, z: 1 },
      ],
    });
    assert(upload.status === 200 && upload.body.ok, 'upload ok');

    const sel1 = await request('GET', '/api/program/selection', null);
    assert(sel1.status === 200, 'selection GET ok');
    assert(sel1.body.selection.confirmed === false, 'not confirmed yet');

    const put = await request('PUT', '/api/program/selection', {
      selectedSequenceNumbers: [2],
    });
    assert(put.status === 200 && put.body.ok, 'PUT selection ok');
    assert(put.body.selection.draftSequenceNumbers.length === 1, 'one selected');

    const confirmOk = await request('POST', '/api/program/selection/confirm', null);
    assert(confirmOk.status === 200 && confirmOk.body.ok, 'confirm ok');

    const sel2 = await request('GET', '/api/program/selection', null);
    assert(sel2.body.selection.confirmed === true, 'confirmed');
    assert(sel2.body.selection.points.length === 1, 'confirmed one point');
    assert(sel2.body.selection.points[0].sequenceNumber === 2, 'confirmed seq 2');

    const del = await request('DELETE', '/api/program/selection', null);
    assert(del.status === 200 && del.body.ok, 'DELETE ok');
    assert(del.body.selection.confirmed === false, 'cleared confirmed');

    const selectAll = await request('PUT', '/api/program/selection', { selectAll: true });
    assert(selectAll.body.selection.draftSequenceNumbers.length === 2, 'select all');

    const confirmEmpty = await request('PUT', '/api/program/selection', {
      selectedSequenceNumbers: [],
    });
    assert(confirmEmpty.status === 200, 'empty draft allowed');
    const confirmBad = await request('POST', '/api/program/selection/confirm', null);
    assert(confirmBad.status === 400, 'empty confirm rejected');

    // upload clears selection
    await request('PUT', '/api/program/selection', { selectAll: true });
    await request('POST', '/api/program/selection/confirm', null);
    const upload2 = await request('POST', '/api/program/upload', {
      version: 1,
      programName: 'G6Smoke2',
      testPoints: [{ name: 'Only', sequenceNumber: 1, x: 0, y: 0, z: 0 }],
    });
    assert(upload2.status === 200, 're-upload ok');
    const sel3 = await request('GET', '/api/program/selection', null);
    assert(sel3.body.selection.confirmed === false, 'upload cleared confirmed');
    assert(sel3.body.selection.draftSequenceNumbers.length === 0, 'upload cleared draft');

    console.log('G6 select smoke: PASS');
  } finally {
    await stop();
  }
}

run().catch((err) => {
  console.error('G6 select smoke FAIL:', err.message);
  process.exit(1);
});
