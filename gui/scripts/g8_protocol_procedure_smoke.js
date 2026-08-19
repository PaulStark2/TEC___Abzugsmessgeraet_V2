'use strict';

/**
 * G8 protocol smoke + M14.2 disconnect scenarios A–E.
 *
 * Spawns isolated simulation_protocol + GUI (protocol) on free ports.
 * Does NOT touch ports 3000/8766 used by npm run dev:start.
 */

const http = require('http');
const path = require('path');
const fs = require('fs');
const { spawn } = require('child_process');
const { findFreePort, startSmokeServer } = require('./lib/smokeServer');

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

function request(base, method, urlPath, body) {
  return new Promise((resolve, reject) => {
    const url = new URL(urlPath, base);
    const payload = body ? JSON.stringify(body) : null;
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

function resolveProtocolHost() {
  const root = path.join(__dirname, '..', '..');
  const candidates = [
    path.join(root, 'build', 'simulation_protocol', 'simulation_protocol.exe'),
    path.join(root, 'build', 'simulation_protocol', 'simulation_protocol'),
    path.join(root, 'build', 'Debug', 'simulation_protocol.exe'),
    path.join(root, 'build', 'Release', 'simulation_protocol.exe'),
  ];
  for (const c of candidates) {
    if (fs.existsSync(c)) return c;
  }
  return null;
}

function startProtocolHost(exe, port) {
  const child = spawn(exe, [String(port)], {
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  let stderr = '';
  child.stderr.on('data', (d) => {
    stderr += d.toString();
  });
  const stop = () =>
    new Promise((resolve) => {
      if (child.killed || child.exitCode !== null) {
        resolve();
        return;
      }
      child.once('exit', () => resolve());
      child.kill();
      setTimeout(() => {
        if (child.exitCode === null) child.kill('SIGKILL');
        resolve();
      }, 2000);
    });
  return { child, stop, getStderr: () => stderr };
}

async function waitMachineOnline(base, timeoutMs = 20000) {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    try {
      const r = await request(base, 'GET', '/api/machine', null);
      if (r.status === 200 && r.body.status && r.body.status.connected === true) {
        return r.body;
      }
    } catch {
      // ignore
    }
    await sleep(200);
  }
  throw new Error('Protocol session not ready (connected=true)');
}

async function waitMachineOffline(base, timeoutMs = 15000) {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    try {
      const r = await request(base, 'GET', '/api/machine', null);
      if (r.status === 200 && r.body.status && r.body.status.connected === false) {
        return r.body;
      }
    } catch {
      // ignore
    }
    await sleep(200);
  }
  throw new Error('Expected machine offline');
}

async function prepareSelection(base) {
  await request(base, 'POST', '/api/program/upload', {
    version: 1,
    programName: 'G8Proto',
    testPoints: [
      { name: 'A', sequenceNumber: 1, x: 0, y: 0, z: 0 },
      { name: 'B', sequenceNumber: 2, x: 3, y: 0, z: 0 },
    ],
  });
  await request(base, 'PUT', '/api/program/selection', { selectAll: true });
  await request(base, 'POST', '/api/program/selection/confirm', null);
}

async function waitProcedure(base, predicate, timeoutMs, label) {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    const s = await request(base, 'GET', '/api/procedure/status', null);
    if (predicate(s.body.status)) return s.body.status;
    await sleep(150);
  }
  throw new Error(`Timeout: ${label}`);
}

async function run() {
  const exe = resolveProtocolHost();
  if (!exe) {
    console.log('G8 protocol smoke: SKIP (simulation_protocol binary not found)');
    return;
  }

  const protocolPort = await findFreePort();
  let host = startProtocolHost(exe, protocolPort);
  await sleep(500);
  if (host.child.exitCode !== null) {
    throw new Error(`simulation_protocol exited early: ${host.getStderr()}`);
  }

  const gui = await startSmokeServer({
    machineClient: 'protocol',
    env: {
      PROTOCOL_HOST: '127.0.0.1',
      PROTOCOL_PORT: String(protocolPort),
    },
  });

  try {
    // --- A: GUI + controller online → command works ---
    await waitMachineOnline(gui.base);
    let move = await request(gui.base, 'POST', '/api/command', {
      command: 'moveTo',
      x: 1,
      y: 0,
      z: 0,
    });
    assert(move.status === 200 && move.body.ok, 'A: moveTo online');
    console.log('Disconnect A: PASS');

    // G8 protocol procedure (Core ProcedureController)
    await waitMachineOnline(gui.base);

    const origin = await request(gui.base, 'POST', '/api/calibration/plate-origin', {
      x: 0,
      y: 0,
      z: 0,
    });
    assert(origin.status === 200 && origin.body.ok, 'plate origin');

    await prepareSelection(gui.base);

    const startWithoutHome = await request(gui.base, 'POST', '/api/procedure/start', {
      mode: 'test',
      insertionForce: 0.1,
      iterations: 1,
    });
    assert(startWithoutHome.status === 400 && startWithoutHome.body.ok === false, 'start without homing rejected');

    const home = await request(gui.base, 'POST', '/api/command', { command: 'home' });
    assert(home.status === 200 && home.body.ok, 'CmdHome');
    const homedAt = Date.now();
    let isHomed = false;
    while (Date.now() - homedAt < 90000) {
      const machine = await request(gui.base, 'GET', '/api/machine', null);
      if (machine.body.status && machine.body.status.isHomed === true) {
        isHomed = true;
        break;
      }
      await sleep(200);
    }
    assert(isHomed, 'machine isHomed after home sequence');

    const start = await request(gui.base, 'POST', '/api/procedure/start', {
      mode: 'test',
      insertionForce: 0.1,
      iterations: 1,
    });
    assert(start.status === 200 && start.body.ok, 'G8 protocol start after homing');
    assert(start.body.status.backend === 'protocol', 'backend protocol');
    assert(start.body.status.capabilities.iterationsSupported === true, 'iterations Core');
    assert(start.body.status.capabilities.modeSupported === true, 'mode Core');
    assert(start.body.status.progress.totalIterations === 1, 'requested iterations');

    const done = await waitProcedure(
      gui.base,
      (s) => s.state === 'completed' || s.state === 'stopped',
      90000,
      'procedure terminal'
    );
    // SimPlant force is 0 (M15). Phase 1 must not succeed without Sollkraft.
    assert(done.state === 'stopped', `expected abort without force, got ${done.state}`);

    const results = await request(gui.base, 'GET', '/api/results/current', null);
    assert(results.status === 200, 'protocol results');
    assert(results.body.results.backend === 'protocol', 'results backend');
    assert(
      results.body.results.dataCompleteness === 'partial' ||
        results.body.results.dataCompleteness === 'complete',
      'controller data completeness'
    );
    console.log('G8 protocol procedure: PASS');

    // --- B: kill controller → offline, commands rejected ---
    await host.stop();
    host = { stop: async () => {}, child: { exitCode: 0 }, getStderr: () => '' };
    await waitMachineOffline(gui.base);
    move = await request(gui.base, 'POST', '/api/command', {
      command: 'moveTo',
      x: 2,
      y: 0,
      z: 0,
    });
    assert(move.status === 400 && move.body.ok === false, 'B: move rejected offline');
    const guiStillUp = await request(gui.base, 'GET', '/', null);
    assert(guiStillUp.status === 200, 'B: GUI still reachable');
    console.log('Disconnect B: PASS');

    // --- E already covered: controller dead, GUI alive ---
    console.log('Disconnect E: PASS');

    // --- C: restart controller → reconnect + command works ---
    host = startProtocolHost(exe, protocolPort);
    await sleep(500);
    await waitMachineOnline(gui.base, 25000);
    move = await request(gui.base, 'POST', '/api/command', {
      command: 'moveTo',
      x: 0,
      y: 0,
      z: 0,
    });
    assert(move.status === 200 && move.body.ok, 'C: move after reconnect');
    console.log('Disconnect C: PASS');

    // --- D: stop GUI, controller remains ---
    const hostStillRunning = host.child.exitCode === null;
    await gui.stop();
    assert(hostStillRunning && host.child.exitCode === null, 'D: controller still alive after GUI stop');
    console.log('Disconnect D: PASS');

    console.log('G8 protocol smoke + disconnect: PASS');
  } finally {
    try {
      await gui.stop();
    } catch {
      // already stopped in D
    }
    await host.stop();
  }
}

run().catch((err) => {
  console.error('G8 protocol smoke FAIL:', err.message);
  process.exit(1);
});
