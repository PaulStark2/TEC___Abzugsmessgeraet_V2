'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');
const { WebSocketServer } = require('ws');
const { createMachineClient } = require('./machineClient');
const { validateTestProgram } = require('./program/validateTestProgram');
const programStore = require('./program/programStore');

const PORT = Number(process.env.PORT) || 3000;
const PUBLIC_DIR = path.join(__dirname, '..', 'public');
const THREE_DIR = path.join(__dirname, '..', 'node_modules', 'three');
const STATUS_HZ = 5;
const STATUS_INTERVAL_MS = 1000 / STATUS_HZ;
const TICK_HZ = 20;
const TICK_INTERVAL_MS = 1000 / TICK_HZ;

const MIME_TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'application/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.ico': 'image/x-icon',
};

function contentTypeFor(filePath) {
  const ext = path.extname(filePath).toLowerCase();
  return MIME_TYPES[ext] || 'application/octet-stream';
}

function isPathInside(rootDir, candidate) {
  const rel = path.relative(rootDir, candidate);
  return Boolean(rel) && !rel.startsWith('..') && !path.isAbsolute(rel);
}

function resolvePublicPath(urlPath) {
  const decoded = decodeURIComponent(urlPath.split('?')[0]);

  // Serve local npm three package offline (no CDN) under /vendor/three/
  if (decoded === '/vendor/three' || decoded.startsWith('/vendor/three/')) {
    const relative = decoded === '/vendor/three' ? '/' : decoded.slice('/vendor/three'.length);
    const absolute = path.normalize(path.join(THREE_DIR, relative));
    if (!isPathInside(THREE_DIR, absolute) && absolute !== THREE_DIR) {
      return null;
    }
    return absolute;
  }

  const relative = decoded === '/' ? '/index.html' : decoded;
  const absolute = path.normalize(path.join(PUBLIC_DIR, relative));

  if (!isPathInside(PUBLIC_DIR, absolute) && absolute !== PUBLIC_DIR) {
    return null;
  }

  return absolute;
}

function readJsonBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    req.on('data', (chunk) => chunks.push(chunk));
    req.on('end', () => {
      const raw = Buffer.concat(chunks).toString('utf8');
      if (!raw) {
        resolve({});
        return;
      }
      try {
        resolve(JSON.parse(raw));
      } catch (err) {
        reject(err);
      }
    });
    req.on('error', reject);
  });
}

function sendJson(res, statusCode, body) {
  const payload = JSON.stringify(body);
  res.writeHead(statusCode, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(payload),
  });
  res.end(payload);
}

/**
 * Dispatch a command object to the MachineClient.
 * @returns {{ ok: boolean, status?: object, error?: string }}
 */
function handleCommand(body) {
  const command = body && body.command;
  if (typeof command !== 'string') {
    return { ok: false, error: 'Missing command' };
  }

  switch (command) {
    case 'moveTo':
      if (typeof machineClient.moveTo !== 'function') {
        return { ok: false, error: 'moveTo not supported by client' };
      }
      return machineClient.moveTo(body.x, body.y, body.z);
    case 'pause':
      if (typeof machineClient.pause !== 'function') {
        return { ok: false, error: 'pause not supported by client' };
      }
      return machineClient.pause();
    case 'resume':
      if (typeof machineClient.resume !== 'function') {
        return { ok: false, error: 'resume not supported by client' };
      }
      return machineClient.resume();
    case 'stop':
      if (typeof machineClient.stop !== 'function') {
        return { ok: false, error: 'stop not supported by client' };
      }
      return machineClient.stop();
    default:
      return { ok: false, error: `Unknown command: ${command}` };
  }
}

const machineClient = createMachineClient();

function buildSummaryFromProgram(normalizedProgram) {
  const testPoints = normalizedProgram.testPoints || [];
  const xs = testPoints.map((p) => p.x);
  const ys = testPoints.map((p) => p.y);
  const zs = testPoints.map((p) => p.z);

  const previewCount = Math.min(5, testPoints.length);

  return {
    programName: normalizedProgram.programName ?? null,
    version: normalizedProgram.version,
    count: testPoints.length,
    pointsPreview: testPoints.slice(0, previewCount).map((p) => ({
      name: p.name,
      sequenceNumber: p.sequenceNumber,
      x: p.x,
      y: p.y,
      z: p.z,
    })),
    xRange: { min: Math.min(...xs), max: Math.max(...xs) },
    yRange: { min: Math.min(...ys), max: Math.max(...ys) },
    zRange: { min: Math.min(...zs), max: Math.max(...zs) },
  };
}

const server = http.createServer(async (req, res) => {
  const urlPath = (req.url || '/').split('?')[0];

  if (req.method === 'POST' && urlPath === '/api/command') {
    try {
      const body = await readJsonBody(req);
      const result = handleCommand(body);
      broadcastStatus();
      sendJson(res, result.ok ? 200 : 400, result);
    } catch {
      sendJson(res, 400, { ok: false, error: 'Invalid JSON body' });
    }
    return;
  }

  if (req.method === 'POST' && urlPath === '/api/program/upload') {
    try {
      const body = await readJsonBody(req);
      const validation = validateTestProgram(body);

      if (!validation.ok) {
        // Invalid upload must not overwrite store.
        sendJson(res, 400, { ok: false, errors: validation.errors });
        return;
      }

      programStore.setProgram(validation.value);
      sendJson(res, 200, { ok: true, summary: buildSummaryFromProgram(validation.value) });
      return;
    } catch {
      sendJson(res, 400, { ok: false, errors: [{ path: '', message: 'Invalid JSON body' }] });
      return;
    }
  }

  if (req.method === 'GET' && urlPath === '/api/program/current') {
    const current = programStore.getProgram();
    if (!current) {
      sendJson(res, 404, { ok: false, error: 'No program loaded' });
      return;
    }

    sendJson(res, 200, {
      ok: true,
      current: {
        meta: {
          programName: current.programName ?? null,
          version: current.version,
        },
        testPoints: current.testPoints,
      },
    });
    return;
  }

  if (req.method !== 'GET' && req.method !== 'HEAD') {
    res.writeHead(405, { 'Content-Type': 'text/plain; charset=utf-8' });
    res.end('Method Not Allowed');
    return;
  }

  const filePath = resolvePublicPath(req.url || '/');
  if (!filePath) {
    res.writeHead(403, { 'Content-Type': 'text/plain; charset=utf-8' });
    res.end('Forbidden');
    return;
  }

  fs.readFile(filePath, (err, data) => {
    if (err) {
      res.writeHead(err.code === 'ENOENT' ? 404 : 500, {
        'Content-Type': 'text/plain; charset=utf-8',
      });
      res.end(err.code === 'ENOENT' ? 'Not Found' : 'Internal Server Error');
      return;
    }

    res.writeHead(200, { 'Content-Type': contentTypeFor(filePath) });
    if (req.method === 'HEAD') {
      res.end();
      return;
    }
    res.end(data);
  });
});

const wss = new WebSocketServer({ server, path: '/ws/status' });

function buildStatusMessage() {
  const status = machineClient.getStatus();
  return JSON.stringify({
    type: 'status',
    ...status,
  });
}

function broadcastStatus() {
  const payload = buildStatusMessage();
  for (const client of wss.clients) {
    if (client.readyState === 1) {
      client.send(payload);
    }
  }
}

wss.on('connection', (socket) => {
  socket.send(buildStatusMessage());
});

setInterval(() => {
  if (typeof machineClient.tick === 'function') {
    machineClient.tick(TICK_INTERVAL_MS / 1000);
  }
}, TICK_INTERVAL_MS);

setInterval(broadcastStatus, STATUS_INTERVAL_MS);

server.listen(PORT, () => {
  const clientKind = (process.env.MACHINE_CLIENT || 'mock').toLowerCase();
  console.log(`GUI server listening on http://localhost:${PORT}`);
  console.log(`Machine client: ${clientKind}`);
  console.log(`Status WebSocket: ws://localhost:${PORT}/ws/status (~${STATUS_HZ} Hz)`);
  console.log('Command API: POST /api/command');
});
