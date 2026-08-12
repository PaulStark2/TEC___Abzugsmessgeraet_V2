'use strict';

/**
 * G14 smoke test against a running simulation_protocol (port 8766).
 *
 * Prerequisites:
 *   1. Build and start: simulation_protocol.exe  (default 8766)
 *   2. Start GUI: set MACHINE_CLIENT=protocol && npm start
 *
 * Usage:
 *   node scripts/g14_protocol_smoke.js
 *
 * Does NOT start servers itself.
 */

const http = require('http');
const { WebSocket } = require('ws');

const BASE = process.env.GUI_BASE || 'http://127.0.0.1:3000';
const WS_URL = process.env.GUI_WS || 'ws://127.0.0.1:3000/ws/status';

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

function nearlyEqual(a, b, eps = 0.5) {
  return Math.abs(a - b) <= eps;
}

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

function getStatusOnce() {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(WS_URL);
    const t = setTimeout(() => {
      ws.close();
      reject(new Error('WS status timeout'));
    }, 3000);
    ws.on('message', (data) => {
      clearTimeout(t);
      try {
        const msg = JSON.parse(String(data));
        ws.close();
        resolve(msg);
      } catch (err) {
        ws.close();
        reject(err);
      }
    });
    ws.on('error', (err) => {
      clearTimeout(t);
      reject(err);
    });
  });
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

async function main() {
  console.log('G14 protocol smoke — expecting MACHINE_CLIENT=protocol + simulation_protocol:8766');

  // 1) Status available
  let status = await getStatusOnce();
  assert(status.type === 'status', 'status type');
  assert(typeof status.x === 'number', 'has x');
  assert(typeof status.machineState === 'string', 'has machineState');
  console.log('1. Status received: PASS', {
    machineState: status.machineState,
    x: status.x,
    y: status.y,
    z: status.z,
    fx: status.fx,
  });

  // 2) MOVE_TO
  let r = await command({ command: 'moveTo', x: 8, y: 0, z: 0 });
  assert(r.body.ok, `moveTo ok: ${r.body.error || r.status}`);
  status = await waitUntil((s) => s.x > 1, 8000, 'position move');
  assert(status.x > 1, 'position changed');
  console.log('2. MOVE_TO + motion: PASS', { x: status.x });

  // 3) PAUSE
  r = await command({ command: 'pause' });
  assert(r.body.ok, `pause ok: ${r.body.error}`);
  const paused = await waitUntil((s) => s.machineState === 'Paused', 3000, 'Paused');
  const pausedX = paused.x;
  await sleep(300);
  const still = await getStatusOnce();
  assert(nearlyEqual(still.x, pausedX, 0.05), 'pose frozen while paused');
  console.log('3. PAUSE: PASS');

  // 4) RESUME
  r = await command({ command: 'resume' });
  assert(r.body.ok, `resume ok: ${r.body.error}`);
  await waitUntil((s) => s.machineState === 'Active' || s.machineState === 'Idle', 5000, 'resume');
  console.log('4. RESUME: PASS');

  // Wait until near target or Idle
  await waitUntil(
    (s) => s.machineState === 'Idle' || nearlyEqual(s.x, 8, 0.5),
    10000,
    'near target'
  );

  // 5) STOP during new move
  r = await command({ command: 'moveTo', x: 20, y: 0, z: 0 });
  assert(r.body.ok, 'moveTo for stop');
  await sleep(100);
  r = await command({ command: 'stop' });
  assert(r.body.ok, `stop ok: ${r.body.error}`);
  status = await waitUntil((s) => s.machineState === 'Stopped', 3000, 'Stopped');
  console.log('5. STOP: PASS', { x: status.x });

  // 6) Pneumatic
  r = await command({ command: 'pneumaticOpen' });
  assert(r.body.ok, `pneumaticOpen: ${r.body.error}`);
  status = await waitUntil((s) => s.pneumaticExtended === true || s.spreaderExtended === true, 3000, 'spreader');
  console.log('6. Pneumatic open: PASS');

  r = await command({ command: 'pneumaticClose' });
  assert(r.body.ok, `pneumaticClose: ${r.body.error}`);
  console.log('7. Pneumatic close: PASS');

  // 7) setSpeed unsupported
  r = await command({ command: 'setSpeed', speed: 20 });
  assert(!r.body.ok, 'setSpeed should fail');
  assert(
    String(r.body.error || '').includes('Not supported'),
    `setSpeed error message: ${r.body.error}`
  );
  console.log('8. setSpeed unsupported: PASS');

  // 8) Force fields present from protocol status
  status = await getStatusOnce();
  assert(typeof status.fx === 'number', 'fx present');
  assert(typeof status.fy === 'number', 'fy present');
  assert(typeof status.fz === 'number', 'fz present');
  console.log('9. Force fields: PASS', { fx: status.fx, fy: status.fy, fz: status.fz });

  console.log('G14 protocol smoke: ALL PASS');
}

main().catch((err) => {
  console.error('G14 protocol smoke FAILED:', err.message || err);
  process.exit(1);
});
