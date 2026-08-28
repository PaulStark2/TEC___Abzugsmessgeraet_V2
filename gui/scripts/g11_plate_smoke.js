'use strict';

/**
 * G11 smoke: Bauplatte wechseln (Spannsystem) against Protocol + SimPlant.
 *
 * Proves:
 *   1) HMI assets for #plate and status-bar Spannsystem field
 *   2) fixtureOpen/Close set Core fixtureExtended
 *   3) SimPlant reports independent fixtureOpen via /api/simulation/state
 *   4) Spreizer (pneumaticExtended) stays independent
 *
 * Usage (from gui/):
 *   npm run smoke:g11
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
            // keep raw text (HTML / JS modules)
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

async function waitStatus(guiPort, predicate, label, timeoutMs = 8000) {
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
    await sleep(100);
  }
  throw new Error(`timeout waiting for ${label}: ${JSON.stringify(last)}`);
}

async function waitPlant(guiPort, predicate, label, timeoutMs = 8000) {
  const deadline = Date.now() + timeoutMs;
  let last = null;
  while (Date.now() < deadline) {
    const response = await request(guiPort, 'GET', '/api/simulation/state');
    if (response.status === 200 && response.body && response.body.state) {
      last = response.body.state;
      if (predicate(last)) {
        return last;
      }
    }
    await sleep(100);
  }
  throw new Error(`timeout waiting for plant ${label}: ${JSON.stringify(last)}`);
}

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

    console.log('[1] HMI assets');
    const hmi = await request(guiPort, 'GET', '/');
    assert.strictEqual(hmi.status, 200, 'HMI page');
    assert.ok(hmi.raw.includes('id="fixture-state"'), 'status bar Spannsystem field');
    const pageJs = await request(guiPort, 'GET', '/js/pages/plate.js');
    assert.strictEqual(pageJs.status, 200, 'plate.js served');
    assert.ok(pageJs.raw.includes('btn-fixture-open'), 'open button');
    assert.ok(pageJs.raw.includes('btn-fixture-close'), 'close button');
    const simPage = await request(guiPort, 'GET', '/simulation');
    assert.strictEqual(simPage.status, 200, 'simulation page');
    assert.ok(simPage.raw.includes('id="sim-fixture-state"'), 'sim status Spannsystem');
    assert.ok(simPage.raw.includes('id="sim-fixture-plant"'), 'sim plant Spannsystem');
    console.log('  PASS: plate page + status fields present');

    console.log('[2] plant starts closed');
    const initialPlant = await request(guiPort, 'GET', '/api/simulation/state');
    assert.strictEqual(initialPlant.status, 200, 'plant state reachable');
    assert.strictEqual(initialPlant.body.state.fixtureOpen, false, 'plant fixture closed at start');
    const initialMachine = await request(guiPort, 'GET', '/api/machine');
    assert.strictEqual(initialMachine.body.status.fixtureExtended, false, 'core fixture closed at start');
    const spreaderBefore = initialMachine.body.status.pneumaticExtended;
    console.log('  PASS: plant and Core start closed');

    console.log('[3] fixtureOpen');
    const opened = await request(guiPort, 'POST', '/api/command', { command: 'fixtureOpen' });
    assert.strictEqual(opened.body.ok, true, `fixtureOpen: ${opened.body.error}`);
    await waitStatus(guiPort, (s) => s.fixtureExtended === true, 'Core fixtureExtended true');
    await waitPlant(guiPort, (s) => s.fixtureOpen === true, 'plant fixtureOpen true');
    const afterOpen = await request(guiPort, 'GET', '/api/machine');
    assert.strictEqual(afterOpen.body.status.pneumaticExtended, spreaderBefore, 'spreader unchanged on open');
    console.log('  PASS: Core fixtureExtended and plant fixtureOpen');

    console.log('[4] fixtureClose');
    const closed = await request(guiPort, 'POST', '/api/command', { command: 'fixtureClose' });
    assert.strictEqual(closed.body.ok, true, `fixtureClose: ${closed.body.error}`);
    await waitStatus(guiPort, (s) => s.fixtureExtended === false, 'Core fixtureExtended false');
    await waitPlant(guiPort, (s) => s.fixtureOpen === false, 'plant fixtureOpen false');
    const afterClose = await request(guiPort, 'GET', '/api/machine');
    assert.strictEqual(afterClose.body.status.pneumaticExtended, spreaderBefore, 'spreader unchanged on close');
    console.log('  PASS: close restores plant and Core; spreader independent');

    console.log('\ng11_plate_smoke.js: PASS');
  } finally {
    killChild(sim);
    killChild(gui);
  }
}

main().catch((err) => {
  console.error(`\ng11_plate_smoke.js: FAIL — ${err.message}`);
  process.exit(1);
});
