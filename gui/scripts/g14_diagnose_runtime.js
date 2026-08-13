'use strict';

/**
 * Diagnose which GUI MachineClient is answering on :3000 and which hosts hold :8766.
 *
 * Usage:
 *   node scripts/g14_diagnose_runtime.js
 */

const http = require('http');
const { execSync } = require('child_process');

function getJson(path) {
  return new Promise((resolve, reject) => {
    http
      .get(`http://127.0.0.1:3000${path}`, (res) => {
        const chunks = [];
        res.on('data', (c) => chunks.push(c));
        res.on('end', () => {
          const text = Buffer.concat(chunks).toString('utf8');
          try {
            resolve({ status: res.statusCode, body: JSON.parse(text) });
          } catch {
            resolve({ status: res.statusCode, body: text });
          }
        });
      })
      .on('error', reject);
  });
}

function postCommand(body) {
  const payload = JSON.stringify(body);
  return new Promise((resolve, reject) => {
    const req = http.request(
      {
        hostname: '127.0.0.1',
        port: 3000,
        path: '/api/command',
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          'Content-Length': Buffer.byteLength(payload),
        },
      },
      (res) => {
        const chunks = [];
        res.on('data', (c) => chunks.push(c));
        res.on('end', () => {
          const text = Buffer.concat(chunks).toString('utf8');
          let json;
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
    req.write(payload);
    req.end();
  });
}

function safeExec(cmd) {
  try {
    return execSync(cmd, { encoding: 'utf8' });
  } catch (err) {
    return String(err.stdout || err.message || err);
  }
}

async function main() {
  console.log('=== Runtime diagnose (port 3000 / 8766) ===\n');

  console.log('--- netstat :3000 / :8766 ---');
  console.log(safeExec('netstat -ano | findstr ":3000"'));
  console.log(safeExec('netstat -ano | findstr ":8766"'));

  console.log('--- GET /api/machine (requires restarted GUI with M14 diagnose) ---');
  try {
    const machine = await getJson('/api/machine');
    console.log(JSON.stringify(machine, null, 2));
  } catch (err) {
    console.log(' /api/machine not reachable or old server without endpoint:', err.message);
  }

  console.log('\n--- POST /api/command moveTo fingerprint ---');
  const cmd = await postCommand({ command: 'moveTo', x: 0, y: 0, z: 0 });
  console.log(JSON.stringify(cmd, null, 2));

  const status = cmd.body && cmd.body.status;
  if (status) {
    const looksProtocol =
      Object.prototype.hasOwnProperty.call(status, 'connected') ||
      Object.prototype.hasOwnProperty.call(status, 'processState');
    const looksMock = Object.prototype.hasOwnProperty.call(status, 'manualSpeed');
    console.log('\nFingerprint:');
    console.log('  looksProtocol=', looksProtocol);
    console.log('  looksMock=', looksMock);
    console.log('  machineClientMode=', cmd.body.machineClientMode || status.machineClientMode || '(not stamped — restart GUI)');
    console.log('  implementation=', cmd.body.implementation || status.implementation || '(not stamped — restart GUI)');
    console.log('  connected=', status.connected);
    console.log('  hasTick field=', status.hasTick);
  }

  console.log(`
If move still works after you think simulation_protocol is dead:
1) List ALL hosts:  Get-CimInstance Win32_Process -Filter "Name='simulation_protocol.exe'"
2) List GUI:        Get-CimInstance Win32_Process | ? { $_.CommandLine -match 'server/index.js' }
3) Stale GUI on :3000 blocks new npm start (EADDRINUSE) — browser keeps talking to the old process.
4) Stale simulation_protocol on :8766 keeps answering M12 ACKs → visualization keeps moving.
`);
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
