'use strict';

/**
 * G3 smoke scenarios against a running GUI server (localhost:3000).
 * Usage: node scripts/g3_smoke.js
 */

const http = require('http');
const { WebSocket } = require('ws');

const BASE = 'http://127.0.0.1:3000';

function request(method, path, body) {
  const url = new URL(path, BASE);
  const payload = body ? JSON.stringify(body) : null;
  return new Promise((resolve, reject) => {
    const req = http.request(
      {
        hostname: url.hostname,
        port: url.port,
        path: url.pathname,
        method,
        headers: payload
          ? {
              'Content-Type': 'application/json',
              'Content-Length': Buffer.byteLength(payload),
            }
          : {},
      },
      (res) => {
        const chunks = [];
        res.on('data', (c) => chunks.push(c));
        res.on('end', () => {
          const text = Buffer.concat(chunks).toString('utf8');
          let json = null;
          try {
            json = JSON.parse(text);
          } catch {
            json = text;
          }
          resolve({ status: res.statusCode, body: json });
        });
      }
    );
    req.on('error', reject);
    if (payload) req.write(payload);
    req.end();
  });
}

function command(body) {
  return request('POST', '/api/command', body);
}

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

function nearlyEqual(a, b, eps = 0.05) {
  return Math.abs(a - b) <= eps;
}

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

async function waitUntil(predicate, timeoutMs, label) {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    const status = await getStatusOnce();
    if (predicate(status)) return status;
    await sleep(100);
  }
  throw new Error(`Timeout waiting for ${label}`);
}

function getStatusOnce() {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket('ws://127.0.0.1:3000/ws/status');
    const t = setTimeout(() => {
      ws.close();
      reject(new Error('WS status timeout'));
    }, 3000);
    ws.on('message', (data) => {
      clearTimeout(t);
      const msg = JSON.parse(data.toString());
      ws.close();
      resolve(msg);
    });
    ws.on('error', reject);
  });
}

async function sampleStatus(count, intervalMs) {
  const samples = [];
  for (let i = 0; i < count; i += 1) {
    samples.push(await getStatusOnce());
    if (i < count - 1) await sleep(intervalMs);
  }
  return samples;
}

async function run() {
  const html = await request('GET', '/');
  assert(html.status === 200, 'HTTP / should be 200');

  // Reset toward origin then moveTo 10/20/30
  let r = await command({ command: 'moveTo', x: 0, y: 0, z: 0 });
  assert(r.body.ok, 'moveTo origin ok');
  await waitUntil(
    (s) => nearlyEqual(s.x, 0) && nearlyEqual(s.y, 0) && nearlyEqual(s.z, 0),
    8000,
    'origin'
  );

  // --- Test 1 MoveTo ---
  r = await command({ command: 'moveTo', x: 10, y: 20, z: 30 });
  assert(r.body.ok, 'moveTo target ok');
  assert(r.body.status.machineState === 'Active', 'T1 Active');
  assert(r.body.status.motionState === 'Moving', 'T1 Moving');

  let mid = await waitUntil((s) => s.motionState === 'Moving' && s.x > 0.5, 3000, 'motion progress');
  assert(mid.machineState === 'Active', 'T1 mid Active');

  const arrived = await waitUntil(
    (s) =>
      s.motionState === 'AtTarget' &&
      nearlyEqual(s.x, 10) &&
      nearlyEqual(s.y, 20) &&
      nearlyEqual(s.z, 30),
    8000,
    'AtTarget 10/20/30'
  );
  assert(arrived.machineState === 'Idle', 'T1 Idle after AtTarget');
  console.log('Test 1 MoveTo: PASS', arrived);

  // --- Test 2 Pause ---
  r = await command({ command: 'moveTo', x: 50, y: 50, z: 50 });
  assert(r.body.ok, 'T2 moveTo');
  await waitUntil((s) => s.motionState === 'Moving' && s.x > 12, 5000, 'T2 moving');
  r = await command({ command: 'pause' });
  assert(r.body.status.machineState === 'Paused', 'T2 Paused');
  const pausedPose = { ...r.body.status };
  await sleep(600);
  const samples = await sampleStatus(4, 200);
  for (const s of samples) {
    assert(s.machineState === 'Paused', 'T2 stays Paused');
    assert(nearlyEqual(s.x, pausedPose.x, 0.01), 'T2 x frozen');
    assert(nearlyEqual(s.y, pausedPose.y, 0.01), 'T2 y frozen');
    assert(nearlyEqual(s.z, pausedPose.z, 0.01), 'T2 z frozen');
  }
  console.log('Test 2 Pause: PASS', samples[samples.length - 1]);

  // --- Test 3 Resume ---
  r = await command({ command: 'resume' });
  assert(r.body.status.machineState === 'Active', 'T3 Active');
  assert(r.body.status.motionState === 'Moving', 'T3 Moving');
  const done = await waitUntil(
    (s) =>
      s.motionState === 'AtTarget' &&
      nearlyEqual(s.x, 50) &&
      nearlyEqual(s.y, 50) &&
      nearlyEqual(s.z, 50),
    10000,
    'T3 AtTarget'
  );
  console.log('Test 3 Resume: PASS', done);

  // --- Test 4 Stop ---
  r = await command({ command: 'moveTo', x: 0, y: 0, z: 0 });
  await waitUntil((s) => s.motionState === 'Moving', 3000, 'T4 moving');
  r = await command({ command: 'stop' });
  assert(r.body.status.machineState === 'Stopped', 'T4 Stopped');
  const stopPose = { ...r.body.status };
  await sleep(500);
  const afterStop = await sampleStatus(4, 200);
  for (const s of afterStop) {
    assert(s.machineState === 'Stopped', 'T4 stays Stopped');
    assert(nearlyEqual(s.x, stopPose.x, 0.01), 'T4 x frozen');
    assert(nearlyEqual(s.y, stopPose.y, 0.01), 'T4 y frozen');
    assert(nearlyEqual(s.z, stopPose.z, 0.01), 'T4 z frozen');
    assert(!(nearlyEqual(s.x, 0) && nearlyEqual(s.y, 0) && nearlyEqual(s.z, 0)), 'T4 not finished to origin');
  }
  console.log('Test 4 Stop: PASS', afterStop[afterStop.length - 1]);

  console.log('All G3 smoke tests passed.');
}

run().catch((err) => {
  console.error('FAIL:', err.message);
  process.exit(1);
});
