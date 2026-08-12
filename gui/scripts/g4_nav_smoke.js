'use strict';

/**
 * G4 navigation smoke — static assets + WS status (server must be running).
 * Usage: node scripts/g4_nav_smoke.js
 */

const http = require('http');
const WebSocket = require('ws');

const BASE = 'http://127.0.0.1:3000';

const ASSETS = [
  '/',
  '/css/app.css',
  '/js/app.js',
  '/js/router.js',
  '/js/api.js',
  '/js/statusBar.js',
  '/js/tcpView.js',
  '/js/pages/menu.js',
  '/js/pages/manual.js',
];

function request(path) {
  return new Promise((resolve, reject) => {
    http.get(new URL(path, BASE), (res) => {
      const chunks = [];
      res.on('data', (c) => chunks.push(c));
      res.on('end', () => {
        resolve({ status: res.statusCode, body: Buffer.concat(chunks).toString('utf8') });
      });
    }).on('error', reject);
  });
}

async function run() {
  for (const path of ASSETS) {
    const r = await request(path);
    if (r.status !== 200) {
      throw new Error(`${path} returned ${r.status}`);
    }
  }

  const html = await request('/');
  if (!html.body.includes('id="page-root"')) {
    throw new Error('Shell page-root missing');
  }
  if (!html.body.includes('id="tcp-canvas"')) {
    throw new Error('Persistent tcp-canvas missing');
  }
  if (!html.body.includes('btn-stop-global')) {
    throw new Error('Global STOP missing');
  }
  if (!html.body.includes('force-fx')) {
    throw new Error('Force placeholder missing');
  }

  const wsMsg = await new Promise((resolve, reject) => {
    const ws = new WebSocket('ws://127.0.0.1:3000/ws/status');
    const t = setTimeout(() => {
      ws.close();
      reject(new Error('WS timeout'));
    }, 3000);
    ws.on('message', (d) => {
      clearTimeout(t);
      ws.close();
      resolve(JSON.parse(d.toString()));
    });
    ws.on('error', reject);
  });

  if (wsMsg.type !== 'status' || !wsMsg.machineState) {
    throw new Error('Invalid WS status shape');
  }

  console.log('G4 nav smoke: all assets OK, WS status OK');
}

run().catch((err) => {
  console.error('FAIL:', err.message);
  process.exit(1);
});
