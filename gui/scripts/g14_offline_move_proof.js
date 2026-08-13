'use strict';

/**
 * M14 offline-move proof (self-contained, does NOT use ports 3000/8766).
 *
 * Spawns simulation_protocol on an ephemeral port, drives ProtocolMachineClient,
 * kills the host, and proves:
 *   - offline moveTo → ok=false
 *   - pose unchanged
 *   - no local kinematics
 *
 * Usage (from gui/):
 *   node scripts/g14_offline_move_proof.js
 *
 * Optional:
 *   set SIMULATION_PROTOCOL_EXE=..\build\simulation_protocol\simulation_protocol.exe
 */

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

async function waitConnected(client, ms = 5000) {
  const start = Date.now();
  while (Date.now() - start < ms) {
    if (client.getStatus().connected) return;
    await sleep(50);
  }
  throw new Error('timeout waiting connected');
}

async function waitDisconnected(client, ms = 5000) {
  const start = Date.now();
  while (Date.now() - start < ms) {
    if (!client.getStatus().connected) return;
    await sleep(50);
  }
  throw new Error('timeout waiting disconnected');
}

async function main() {
  const exe = defaultExe();
  const port = await findFreePort();
  console.log(`Using ephemeral port ${port}`);
  console.log(`Spawning ${exe}`);

  const child = spawn(exe, [String(port)], { stdio: ['ignore', 'pipe', 'pipe'] });
  let childOut = '';
  child.stdout.on('data', (d) => {
    childOut += String(d);
  });
  child.stderr.on('data', (d) => {
    childOut += String(d);
  });

  await sleep(400);
  if (child.exitCode !== null) {
    throw new Error(`host exited early: ${childOut}`);
  }

  const client = createProtocolMachineClient({
    host: '127.0.0.1',
    port,
    ackTimeoutMs: 1000,
  });

  try {
    await waitConnected(client);
    console.log('TEST1 online connect: PASS', client.getStatus());

    const online = await client.moveTo(5, 0, 0);
    assert.ok(online.ok, `online move failed: ${online.error}`);
    // Allow status push to catch up.
    for (let i = 0; i < 40; i += 1) {
      if (client.getStatus().x > 0.5) break;
      await sleep(50);
    }
    assert.ok(client.getStatus().x > 0.5, 'pose should advance while host alive');
    console.log('TEST1 online move+pose: PASS', { x: client.getStatus().x });

    console.log('Killing simulation_protocol child…');
    child.kill('SIGTERM');
    await sleep(200);
    try {
      child.kill('SIGKILL');
    } catch {
      // ignore
    }
    await waitDisconnected(client);
    assert.strictEqual(client.getStatus().connected, false);
    console.log('TEST2 offline status: PASS');

    const poseBefore = {
      x: client.getStatus().x,
      y: client.getStatus().y,
      z: client.getStatus().z,
    };
    console.log('Pose before offline moves:', poseBefore);

    for (const cmd of [
      () => client.moveTo(99, 0, 0),
      () => client.jog('X', 1, 1),
      () => client.pneumaticOpen(),
      () => client.pause(),
    ]) {
      const result = await cmd();
      assert.strictEqual(result.ok, false, 'offline mutating command must fail');
      assert.strictEqual(result.error, MACHINE_UNAVAILABLE);
    }

    const poseAfter = {
      x: client.getStatus().x,
      y: client.getStatus().y,
      z: client.getStatus().z,
    };
    assert.strictEqual(poseAfter.x, poseBefore.x);
    assert.strictEqual(poseAfter.y, poseBefore.y);
    assert.strictEqual(poseAfter.z, poseBefore.z);
    console.log('TEST3/4 offline commands + frozen pose: PASS', poseAfter);

    // Restart host on same port → reconnect → move works again
    const child2 = spawn(exe, [String(port)], { stdio: ['ignore', 'pipe', 'pipe'] });
    await sleep(400);
    await waitConnected(client, 8000);
    const again = await client.moveTo(1, 0, 0);
    assert.ok(again.ok, `reconnect move failed: ${again.error}`);
    console.log('TEST5 reconnect move: PASS');
    child2.kill('SIGKILL');
  } finally {
    client.close();
    try {
      child.kill('SIGKILL');
    } catch {
      // ignore
    }
  }

  console.log('g14_offline_move_proof.js: ALL PASS');
}

main().catch((err) => {
  console.error('FAILED:', err);
  process.exit(1);
});
