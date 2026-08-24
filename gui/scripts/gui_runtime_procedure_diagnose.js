'use strict';

/**
 * Runtime diagnosis against the same simulation_protocol.exe the GUI starts.
 * Proves SIM_BUILD banner + Phase transitions for 1 point / 25 N / Test / 3 iters.
 */

const path = require('path');
const fs = require('fs');
const net = require('net');
const { spawn } = require('child_process');
const { findFreePort } = require('./lib/smokeServer');

const root = path.join(__dirname, '..', '..');
const exe = path.join(root, 'build', 'simulation_protocol', 'simulation_protocol.exe');

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

async function waitPort(port, timeoutMs = 10000) {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    try {
      await new Promise((resolve, reject) => {
        const s = net.connect({ host: '127.0.0.1', port }, () => {
          s.end();
          resolve();
        });
        s.on('error', reject);
      });
      return;
    } catch {
      await sleep(40);
    }
  }
  throw new Error(`port ${port} not ready`);
}

function summarize(s) {
  if (!s) return null;
  return {
    machineState: s.machineState,
    processState: s.processState,
    procedurePhase: s.procedurePhase,
    homingState: s.homingState,
    isHomed: s.isHomed,
    x: s.x,
    y: s.y,
    z: s.z,
    fx: s.fx,
    fy: s.fy,
    fz: s.fz,
    spreaderExtended: s.spreaderExtended,
    plateOriginValid: s.plateOriginValid,
    probeCenterValid: s.probeCenterValid,
    programPointCount: s.programPointCount,
  };
}

async function main() {
  console.log('=== GUI-path binary diagnosis ===');
  if (!fs.existsSync(exe)) {
    console.error('MISSING', exe);
    process.exit(2);
  }
  const st = fs.statSync(exe);
  console.log('exe=', exe);
  console.log('mtime=', st.mtime.toISOString());
  console.log('size=', st.size);

  const protocolPort = await findFreePort();
  const plantPort = await findFreePort();
  console.log('ports protocol=', protocolPort, 'plant=', plantPort);

  let stdout = '';
  let stderr = '';
  const child = spawn(exe, [String(protocolPort), String(plantPort)], {
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  child.stdout.on('data', (d) => {
    stdout += d.toString();
  });
  child.stderr.on('data', (d) => {
    stderr += d.toString();
  });

  const startDeadline = Date.now() + 10000;
  while (Date.now() < startDeadline) {
    if ((stdout + stderr).includes('Listening on') || (stdout + stderr).includes('[SIM_BUILD]')) {
      break;
    }
    if (child.exitCode !== null) {
      console.error('host exited early', stdout, stderr);
      process.exit(3);
    }
    await sleep(40);
  }

  const combinedStart = stdout + stderr;
  const hasBanner = combinedStart.includes('[SIM_BUILD] procedure-force-model-v3');
  console.log('has_SIM_BUILD_banner=', hasBanner);
  if (!hasBanner) {
    console.error('STALE BINARY — rebuild simulation_protocol');
    console.error(combinedStart);
    child.kill();
    process.exit(4);
  }

  await waitPort(protocolPort);

  const { createProtocolMachineClient } = require(path.join(
    __dirname,
    '..',
    'server',
    'protocol',
    'protocolMachineClient.js'
  ));
  const client = createProtocolMachineClient({ host: '127.0.0.1', port: protocolPort });
  await new Promise((resolve, reject) => {
    const t = setTimeout(() => reject(new Error('session ready timeout')), 15000);
    client.onSessionReady(() => {
      clearTimeout(t);
      resolve();
    });
  });

  console.log('pre-home status=', JSON.stringify(summarize(client.getStatus())));

  const home = await client.home();
  console.log('home ack=', home && home.ok, home && home.error);

  let status = client.getStatus();
  const homeDeadline = Date.now() + 120000;
  while (Date.now() < homeDeadline) {
    status = client.getStatus();
    if (
      status.isHomed === true &&
      status.machineState !== 'Homing' &&
      status.machineState !== 'Stopped'
    ) {
      break;
    }
    await sleep(100);
  }
  console.log('post-home status=', JSON.stringify(summarize(status)));
  if (!status.isHomed) {
    console.error('Homing did not complete');
    console.error('--- stderr tail ---\n' + stderr.slice(-4000));
    client.close();
    child.kill();
    process.exit(5);
  }

  const origin = await client.setPlateOrigin(55, 57, 17);
  const probe = await client.setProbeCenter(40, 40, 10);
  console.log('setPlateOrigin=', origin && origin.ok, 'setProbeCenter=', probe && probe.ok);

  const cfgRes = await client.setTestConfiguration({
    mode: 0,
    target_force_N: 25,
    iterations: 3,
    active_point_index: 0,
  });
  console.log('setTestConfiguration=', cfgRes && cfgRes.ok, cfgRes && cfgRes.error);

  const load = await client.loadProgram([{ x: 5, y: 2, z: 0 }]);
  console.log('loadProgram=', load && load.ok, load && load.error);

  status = client.getStatus();
  console.log('pre-start status=', JSON.stringify(summarize(status)));

  const start = await client.start();
  console.log('start=', start && start.ok, start && start.error);
  if (!start || !start.ok) {
    const hits = (stderr + stdout)
      .split(/\r?\n/)
      .filter((l) => /CmdStart|ProcedureController::start|reject/.test(l));
    console.error('start reject logs:\n' + hits.join('\n'));
    client.close();
    child.kill();
    process.exit(6);
  }

  const phaseHist = [];
  let lastPhase = null;
  let lastZBucket = null;
  const zTrace = [];
  const runDeadline = Date.now() + 300000;
  while (Date.now() < runDeadline) {
    status = client.getStatus();
    const phase = status.procedurePhase;
    const zBucket = status.z == null ? null : Math.round(Number(status.z) * 10) / 10;
    if (phase && phase !== lastPhase) {
      const row = {
        phase,
        processState: status.processState,
        z: status.z,
        fz: status.fz,
        spreaderExtended: status.spreaderExtended,
      };
      phaseHist.push(row);
      console.log(
        `[phase] ${phase} z=${Number(status.z).toFixed(3)} fz=${Number(status.fz).toFixed(2)} spr=${status.spreaderExtended}`
      );
      lastPhase = phase;
    }
    if (zBucket !== lastZBucket && (phase === 'InsertInitial' || phase === 'OptimizeInsert' || phase === 'TestOrAge')) {
      zTrace.push({
        phase,
        z: status.z,
        fz: status.fz,
        spr: status.spreaderExtended,
      });
      if (zTrace.length <= 40 || zTrace.length % 5 === 0) {
        console.log(
          `[z] ${phase} z=${Number(status.z).toFixed(3)} fz=${Number(status.fz).toFixed(2)}`
        );
      }
      lastZBucket = zBucket;
    }
    if (status.processState === 'Completed' || status.processState === 'Aborted') {
      break;
    }
    await sleep(50);
  }

  console.log('final=', JSON.stringify(summarize(status)));
  console.log('phaseHistory=', JSON.stringify(phaseHist, null, 2));
  console.log('zTrace(first40)=', JSON.stringify(zTrace.slice(0, 40), null, 2));

  const interesting = (stderr + stdout)
    .split(/\r?\n/)
    .filter((l) =>
      /InsertInitial|OptimizeInsert|TestOrAge|enterPhase|SIM_BUILD|force-free|start OUT|start IN|cycle_leg|contact ok|target_force|CmdSetTestConfiguration/.test(
        l
      )
    );
  console.log('--- host logs (filtered) ---');
  console.log(interesting.join('\n'));

  try {
    client.close();
  } catch {
    /* ignore */
  }
  child.kill();

  const sawInsertInitial = phaseHist.some((p) => p.phase === 'InsertInitial');
  const insertEntry = phaseHist.find((p) => p.phase === 'InsertInitial');
  const insertZTrace = zTrace.filter((r) => r.phase === 'InsertInitial');
  let firstDz = null;
  if (insertZTrace.length >= 2) {
    firstDz = insertZTrace[1].z - insertZTrace[0].z;
  }
  console.log('sawInsertInitial=', sawInsertInitial);
  console.log('insertEntry=', JSON.stringify(insertEntry));
  console.log('firstInsertInitial_dz=', firstDz);

  // Success criteria for diagnosis with fresh binary: reach InsertInitial and first Z move is +Z.
  if (!sawInsertInitial) {
    process.exit(7);
  }
  if (firstDz != null && firstDz < -0.05) {
    console.error('FIRST_WRONG_MOVE: InsertInitial moved -Z first (dz=' + firstDz + ')');
    process.exit(8);
  }
  console.log('diag_ok: InsertInitial reached; first dz not negative');
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
