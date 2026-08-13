'use strict';

/**
 * M14.2 architecture separation proof (self-contained).
 *
 * Proves:
 *   A) GUI without controller → move rejected, pose frozen
 *   B) Controller without GUI → MOVE_TO works via M12 ProtocolMachineClient
 *   C) GUI + controller together
 *   D/E) Disconnect + reconnect (client-level)
 *   F) Simulation stop while GUI keeps running
 *   G) GUI stop while simulation keeps running
 *
 * Does not require a browser. Uses ephemeral ports when possible.
 *
 * Usage (from gui/):
 *   npm run proof:architecture
 */

const http = require('http');
const net = require('net');
const path = require('path');
const { spawn } = require('child_process');
const assert = require('assert');
const {
  createProtocolMachineClient,
  MACHINE_UNAVAILABLE,
} = require('../server/protocol/protocolMachineClient');

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

function findFreePort() {
  return new Promise((resolve, reject) => {
    const s = net.createServer();
    s.listen(0, '127.0.0.1', () => {
      const { port } = s.address();
      s.close((err) => (err ? reject(err) : resolve(port)));
    });
    s.on('error', reject);
  });
}

function defaultExe() {
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

function spawnSim(port) {
  const exe = defaultExe();
  const child = spawn(exe, [String(port)], {
    stdio: ['ignore', 'pipe', 'pipe'],
    windowsHide: true,
  });
  let out = '';
  child.stdout.on('data', (d) => {
    out += d.toString();
  });
  child.stderr.on('data', (d) => {
    out += d.toString();
  });
  child.__out = () => out;
  return child;
}

function spawnGui({ guiPort, protocolPort }) {
  const indexJs = path.resolve(__dirname, '..', 'server', 'index.js');
  const child = spawn(process.execPath, [indexJs], {
    cwd: path.resolve(__dirname, '..'),
    env: {
      ...process.env,
      MACHINE_CLIENT: 'protocol',
      PROTOCOL_HOST: '127.0.0.1',
      PROTOCOL_PORT: String(protocolPort),
      PORT: String(guiPort),
    },
    stdio: ['ignore', 'pipe', 'pipe'],
    windowsHide: true,
  });
  let out = '';
  child.stdout.on('data', (d) => {
    out += d.toString();
  });
  child.stderr.on('data', (d) => {
    out += d.toString();
  });
  child.__out = () => out;
  return child;
}

function killChild(child) {
  if (!child || child.killed || child.exitCode !== null) return;
  try {
    child.kill('SIGTERM');
  } catch {
    // ignore
  }
  try {
    child.kill('SIGKILL');
  } catch {
    // ignore
  }
}

async function waitPort(host, port, ms = 15000) {
  const deadline = Date.now() + ms;
  while (Date.now() < deadline) {
    try {
      await new Promise((resolve, reject) => {
        const s = net.connect({ host, port }, () => {
          s.end();
          resolve();
        });
        s.on('error', reject);
      });
      return;
    } catch {
      await sleep(150);
    }
  }
  throw new Error(`timeout waiting for ${host}:${port}`);
}

function request(guiPort, method, urlPath, body) {
  const payload = body ? JSON.stringify(body) : null;
  return new Promise((resolve, reject) => {
    const req = http.request(
      {
        hostname: '127.0.0.1',
        port: guiPort,
        path: urlPath,
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
          let parsed = null;
          try {
            parsed = text ? JSON.parse(text) : null;
          } catch {
            parsed = text;
          }
          resolve({ status: res.statusCode, body: parsed, raw: text });
        });
      }
    );
    req.on('error', reject);
    if (payload) req.write(payload);
    req.end();
  });
}

async function waitConnected(client, ms = 8000) {
  const deadline = Date.now() + ms;
  while (Date.now() < deadline) {
    if (client.getStatus().connected) return;
    await sleep(100);
  }
  throw new Error('timeout waiting connected');
}

async function waitDisconnected(client, ms = 8000) {
  const deadline = Date.now() + ms;
  while (Date.now() < deadline) {
    if (!client.getStatus().connected) return;
    await sleep(100);
  }
  throw new Error('timeout waiting disconnected');
}

async function waitGuiMachine(guiPort, predicate, ms = 10000, label = 'condition') {
  const deadline = Date.now() + ms;
  let last = null;
  while (Date.now() < deadline) {
    try {
      const r = await request(guiPort, 'GET', '/api/machine');
      last = r.body;
      if (r.status === 200 && predicate(r.body)) return r.body;
    } catch {
      // ignore until up
    }
    await sleep(200);
  }
  throw new Error(`timeout waiting GUI ${label}: ${JSON.stringify(last)}`);
}

async function testB_ControllerWithoutGui() {
  console.log('\n[B] Controller without GUI');
  const port = await findFreePort();
  const sim = spawnSim(port);
  try {
    await waitPort('127.0.0.1', port);
    const client = createProtocolMachineClient({
      host: '127.0.0.1',
      port,
      reconnectDelayMs: 200,
      ackTimeoutMs: 3000,
    });
    try {
      await waitConnected(client);
      const before = client.getStatus();
      const move = await client.moveTo(4, 0, 0);
      assert.strictEqual(move.ok, true, `move ok: ${move.error}`);
      const deadline = Date.now() + 5000;
      let after = client.getStatus();
      while (Date.now() < deadline && !(after.x > before.x + 0.5)) {
        await sleep(100);
        after = client.getStatus();
      }
      assert.ok(after.x > before.x + 0.5, `pose advanced without GUI (x=${after.x})`);
      console.log('  PASS: MOVE_TO + status via M12 only (no browser/GUI)');
    } finally {
      client.close();
    }
  } finally {
    killChild(sim);
  }
}

async function testA_GuiWithoutController() {
  console.log('\n[A] GUI without controller');
  const guiPort = await findFreePort();
  const deadProtocolPort = await findFreePort(); // nothing listens
  const gui = spawnGui({ guiPort, protocolPort: deadProtocolPort });
  try {
    await waitPort('127.0.0.1', guiPort);
    const machine = await waitGuiMachine(
      guiPort,
      (m) => m && m.machineClientMode === 'protocol',
      8000,
      'protocol mode'
    );
    assert.strictEqual(machine.machineClientMode, 'protocol');
    // May briefly try connecting; wait until offline
    await waitGuiMachine(
      guiPort,
      (m) => m.status && m.status.connected === false,
      8000,
      'offline'
    );
    const before = (await request(guiPort, 'GET', '/api/machine')).body.status;
    const move = await request(guiPort, 'POST', '/api/command', {
      command: 'moveTo',
      x: 50,
      y: 0,
      z: 0,
    });
    assert.ok(move.body && move.body.ok === false, 'offline move must fail');
    assert.ok(
      String(move.body.error || '').includes('Machine connection unavailable') ||
        String(move.body.error || '').includes('unavailable'),
      `error=${move.body.error}`
    );
    await sleep(800);
    const after = (await request(guiPort, 'GET', '/api/machine')).body.status;
    assert.strictEqual(after.x, before.x, 'pose must not change offline');
    assert.strictEqual(after.y, before.y);
    assert.strictEqual(after.z, before.z);
    console.log('  PASS: offline GUI rejects MOVE_TO; pose frozen');
  } finally {
    killChild(gui);
  }
}

async function testC_GuiPlusController() {
  console.log('\n[C] GUI + controller');
  const guiPort = await findFreePort();
  const simPort = await findFreePort();
  const sim = spawnSim(simPort);
  let gui = null;
  try {
    await waitPort('127.0.0.1', simPort);
    gui = spawnGui({ guiPort, protocolPort: simPort });
    await waitPort('127.0.0.1', guiPort);
    await waitGuiMachine(guiPort, (m) => m.status && m.status.connected === true, 12000, 'online');
    let move = null;
    for (let i = 0; i < 8; i += 1) {
      move = await request(guiPort, 'POST', '/api/command', {
        command: 'moveTo',
        x: 6,
        y: 0,
        z: 0,
      });
      if (move.body && move.body.ok) break;
      await sleep(250);
    }
    assert.ok(move && move.body && move.body.ok, `move failed: ${move && move.body && move.body.error}`);
    await waitGuiMachine(guiPort, (m) => m.status && m.status.x > 1, 8000, 'pose move');
    console.log('  PASS: GUI command reaches Core via protocol host');
  } finally {
    killChild(gui);
    killChild(sim);
  }
}

async function testDE_DisconnectReconnect() {
  console.log('\n[D/E] Disconnect + reconnect (ProtocolMachineClient)');
  const port = await findFreePort();
  let sim = spawnSim(port);
  const client = createProtocolMachineClient({
    host: '127.0.0.1',
    port,
    reconnectDelayMs: 250,
    ackTimeoutMs: 3000,
  });
  try {
    await waitPort('127.0.0.1', port);
    await waitConnected(client);
    killChild(sim);
    await waitDisconnected(client);
    const failed = await client.moveTo(9, 0, 0);
    assert.strictEqual(failed.ok, false);
    assert.strictEqual(failed.error, MACHINE_UNAVAILABLE);
    sim = spawnSim(port);
    await waitPort('127.0.0.1', port);
    await waitConnected(client, 10000);
    const ok = await client.moveTo(2, 0, 0);
    assert.strictEqual(ok.ok, true);
    console.log('  PASS: disconnect rejects; reconnect accepts MOVE_TO');
  } finally {
    client.close();
    killChild(sim);
  }
}

async function testF_SimStopGuiSurvives() {
  console.log('\n[F] Simulation stop while GUI runs');
  const guiPort = await findFreePort();
  const simPort = await findFreePort();
  const sim = spawnSim(simPort);
  let gui = null;
  try {
    await waitPort('127.0.0.1', simPort);
    gui = spawnGui({ guiPort, protocolPort: simPort });
    await waitPort('127.0.0.1', guiPort);
    await waitGuiMachine(guiPort, (m) => m.status && m.status.connected === true);
    killChild(sim);
    await waitGuiMachine(guiPort, (m) => m.status && m.status.connected === false, 10000, 'offline');
    const machine = await request(guiPort, 'GET', '/api/machine');
    assert.strictEqual(machine.status, 200);
    assert.strictEqual(machine.body.ok, true);
    assert.strictEqual(machine.body.status.connected, false);
    console.log('  PASS: GUI stays up after simulation stop');
  } finally {
    killChild(gui);
    killChild(sim);
  }
}

async function testG_GuiStopSimSurvives() {
  console.log('\n[G] GUI stop while simulation runs');
  const guiPort = await findFreePort();
  const simPort = await findFreePort();
  const sim = spawnSim(simPort);
  let gui = null;
  try {
    await waitPort('127.0.0.1', simPort);
    gui = spawnGui({ guiPort, protocolPort: simPort });
    await waitPort('127.0.0.1', guiPort);
    await waitGuiMachine(guiPort, (m) => m.status && m.status.connected === true);
    killChild(gui);
    await sleep(500);
    const client = createProtocolMachineClient({
      host: '127.0.0.1',
      port: simPort,
      reconnectDelayMs: 200,
      ackTimeoutMs: 3000,
    });
    try {
      await waitConnected(client);
      const move = await client.moveTo(3, 0, 0);
      assert.strictEqual(move.ok, true);
      console.log('  PASS: simulation survives GUI stop; direct M12 still works');
    } finally {
      client.close();
    }
  } finally {
    killChild(gui);
    killChild(sim);
  }
}

async function testNegativ_OfflineThenOnlineViaGui() {
  console.log('\n[NEG] GUI offline MOVE_TO rejected; after host start MOVE_TO works');
  const guiPort = await findFreePort();
  const simPort = await findFreePort();
  const gui = spawnGui({ guiPort, protocolPort: simPort });
  let sim = null;
  try {
    await waitPort('127.0.0.1', guiPort);
    await waitGuiMachine(guiPort, (m) => m.status && m.status.connected === false);
    const before = (await request(guiPort, 'GET', '/api/machine')).body.status;
    const offline = await request(guiPort, 'POST', '/api/command', {
      command: 'moveTo',
      x: 40,
      y: 0,
      z: 0,
    });
    assert.strictEqual(offline.body.ok, false);
    await sleep(500);
    const mid = (await request(guiPort, 'GET', '/api/machine')).body.status;
    assert.strictEqual(mid.x, before.x);

    sim = spawnSim(simPort);
    await waitPort('127.0.0.1', simPort);
    await waitGuiMachine(guiPort, (m) => m.status && m.status.connected === true, 12000, 'reconnect');
    let online = null;
    for (let i = 0; i < 8; i += 1) {
      online = await request(guiPort, 'POST', '/api/command', {
        command: 'moveTo',
        x: 5,
        y: 0,
        z: 0,
      });
      if (online.body && online.body.ok) break;
      await sleep(250);
    }
    assert.strictEqual(online.body.ok, true);
    await waitGuiMachine(guiPort, (m) => m.status && m.status.x > 1, 8000, 'pose after reconnect');
    console.log('  PASS: negativ path offline fail → online success');
  } finally {
    killChild(gui);
    killChild(sim);
  }
}

async function main() {
  console.log('M14.2 architecture proof');
  console.log('exe=', defaultExe());
  await testB_ControllerWithoutGui();
  await testA_GuiWithoutController();
  await testC_GuiPlusController();
  await testDE_DisconnectReconnect();
  await testF_SimStopGuiSurvives();
  await testG_GuiStopSimSurvives();
  await testNegativ_OfflineThenOnlineViaGui();
  console.log('\nm14_2_architecture_proof.js: ALL PASS');
}

main().catch((err) => {
  console.error('FAILED:', err);
  process.exit(1);
});
