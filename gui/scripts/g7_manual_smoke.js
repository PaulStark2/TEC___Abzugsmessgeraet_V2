'use strict';

/**
 * G7 smoke scenarios against a running GUI server (localhost:3000).
 * Usage: node scripts/g7_manual_smoke.js
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

async function waitForServer(timeoutMs = 20000) {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    try {
      const r = await request('GET', '/', null);
      if (r.status === 200) return;
    } catch {
      // ignore
    }
    await sleep(200);
  }
  throw new Error('GUI server did not become reachable in time');
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

async function measureMoveDuration(speed, targetX) {
  await command({ command: 'setSpeed', speed });
  await command({ command: 'moveTo', x: 0, y: 0, z: 0 });
  await waitUntil(
    (s) => nearlyEqual(s.x, 0) && nearlyEqual(s.y, 0) && nearlyEqual(s.z, 0),
    10000,
    'origin before speed test'
  );

  const t0 = Date.now();
  let r = await command({ command: 'moveTo', x: targetX, y: 0, z: 0 });
  assert(r.body.ok, 'moveTo for speed test');
  await waitUntil((s) => nearlyEqual(s.x, targetX) && s.motionState === 'AtTarget', 15000, `target ${targetX}`);
  return Date.now() - t0;
}

async function run() {
  await waitForServer();

  // Reset pose
  let r = await command({ command: 'moveTo', x: 0, y: 0, z: 0 });
  assert(r.body.ok, 'moveTo origin');
  await waitUntil(
    (s) => nearlyEqual(s.x, 0) && nearlyEqual(s.y, 0) && nearlyEqual(s.z, 0),
    8000,
    'origin'
  );

  // --- setSpeed ---
  r = await command({ command: 'setSpeed', speed: 25 });
  assert(r.body.ok, 'setSpeed ok');
  assert(r.body.status.manualSpeed === 25, 'manualSpeed in status');

  // --- jog ---
  const beforeJog = await getStatusOnce();
  r = await command({ command: 'jog', axis: 'X', direction: 1 });
  assert(r.body.ok, 'jog +X ok');
  assert(r.body.status.motionState === 'Moving', 'jog moving');
  await waitUntil((s) => nearlyEqual(s.x, beforeJog.x + 1, 0.1), 5000, 'jog +1 X');
  console.log('G7 jog: PASS');

  // --- pneumatic ---
  r = await command({ command: 'pneumaticOpen' });
  assert(r.body.status.pneumaticExtended === true, 'pneumatic open');
  r = await command({ command: 'pneumaticClose' });
  assert(r.body.status.pneumaticExtended === false, 'pneumatic close');
  console.log('G7 pneumatic: PASS');

  // --- home stub ---
  await command({ command: 'moveTo', x: 10, y: 5, z: 3 });
  await waitUntil((s) => nearlyEqual(s.x, 10), 8000, 'away from home');
  r = await command({ command: 'home' });
  assert(r.body.status.machineState === 'Homing', 'home starts Homing');
  assert(r.body.status.homingInProgress === true, 'homingInProgress true');
  await waitUntil(
    (s) => nearlyEqual(s.x, 0) && nearlyEqual(s.y, 0) && nearlyEqual(s.z, 0) && s.machineState === 'Idle',
    12000,
    'home complete'
  );
  console.log('G7 home: PASS');

  // --- stop during jog ---
  r = await command({ command: 'jog', axis: 'Y', direction: 1, step: 20 });
  assert(r.body.ok, 'long jog');
  await sleep(200);
  r = await command({ command: 'stop' });
  assert(r.body.status.machineState === 'Stopped', 'stopped');
  const stoppedY = r.body.status.y;
  await sleep(500);
  const afterStop = await getStatusOnce();
  assert(afterStop.machineState === 'Stopped', 'stays stopped');
  assert(nearlyEqual(afterStop.y, stoppedY, 0.01), 'y frozen after stop');
  console.log('G7 stop during motion: PASS');

  // --- speed affects duration ---
  const slowMs = await measureMoveDuration(5, 30);
  const fastMs = await measureMoveDuration(40, 30);
  assert(fastMs < slowMs, `faster speed should be quicker (fast=${fastMs}ms slow=${slowMs}ms)`);
  console.log(`G7 speed timing: PASS (slow=${slowMs}ms fast=${fastMs}ms)`);

  console.log('All G7 manual smoke tests passed.');
}

run().catch((err) => {
  console.error('FAIL:', err.message);
  process.exit(1);
});
