'use strict';

/**
 * HTTP command + WebSocket status (G2/G3). Browser does not simulate motion.
 */

let statusCallback = null;
let connectionCallback = null;
let reconnectTimer = null;

/**
 * @param {object} body
 */
export async function sendCommand(body) {
  const response = await fetch('/api/command', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  let result;
  try {
    result = await response.json();
  } catch {
    throw new Error('Ungültige Serverantwort');
  }
  if (!response.ok || !result.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  if (result.status && statusCallback) {
    statusCallback(result.status);
  }
  return result;
}

/**
 * @param {(status: object) => void} onStatus
 * @param {(connected: boolean, message?: string) => void} [onConnection]
 */
export function connectStatus(onStatus, onConnection) {
  statusCallback = onStatus;
  connectionCallback = onConnection;

  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  const url = `${protocol}//${window.location.host}/ws/status`;
  const socket = new WebSocket(url);

  socket.addEventListener('open', () => {
    if (connectionCallback) {
      connectionCallback(true, 'GUI läuft — Live-Status verbunden');
    }
  });

  socket.addEventListener('message', (event) => {
    let message;
    try {
      message = JSON.parse(event.data);
    } catch {
      return;
    }
    if (message.type !== 'status' || !statusCallback) {
      return;
    }
    statusCallback(message);
  });

  socket.addEventListener('close', () => {
    if (connectionCallback) {
      connectionCallback(false, 'Live-Status getrennt — reconnect in 2s…');
    }
    if (reconnectTimer) {
      clearTimeout(reconnectTimer);
    }
    reconnectTimer = window.setTimeout(() => {
      reconnectTimer = null;
      connectStatus(onStatus, onConnection);
    }, 2000);
  });

  socket.addEventListener('error', () => {
    socket.close();
  });
}
