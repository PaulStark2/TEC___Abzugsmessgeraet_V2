'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');
const { WebSocketServer } = require('ws');
const { createMachineClient } = require('./machineClient');

const PORT = Number(process.env.PORT) || 3000;
const PUBLIC_DIR = path.join(__dirname, '..', 'public');
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

function resolvePublicPath(urlPath) {
  const decoded = decodeURIComponent(urlPath.split('?')[0]);
  const relative = decoded === '/' ? '/index.html' : decoded;
  const absolute = path.normalize(path.join(PUBLIC_DIR, relative));

  if (!absolute.startsWith(PUBLIC_DIR)) {
    return null;
  }

  return absolute;
}

const machineClient = createMachineClient();

const server = http.createServer((req, res) => {
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
});
