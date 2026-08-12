'use strict';

import { setupTcpView, setTcpPose } from './tcpView.js';

function $(id) {
  return document.getElementById(id);
}

function formatPose(value) {
  return Number(value).toFixed(2);
}

function applyStatus(status) {
  const machineEl = $('machine-state');
  const motionEl = $('motion-state');
  const xEl = $('pos-x');
  const yEl = $('pos-y');
  const zEl = $('pos-z');

  if (machineEl) machineEl.textContent = status.machineState;
  if (motionEl) motionEl.textContent = status.motionState;
  if (xEl) xEl.textContent = formatPose(status.x);
  if (yEl) yEl.textContent = formatPose(status.y);
  if (zEl) zEl.textContent = formatPose(status.z);

  // Display-only: set Three.js TCP from Core pose (no interpolation).
  setTcpPose(status);
}

function setGuiStatus(text, isError) {
  const el = $('gui-status');
  if (!el) return;
  el.textContent = text;
  el.classList.toggle('gui-status--error', Boolean(isError));
}

function setCommandFeedback(text, isError) {
  const el = $('command-feedback');
  if (!el) return;
  el.textContent = text || '';
  el.classList.toggle('command-feedback--error', Boolean(isError));
}

/**
 * Send a command to the server. Browser does not simulate motion.
 * @param {object} body
 */
async function sendCommand(body) {
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
  if (result.status) {
    applyStatus(result.status);
  }
  return result;
}

function wireControls() {
  const form = $('move-form');
  if (form) {
    form.addEventListener('submit', async (event) => {
      event.preventDefault();
      const x = Number($('target-x').value);
      const y = Number($('target-y').value);
      const z = Number($('target-z').value);
      try {
        await sendCommand({ command: 'moveTo', x, y, z });
        setCommandFeedback(`moveTo(${x}, ${y}, ${z}) gesendet`, false);
      } catch (err) {
        setCommandFeedback(err.message || String(err), true);
      }
    });
  }

  const pauseBtn = $('btn-pause');
  if (pauseBtn) {
    pauseBtn.addEventListener('click', async () => {
      try {
        await sendCommand({ command: 'pause' });
        setCommandFeedback('pause gesendet', false);
      } catch (err) {
        setCommandFeedback(err.message || String(err), true);
      }
    });
  }

  const resumeBtn = $('btn-resume');
  if (resumeBtn) {
    resumeBtn.addEventListener('click', async () => {
      try {
        await sendCommand({ command: 'resume' });
        setCommandFeedback('resume gesendet', false);
      } catch (err) {
        setCommandFeedback(err.message || String(err), true);
      }
    });
  }

  const stopBtn = $('btn-stop');
  if (stopBtn) {
    stopBtn.addEventListener('click', async () => {
      try {
        await sendCommand({ command: 'stop' });
        setCommandFeedback('STOP gesendet (Soft-Stop)', false);
      } catch (err) {
        setCommandFeedback(err.message || String(err), true);
      }
    });
  }
}

function connectStatusSocket() {
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  const url = `${protocol}//${window.location.host}/ws/status`;
  const socket = new WebSocket(url);

  socket.addEventListener('open', () => {
    setGuiStatus('GUI läuft — Live-Status verbunden', false);
  });

  socket.addEventListener('message', (event) => {
    let message;
    try {
      message = JSON.parse(event.data);
    } catch {
      return;
    }

    if (message.type !== 'status') {
      return;
    }

    applyStatus(message);
  });

  socket.addEventListener('close', () => {
    setGuiStatus('Live-Status getrennt — reconnect in 2s…', true);
    window.setTimeout(connectStatusSocket, 2000);
  });

  socket.addEventListener('error', () => {
    socket.close();
  });
}

document.addEventListener('DOMContentLoaded', () => {
  setGuiStatus('GUI läuft — verbinde Live-Status…', false);
  setupTcpView();
  wireControls();
  connectStatusSocket();
});
