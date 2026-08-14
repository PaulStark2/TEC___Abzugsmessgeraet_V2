'use strict';

/**
 * G10 GUI improvements smoke — progress, delete program, scroll list, fault/homing prep.
 * Usage: node scripts/g10_gui_improvements_smoke.js
 */

const { startSmokeServer } = require('./lib/smokeServer');
const { derivePointProgress } = require('../server/procedure/procedureProgress');
const { formatLimitWarnings } = require('../server/display/limitWarnings');
const { MessageId } = require('../server/protocol/protocolTypes');
const { encodeEmptyMessage } = require('../server/protocol/m12Codec');

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

function makeProgram(pointCount) {
  const testPoints = [];
  for (let i = 1; i <= pointCount; i += 1) {
    testPoints.push({
      name: `P${i}`,
      sequenceNumber: i,
      x: i,
      y: 0,
      z: 0,
    });
  }
  return {
    version: 1,
    programName: `Prog${pointCount}`,
    testPoints,
  };
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

function testProcedureProgressHelper() {
  const running = derivePointProgress({
    totalPoints: 5,
    index0Based: 4,
    processState: 'Running',
    procedureState: 'running',
  });
  assert(running.pointsDone === 4, 'running last point shows 4/5 while in progress');

  const complete = derivePointProgress({
    totalPoints: 5,
    index0Based: null,
    processState: 'Completed',
    procedureState: 'completed',
  });
  assert(complete.pointsDone === 5, 'completed shows 5/5');
  assert(complete.percent === 100, 'completed percent 100');
  assert(complete.isComplete === true, 'isComplete true');
}

function testLimitWarnings() {
  const none = formatLimitWarnings(0);
  assert(none.length === 0, 'no limits');

  const xMax = formatLimitWarnings(1 << 1);
  assert(xMax.length === 1, 'one limit');
  assert(xMax[0] === 'X-Max Endanschlag aktiv', 'X-Max label');

  const all = formatLimitWarnings(0b111111);
  assert(all.length === 6, 'all six limits');
  assert(all.includes('Z-Max Endanschlag aktiv'), 'Z-Max label');
}

function testClearFaultEncoding() {
  const encoded = encodeEmptyMessage(MessageId.CmdClearFault);
  assert(encoded.ok === true, 'CmdClearFault encodes');
}

async function runHttpTests() {
  const server = await startSmokeServer({ machineClient: 'mock' });
  const { request, stop } = server;

  try {
    // --- Progress 5/5 ---
    const upload5 = await request('POST', '/api/program/upload', makeProgram(5));
    assert(upload5.status === 200 && upload5.body.ok, 'upload 5 points');
    await request('PUT', '/api/program/selection', { selectAll: true });
    await request('POST', '/api/program/selection/confirm', null);

    const start5 = await request('POST', '/api/procedure/start', {
      mode: 'test',
      insertionForce: 10,
      iterations: 1,
    });
    assert(start5.status === 200 && start5.body.ok, 'start 5-point procedure');

    const done5 = await waitProcedure(
      request,
      (s) => s.state === 'completed',
      20000,
      '5-point completed'
    );
    assert(done5.progress.pointsDone === 5, 'pointsDone 5/5');
    assert(done5.progress.totalPoints === 5, 'totalPoints 5');
    assert(done5.progress.percent === 100, 'percent 100');
    assert(done5.progress.isComplete === true, 'isComplete');

    // --- 12 points in summary (no 5 cap) ---
    const upload12 = await request('POST', '/api/program/upload', makeProgram(12));
    assert(upload12.body.summary.count === 12, 'count 12');
    assert(upload12.body.summary.pointsPreview.length === 12, 'preview all 12');
    assert(upload12.body.summary.testPoints.length === 12, 'testPoints all 12');

    // --- Delete program ---
    const delIdle = await request('DELETE', '/api/program/current', null);
    assert(delIdle.status === 200 && delIdle.body.ok, 'delete idle ok');
    const afterDel = await request('GET', '/api/program/current', null);
    assert(afterDel.status === 404, '404 after delete');

    await request('POST', '/api/program/upload', makeProgram(2));
    await request('PUT', '/api/program/selection', { selectAll: true });
    await request('POST', '/api/program/selection/confirm', null);
    await request('POST', '/api/procedure/start', {
      mode: 'test',
      insertionForce: 5,
      iterations: 1,
    });
    const delRunning = await request('DELETE', '/api/program/current', null);
    assert(delRunning.status === 409, 'delete blocked while running');
    await request('POST', '/api/procedure/stop', null);
    await sleep(200);

    await request('POST', '/api/program/upload', makeProgram(2));
    await request('PUT', '/api/program/selection', { selectAll: true });
    await request('POST', '/api/program/selection/confirm', null);
    await request('POST', '/api/procedure/start', {
      mode: 'test',
      insertionForce: 5,
      iterations: 1,
    });
    await waitProcedure(request, (s) => s.state === 'completed', 15000, 'complete for results');
    const resultsBefore = await request('GET', '/api/results/current', null);
    assert(resultsBefore.body.results, 'results exist before delete');
    await request('DELETE', '/api/program/current', null);
    const resultsAfter = await request('GET', '/api/results/current', null);
    assert(resultsAfter.body.results, 'results preserved after program delete');

    // --- Fault reset (mock: no fake success) ---
    const clearFault = await request('POST', '/api/command', { command: 'clearFault' });
    assert(clearFault.status === 400, 'mock clearFault HTTP 400');
    assert(clearFault.body.ok === false, 'mock clearFault not ok');
    assert(
      String(clearFault.body.error).includes('Mock'),
      'mock clearFault explains controller requirement'
    );

    // --- HOME command ---
    const home = await request('POST', '/api/command', { command: 'home' });
    assert(home.status === 200 && home.body.ok, 'HOME command accepted');
    assert(home.body.status.machineState === 'Homing', 'mock enters Homing');

    console.log('G10 GUI improvements smoke: PASS');
  } finally {
    await stop();
  }
}

async function run() {
  testProcedureProgressHelper();
  testLimitWarnings();
  testClearFaultEncoding();
  await runHttpTests();
}

run().catch((err) => {
  console.error('G10 GUI improvements smoke FAIL:', err.message);
  process.exit(1);
});
