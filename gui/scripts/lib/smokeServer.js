'use strict';

const http = require('http');
const path = require('path');
const { spawn } = require('child_process');
const net = require('net');

/**
 * Allocate an unused TCP port on 127.0.0.1.
 * @returns {Promise<number>}
 */
function findFreePort() {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.listen(0, '127.0.0.1', () => {
      const address = server.address();
      const port = typeof address === 'object' && address ? address.port : 0;
      server.close((err) => {
        if (err) reject(err);
        else resolve(port);
      });
    });
    server.on('error', reject);
  });
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

/**
 * Start an isolated GUI server (fresh RAM stores) for smoke tests.
 * @param {{ port?: number, timeoutMs?: number }} [options]
 * @returns {Promise<{ base: string, port: number, request: Function, stop: () => Promise<void> }>}
 */
async function startSmokeServer(options = {}) {
  const port = options.port || (await findFreePort());
  const timeoutMs = options.timeoutMs || 20000;
  const machineClient = options.machineClient || 'mock';
  const base = `http://127.0.0.1:${port}`;
  const serverEntry = path.join(__dirname, '..', '..', 'server', 'index.js');

  const child = spawn(process.execPath, [serverEntry], {
    env: {
      ...process.env,
      PORT: String(port),
      MACHINE_CLIENT: machineClient,
      ...(options.env || {}),
    },
    stdio: ['ignore', 'pipe', 'pipe'],
  });

  let stderr = '';
  child.stderr.on('data', (chunk) => {
    stderr += chunk.toString();
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
        if (child.exitCode === null) {
          child.kill('SIGKILL');
        }
        resolve();
      }, 2000);
    });

  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    if (child.exitCode !== null) {
      throw new Error(`Smoke server exited early (code ${child.exitCode}): ${stderr || '(no stderr)'}`);
    }
    try {
      const r = await request(base, 'GET', '/', null);
      if (r.status === 200) {
        return {
          base,
          port,
          request: (method, urlPath, body) => request(base, method, urlPath, body),
          stop,
        };
      }
    } catch {
      // not ready yet
    }
    await new Promise((r) => setTimeout(r, 100));
  }

  await stop();
  throw new Error(`Smoke server did not become reachable on ${base}`);
}

module.exports = {
  startSmokeServer,
  findFreePort,
};
