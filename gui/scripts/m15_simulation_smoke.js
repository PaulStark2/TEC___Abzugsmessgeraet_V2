'use strict';

/**
 * M15 simulation smoke test (self-contained, no browser required).
 *
 * Proves:
 *   1) /simulation is served as a standalone page
 *   2) plant channel state is reachable through the Node proxy
 *   3) force stays zero while idle (no injected force)
 *   4) probe delivers a short pulse and returns to LOW on its own
 *   5) X/Y/Z pose comes from the Core status (controller-driven)
 *   6) position-based limit switch trips and CORE decides the reaction (soft block)
 *   7) injected TCP force: Core ForceWatchdog → Fault (ForceCollision)
 *   8) offline: simulation UI data + machine commands fail clearly, pose freezes
 *
 * Usage (from gui/):
 *   npm run smoke:m15
 */

const assert = require('assert');
const http = require('http');
const net = require('net');
const path = require('path');
const { spawn } = require('child_process');

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function findFreePort() {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.listen(0, '127.0.0.1', () => {
      const { port } = server.address();
      server.close((err) => (err ? reject(err) : resolve(port)));
    });
    server.on('error', reject);
  });
}

function simulationExe() {
  if (process.env.SIMULATION_PROTOCOL_EXE) {
    return process.env.SIMULATION_PROTOCOL_EXE;
  }
  return path.resolve(
    __dirname,
    '..',
    '..',
    'build',
    'simulation_protocol',
    process.platform === 'win32' ? 'simulation_protocol.exe' : 'simulation_protocol'
  );
}

function spawnSim(protocolPort, plantPort) {
  return spawn(simulationExe(), [String(protocolPort), String(plantPort)], {
    stdio: ['ignore', 'ignore', 'ignore'],
    windowsHide: true,
  });
}

function spawnGui({ guiPort, protocolPort, plantPort }) {
  return spawn(process.execPath, [path.resolve(__dirname, '..', 'server', 'index.js')], {
    cwd: path.resolve(__dirname, '..'),
    env: {
      ...process.env,
      MACHINE_CLIENT: 'protocol',
      PROTOCOL_HOST: '127.0.0.1',
      PROTOCOL_PORT: String(protocolPort),
      PLANT_PORT: String(plantPort),
      PORT: String(guiPort),
    },
    stdio: ['ignore', 'ignore', 'ignore'],
    windowsHide: true,
  });
}

function killChild(child) {
  if (!child || child.exitCode !== null) {
    return;
  }
  try {
    child.kill('SIGKILL');
  } catch {
    // ignore
  }
}

async function waitPort(port, timeoutMs = 15000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    try {
      await new Promise((resolve, reject) => {
        const socket = net.connect({ host: '127.0.0.1', port }, () => {
          socket.end();
          resolve();
        });
        socket.on('error', reject);
      });
      return;
    } catch {
      await sleep(150);
    }
  }
  throw new Error(`timeout waiting for 127.0.0.1:${port}`);
}

function request(guiPort, method, urlPath, body) {
  const payload = body === undefined ? null : JSON.stringify(body);
  return new Promise((resolve, reject) => {
    const req = http.request(
      {
        hostname: '127.0.0.1',
        port: guiPort,
        path: urlPath,
        method,
        headers: payload
          ? { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(payload) }
          : {},
      },
      (res) => {
        const chunks = [];
        res.on('data', (chunk) => chunks.push(chunk));
        res.on('end', () => {
          const text = Buffer.concat(chunks).toString('utf8');
          let parsed = text;
          try {
            parsed = text ? JSON.parse(text) : null;
          } catch {
            // keep raw text (HTML pages)
          }
          resolve({ status: res.statusCode, body: parsed, raw: text });
        });
      }
    );
    req.on('error', reject);
    if (payload) {
      req.write(payload);
    }
    req.end();
  });
}

async function waitStatus(guiPort, predicate, label, timeoutMs = 12000) {
  const deadline = Date.now() + timeoutMs;
  let last = null;
  while (Date.now() < deadline) {
    const response = await request(guiPort, 'GET', '/api/machine');
    if (response.status === 200 && response.body && response.body.status) {
      last = response.body.status;
      if (predicate(last)) {
        return last;
      }
    }
    await sleep(150);
  }
  throw new Error(`timeout waiting for ${label}: ${JSON.stringify(last)}`);
}

const LIMIT_BIT_X_MAX = 1;

async function main() {
  const protocolPort = await findFreePort();
  const plantPort = await findFreePort();
  const guiPort = await findFreePort();

  const sim = spawnSim(protocolPort, plantPort);
  let gui = null;

  try {
    await waitPort(protocolPort);
    await waitPort(plantPort);
    gui = spawnGui({ guiPort, protocolPort, plantPort });
    await waitPort(guiPort);
    await waitStatus(guiPort, (status) => status.connected === true, 'controller connected');

    console.log('[1] /simulation page');
    const page = await request(guiPort, 'GET', '/simulation');
    assert.strictEqual(page.status, 200, 'simulation page served');
    assert.ok(page.raw.includes('gantry-canvas'), 'page contains gantry canvas');
    assert.ok(page.raw.includes('/js/sim/simulationPage.js'), 'page loads simulation module');
    const hmi = await request(guiPort, 'GET', '/');
    assert.ok(hmi.raw.includes('tcp-canvas'), 'machine HMI still intact');
    console.log('  PASS: standalone simulation page, HMI unchanged');

    console.log('[2] plant state via Node proxy');
    const initial = await request(guiPort, 'GET', '/api/simulation/state');
    assert.strictEqual(initial.status, 200, 'plant state reachable');
    assert.strictEqual(initial.body.state.limitXMax, false, 'X limit LOW at start');
    assert.strictEqual(initial.body.state.probeTriggered, false, 'probe LOW at start');
    console.log('  PASS: plant channel answers through /api/simulation/state');

    console.log('[3] force is zero while idle (no injected force)');
    const idleStatus = await request(guiPort, 'GET', '/api/machine');
    assert.strictEqual(idleStatus.body.status.fx, 0, 'fx = 0');
    assert.strictEqual(idleStatus.body.status.fy, 0, 'fy = 0');
    assert.strictEqual(idleStatus.body.status.fz, 0, 'fz = 0');
    console.log('  PASS: fx = fy = fz = 0');

    console.log('[4] probe pulse');
    const probe = await request(guiPort, 'POST', '/api/simulation/probe', { pulseMs: 100 });
    assert.strictEqual(probe.status, 200, 'probe accepted');
    assert.strictEqual(probe.body.state.probeTriggered, true, 'probe HIGH right after trigger');
    await sleep(400);
    const afterProbe = await request(guiPort, 'GET', '/api/simulation/state');
    assert.strictEqual(afterProbe.body.state.probeTriggered, false, 'probe returned to LOW');
    console.log('  PASS: short pulse, automatic reset, no machine motion');

    console.log('[5] pose follows controller (Y and Z)');
    const moveY = await request(guiPort, 'POST', '/api/command', {
      command: 'moveTo',
      x: 0,
      y: 4,
      z: 3,
    });
    assert.strictEqual(moveY.body.ok, true, `moveTo accepted: ${moveY.body.error}`);
    const reached = await waitStatus(
      guiPort,
      (status) => status.y > 3.5 && status.z > 2.5,
      'Y/Z pose from Core'
    );
    assert.ok(Math.abs(reached.x) < 0.5, 'X stayed near zero');
    console.log(`  PASS: pose from Core status (y=${reached.y.toFixed(2)}, z=${reached.z.toFixed(2)})`);

    console.log('[6] position-based limit switch, Core decides reaction');
    const limits = await request(guiPort, 'POST', '/api/simulation/limits', { xMax: 5 });
    assert.strictEqual(limits.status, 200, 'limits accepted');
    assert.strictEqual(limits.body.state.limits.xMax, 5, 'xMax stored in plant');

    const moveX = await request(guiPort, 'POST', '/api/command', {
      command: 'moveTo',
      x: 40,
      y: 4,
      z: 3,
    });
    assert.strictEqual(moveX.body.ok, true, `moveTo accepted: ${moveX.body.error}`);

    const blocked = await waitStatus(
      guiPort,
      (status) =>
        status.machineState !== 'Fault' &&
        status.limitWarning === true &&
        (status.blockedDirections & (1 << LIMIT_BIT_X_MAX)) !== 0,
      'Core soft limit blocks X+ without Fault'
    );
    assert.ok(
      (blocked.limits & (1 << LIMIT_BIT_X_MAX)) !== 0,
      `limit X+ reported HIGH (limits=${blocked.limits})`
    );
    assert.ok(blocked.x >= 5, `pose reached trip point (x=${blocked.x})`);
    assert.ok(blocked.x < 40, 'motion did not continue to the commanded target');

    const plantAtLimit = await request(guiPort, 'GET', '/api/simulation/state');
    assert.strictEqual(plantAtLimit.body.state.limitXMax, true, 'plant keeps sensor HIGH');
    console.log(`  PASS: sensor HIGH at x=${blocked.x.toFixed(2)}, Core blocked X+ (no Fault)`);

    console.log('[7] injected TCP force, Core ForceWatchdog');
    const inject = await request(guiPort, 'POST', '/api/simulation/injected-force', {
      fx: 25,
      fy: 0,
      fz: 0,
    });
    assert.strictEqual(inject.status, 200, 'injected-force accepted');
    assert.ok(Math.abs(inject.body.state.injectedFx - 25) < 0.01, 'plant reports injectedFx=25');
    const forceFault = await waitStatus(
      guiPort,
      (status) => status.machineState === 'Fault' && status.faultReason === 'ForceCollision',
      'Core Fault ForceCollision from injected Fx'
    );
    assert.strictEqual(forceFault.machineState, 'Fault', 'machine latched Fault');
    const clearInject = await request(guiPort, 'POST', '/api/simulation/injected-force', {
      fx: 0,
      fy: 0,
      fz: 0,
    });
    assert.strictEqual(clearInject.status, 200, 'injected-force clear accepted');
    const afterClear = await request(guiPort, 'GET', '/api/machine');
    assert.strictEqual(afterClear.body.status.machineState, 'Fault', 'Fault stays latched after release');
    console.log('  PASS: 25 N Fx → ForceCollision; release does not clear Fault');

    console.log('[8] offline behaviour');
    const beforeStop = (await request(guiPort, 'GET', '/api/machine')).body.status;
    killChild(sim);
    await waitStatus(guiPort, (status) => status.connected === false, 'controller offline');

    const offlinePlant = await request(guiPort, 'GET', '/api/simulation/state');
    assert.strictEqual(offlinePlant.status, 503, 'plant channel reports connection problem');
    assert.ok(offlinePlant.body.error, `plant error message present: ${offlinePlant.body.error}`);

    const offlineMove = await request(guiPort, 'POST', '/api/command', {
      command: 'moveTo',
      x: 1,
      y: 1,
      z: 1,
    });
    assert.strictEqual(offlineMove.body.ok, false, 'machine command rejected while offline');

    const offlinePage = await request(guiPort, 'GET', '/simulation');
    assert.strictEqual(offlinePage.status, 200, 'simulation UI stays available');

    await sleep(600);
    const afterStop = (await request(guiPort, 'GET', '/api/machine')).body.status;
    assert.strictEqual(afterStop.x, beforeStop.x, 'pose frozen (x)');
    assert.strictEqual(afterStop.y, beforeStop.y, 'pose frozen (y)');
    assert.strictEqual(afterStop.z, beforeStop.z, 'pose frozen (z)');
    console.log('  PASS: UI stays open, commands rejected, pose frozen');

    console.log('\nm15_simulation_smoke.js: PASS');
  } finally {
    killChild(sim);
    killChild(gui);
  }
}

main().catch((err) => {
  console.error(`\nm15_simulation_smoke.js: FAIL — ${err.message}`);
  process.exit(1);
});
