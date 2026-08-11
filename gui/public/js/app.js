'use strict';

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
}

function setGuiStatus(text, isError) {
  const el = $('gui-status');
  if (!el) return;
  el.textContent = text;
  el.classList.toggle('gui-status--error', Boolean(isError));
}

function connectStatusSocket() {
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  const url = `${protocol}//${window.location.host}/ws/status`;
  const socket = new WebSocket(url);

  socket.addEventListener('open', () => {
    setGuiStatus('GUI läuft — Live-Status verbunden (G2 Mock)', false);
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
    // close handler performs reconnect
    socket.close();
  });
}

document.addEventListener('DOMContentLoaded', () => {
  setGuiStatus('GUI läuft — verbinde Live-Status…', false);
  connectStatusSocket();
});
