'use strict';

/**
 * E2E diagnosis: which command in the GUI procedure-start chain returns InvalidState?
 * Spawns isolated simulation_protocol + GUI per case. Does not fix guards.
 *
 * Case A: no M19 (persisted probe_center from calibration store/file)
 * Case B: M19 to Completed, then start
 * Case C: intentional start while M19 still Running (expected first fail)
 */

const http = require('http');
const path = require('path');
const fs = require('fs');
const { spawn } = require('child_process');
const { findFreePort } = require('./lib/smokeServer');

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
  ];
  for (const c of candidates) {
    if (fs.existsSync(c)) return c;
  }
  return null;
}

function startProtocolHost(exe, protocolPort, plantPort) {
  const child = spawn(exe, [String(protocolPort), String(plantPort)], {
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  let stdout = '';
  let stderr = '';
  child.stdout.on('data', (d) => {
    stdout += d.toString();
  });
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
  return {
    child,
    stop,
    getStdout: () => stdout,
    getStderr: () => stderr,
  };
}

function startGuiServer({ protocolPort, plantPort, guiPort }) {
  const serverEntry = path.join(__dirname, '..', 'server', 'index.js');
  const child = spawn(process.execPath, [serverEntry], {
    env: {
      ...process.env,
      PORT: String(guiPort),
      MACHINE_CLIENT: 'protocol',
      PROTOCOL_HOST: '127.0.0.1',
      PROTOCOL_PORT: String(protocolPort),
      PLANT_HOST: '127.0.0.1',
      PLANT_PORT: String(plantPort),
    },
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  let log = '';
  const append = (d) => {
    log += d.toString();
  };
  child.stdout.on('data', append);
  child.stderr.on('data', append);
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
  return {
    base: `http://127.0.0.1:${guiPort}`,
    stop,
    getLog: () => log,
    child,
  };
}

function extractProcedureStartLines(log) {
  return String(log)
    .split(/\r?\n/)
    .filter((line) => line.includes('[ProcedureStart]'));
}

function summarizeMachine(status) {
  if (!status) return null;
  return {
    processState: status.processState,
    procedurePhase: status.procedurePhase,
    machineState: status.machineState,
    isHomed: status.isHomed,
    plateOriginValid: status.plateOriginValid,
    probeCenterValid: status.probeCenterValid,
    programPointCount: status.programPointCount,
    x: status.x,
    y: status.y,
    z: status.z,
  };
}

async function waitMachineOnline(base, timeoutMs = 25000) {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    try {
      const r = await request(base, 'GET', '/api/machine', null);
      if (r.status === 200 && r.body.status && r.body.status.connected === true) {
        return r.body.status;
      }
    } catch {
      // retry
    }
    await sleep(200);
  }
  throw new Error('machine not connected');
}

async function waitUntil(base, predicate, timeoutMs, label) {
  const start = Date.now();
  let last = null;
  while (Date.now() - start < timeoutMs) {
    const r = await request(base, 'GET', '/api/machine', null);
    last = r.body.status;
    if (predicate(last)) return last;
    await sleep(150);
  }
  throw new Error(`Timeout ${label}: last=${JSON.stringify(summarizeMachine(last))}`);
}

async function homeMachine(base) {
  const before = await request(base, 'GET', '/api/machine', null);
  const wasHomed = !!(before.body.status && before.body.status.isHomed);

  const home = await request(base, 'POST', '/api/command', { command: 'home' });
  if (!(home.status === 200 && home.body.ok)) {
    throw new Error(`home failed: ${JSON.stringify(home.body)}`);
  }

  // Avoid racing on a stale isHomed=true from before CmdHome took effect.
  if (wasHomed) {
    await waitUntil(
      base,
      (s) =>
        s &&
        (s.machineState === 'Homing' ||
          s.isHomed === false ||
          (s.isHomed === true && s.machineState !== 'Stopped')),
      15000,
      'homing started or left Stopped'
    ).catch(() => null);
  }

  return waitUntil(
    base,
    (s) =>
      s &&
      s.isHomed === true &&
      s.machineState !== 'Homing' &&
      s.machineState !== 'Stopped' &&
      s.processState !== 'Running',
    120000,
    'isHomed + not Homing/Stopped/Running'
  );
}

async function prepareProgram(base) {
  await request(base, 'POST', '/api/program/upload', {
    version: 1,
    programName: 'DiagE2E',
    testPoints: [
      { name: 'A', sequenceNumber: 1, x: 1, y: 0, z: 0 },
      { name: 'B', sequenceNumber: 2, x: 3, y: 0, z: 0 },
    ],
  });
  await request(base, 'PUT', '/api/program/selection', { selectAll: true });
  await request(base, 'POST', '/api/program/selection/confirm', null);
}

async function seedPersistedCalibration(base) {
  const plate = await request(base, 'POST', '/api/calibration/plate-origin', {
    x: 55,
    y: 57,
    z: 17,
  });
  if (!(plate.status === 200 && plate.body.ok)) {
    throw new Error(`plate-origin failed: ${JSON.stringify(plate.body)}`);
  }
  const calibGet = await request(base, 'GET', '/api/calibration', null);
  const calib = calibGet.body.calibration || {};
  if (!calib.probeCenterValid) {
    throw new Error(
      'Case A requires calibration.json probeCenterValid=true (reconnect rehydrate). ' +
        `Got probeCenterValid=${calib.probeCenterValid}`
    );
  }
  return calib;
}

async function runProcedureStart(base, label, gui) {
  const beforeLogLen = gui.getLog().length;
  const beforeMachine = await request(base, 'GET', '/api/machine', null);
  console.log(`\n=== ${label}: machine before start ===`);
  console.log(JSON.stringify(summarizeMachine(beforeMachine.body.status), null, 2));

  const start = await request(base, 'POST', '/api/procedure/start', {
    mode: 'test',
    insertionForce: 0.1,
    iterations: 1,
  });

  await sleep(400);
  const newLog = gui.getLog().slice(beforeLogLen);
  const lines = extractProcedureStartLines(newLog);

  console.log(`=== ${label}: HTTP result ===`);
  console.log(JSON.stringify({ status: start.status, body: start.body }, null, 2));
  console.log(`=== ${label}: [ProcedureStart] log lines ===`);
  if (lines.length === 0) {
    console.log('(none captured — check GUI log)');
    console.log(newLog.slice(-2500));
  } else {
    for (const line of lines) console.log(line);
  }

  const afterMachine = await request(base, 'GET', '/api/machine', null);
  console.log(`=== ${label}: machine after start ===`);
  console.log(JSON.stringify(summarizeMachine(afterMachine.body.status), null, 2));

  const failLine = lines.find((l) => l.includes('-> InvalidState') || l.includes('FAIL'));
  const firstFailCmd = (() => {
    const m = failLine && failLine.match(/Cmd\w+/);
    return m ? m[0] : null;
  })();

  return {
    start,
    lines,
    before: beforeMachine.body.status,
    after: afterMachine.body.status,
    firstFailCmd,
    failLine: failLine || null,
  };
}

async function runM19ToCompleted(base) {
  await waitUntil(
    base,
    (s) =>
      s &&
      s.isHomed === true &&
      s.machineState !== 'Homing' &&
      s.machineState !== 'Stopped' &&
      s.processState !== 'Running',
    30000,
    'ready for moveTo before M19'
  );

  const move = await request(base, 'POST', '/api/command', {
    command: 'moveTo',
    x: 40,
    y: 40,
    z: 10,
  });
  if (!(move.status === 200 && move.body.ok)) {
    throw new Error(`moveTo probe failed: ${JSON.stringify(move.body)}`);
  }
  // Wait until pose is actually near the probe disc — Idle/Active alone is not enough
  // if the previous pose was already Active.
  await waitUntil(
    base,
    (s) =>
      s &&
      s.machineState === 'Idle' &&
      Math.abs(Number(s.x) - 40) < 1.5 &&
      Math.abs(Number(s.y) - 40) < 1.5 &&
      Math.abs(Number(s.z) - 10) < 1.5,
    90000,
    'moveTo near probe disc'
  );
  await sleep(300);

  const before = await request(base, 'GET', '/api/machine', null);
  console.log('M19 before startProbeCalib:', JSON.stringify(summarizeMachine(before.body.status)));

  const probe = await request(base, 'POST', '/api/calibration/probe-start', null);
  console.log('M19 probe-start HTTP:', JSON.stringify(probe.body));
  if (!(probe.status === 200 && probe.body.ok)) {
    throw new Error(`probe-start failed: ${JSON.stringify(probe.body)}`);
  }

  const running = await waitUntil(
    base,
    (s) => s && (s.processState === 'Running' || s.processState === 'Completed'),
    15000,
    'M19 Running or Completed'
  );
  console.log('M19 after start:', JSON.stringify(summarizeMachine(running)));

  const done = await waitUntil(
    base,
    (s) => s && (s.processState === 'Completed' || s.processState === 'Aborted'),
    180000,
    'M19 terminal'
  );
  console.log('M19 terminal:', JSON.stringify(summarizeMachine(done)));
  return done;
}

async function withFreshStack(exe, runFn) {
  const protocolPort = await findFreePort();
  const plantPort = await findFreePort();
  const guiPort = await findFreePort();

  const host = startProtocolHost(exe, protocolPort, plantPort);
  await sleep(600);
  if (host.child.exitCode !== null) {
    throw new Error(`simulation_protocol exited: ${host.getStderr()}`);
  }

  const gui = startGuiServer({ protocolPort, plantPort, guiPort });
  await sleep(800);
  if (gui.child.exitCode !== null) {
    throw new Error(`GUI exited early: ${gui.getLog()}`);
  }

  try {
    await waitMachineOnline(gui.base);
    await prepareProgram(gui.base);
    return await runFn({ base: gui.base, gui, host });
  } finally {
    await gui.stop();
    await host.stop();
  }
}

function printCaseSummary(name, result) {
  if (!result) {
    console.log(`${name}: not run`);
    return;
  }
  console.log(
    `${name}: HTTP ok=${result.start && result.start.body && result.start.body.ok} ` +
      `status=${result.start && result.start.status}`
  );
  console.log(`  before.processState=${result.before && result.before.processState}`);
  console.log(`  firstFailCmd=${result.firstFailCmd || '(none)'}`);
  console.log(`  first failure line: ${result.failLine || '(none)'}`);
  const okLine = (result.lines || []).find((l) => l.includes('chain complete Ok'));
  console.log(`  success line: ${okLine || '(none)'}`);
}

async function main() {
  const exe = resolveProtocolHost();
  if (!exe) {
    console.error('simulation_protocol.exe not found — build first');
    process.exit(1);
  }
  console.log('Using host:', exe);

  const only = String(process.env.DIAG_CASE || 'ABC').toUpperCase();
  const report = { caseA: null, caseB: null, caseC: null };

  // ---------- Case A: fresh stack, no M19 ----------
  if (only.includes('A')) {
  console.log('\n######## CASE A: no M19 (persisted probe_center) ########');
  report.caseA = await withFreshStack(exe, async ({ base, gui, host }) => {
    await homeMachine(base);
    const calibA = await seedPersistedCalibration(base);
    console.log('Case A calib store probeCenterValid=', calibA.probeCenterValid);
    await sleep(500);
    const result = await runProcedureStart(base, 'CASE A', gui);
    const simHits = String(host.getStderr())
      .split(/\r?\n/)
      .filter((l) => l.includes('[CmdStart]') || l.includes('[ProcedureController::start]'));
    console.log('=== CASE A simulation stderr hits ===');
    if (simHits.length === 0) console.log('(none)');
    else simHits.forEach((l) => console.log(l));
    return result;
  });
  }

  // ---------- Case B: fresh stack, M19 Completed then start ----------
  if (only.includes('B')) {
  console.log('\n######## CASE B: after M19 Completed ########');
  report.caseB = await withFreshStack(exe, async ({ base, gui, host }) => {
    await homeMachine(base);
    await seedPersistedCalibration(base);
    const m19 = await runM19ToCompleted(base);
    if (m19.processState !== 'Completed') {
      console.error('CASE B: M19 did not reach Completed — still attempting start for diagnosis');
    }
    const preB = await request(base, 'GET', '/api/machine', null);
    console.log('CASE B pre-start:', JSON.stringify(summarizeMachine(preB.body.status)));
    const result = await runProcedureStart(base, 'CASE B', gui);
    const simHits = String(host.getStderr())
      .split(/\r?\n/)
      .filter((l) => l.includes('[CmdStart]') || l.includes('[ProcedureController::start]'));
    console.log('=== CASE B simulation stderr hits ===');
    if (simHits.length === 0) console.log('(none)');
    else simHits.forEach((l) => console.log(l));
    result.m19Terminal = summarizeMachine(m19);
    return result;
  });
  }

  // ---------- Case C: start while M19 still Running ----------
  if (only.includes('C')) {
  console.log('\n######## CASE C: start while M19 Running (expected InvalidState) ########');
  report.caseC = await withFreshStack(exe, async ({ base, gui, host }) => {
    await homeMachine(base);
    await seedPersistedCalibration(base);
    await waitUntil(
      base,
      (s) =>
        s &&
        s.isHomed === true &&
        s.machineState !== 'Homing' &&
        s.machineState !== 'Stopped' &&
        s.processState !== 'Running',
      30000,
      'ready for Case C moveTo'
    );
    const move = await request(base, 'POST', '/api/command', {
      command: 'moveTo',
      x: 40,
      y: 40,
      z: 10,
    });
    if (!(move.status === 200 && move.body.ok)) {
      throw new Error(`Case C moveTo failed: ${JSON.stringify(move.body)}`);
    }
    await waitUntil(
      base,
      (s) =>
        s &&
        s.machineState === 'Idle' &&
        Math.abs(Number(s.x) - 40) < 1.5 &&
        Math.abs(Number(s.y) - 40) < 1.5 &&
        Math.abs(Number(s.z) - 10) < 1.5,
      90000,
      'Case C moveTo near probe'
    );
    const probe = await request(base, 'POST', '/api/calibration/probe-start', null);
    if (!(probe.status === 200 && probe.body.ok)) {
      throw new Error(`Case C probe-start failed: ${JSON.stringify(probe.body)}`);
    }
    const running = await waitUntil(
      base,
      (s) => s && s.processState === 'Running',
      15000,
      'Case C M19 Running'
    );
    console.log('CASE C M19 Running confirmed:', JSON.stringify(summarizeMachine(running)));
    const result = await runProcedureStart(base, 'CASE C', gui);
    const simHits = String(host.getStderr())
      .split(/\r?\n/)
      .filter(
        (l) =>
          l.includes('[CmdStart]') ||
          l.includes('[ProcedureController::start]') ||
          l.includes('InvalidState')
      );
    console.log('=== CASE C simulation stderr hits ===');
    if (simHits.length === 0) console.log('(none)');
    else simHits.forEach((l) => console.log(l));
    result.m19AtFail = summarizeMachine(running);
    return result;
  });
  }

  console.log('\n======== SUMMARY ========');
  printCaseSummary('Case A (no M19)', report.caseA);
  printCaseSummary('Case B (after M19 Completed)', report.caseB);
  printCaseSummary('Case C (during M19 Running)', report.caseC);
}

main().catch((err) => {
  console.error('procedure_start_e2e_diagnose FAIL:', err.message || err);
  process.exit(1);
});
