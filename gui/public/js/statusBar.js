'use strict';

import { sendCommand } from './api.js';
import { setTcpPose } from './tcpView.js';

function $(id) {
  return document.getElementById(id);
}

function formatPose(value) {
  return Number(value).toFixed(2);
}

function formatForce(value) {
  if (value === undefined || value === null || !Number.isFinite(Number(value))) {
    return '—';
  }
  return Number(value).toFixed(2);
}

function readForce(status, keys) {
  for (const key of keys) {
    if (status[key] !== undefined && status[key] !== null) {
      const n = Number(status[key]);
      if (Number.isFinite(n)) {
        return n;
      }
    }
  }
  return undefined;
}

/**
 * Update global status bar and 3D TCP (display-only).
 * @param {object} status
 */
export function applyStatus(status) {
  const machineEl = $('machine-state');
  const motionEl = $('motion-state');
  const xEl = $('pos-x');
  const yEl = $('pos-y');
  const zEl = $('pos-z');
  const fxEl = $('force-fx');
  const fyEl = $('force-fy');
  const fzEl = $('force-fz');

  if (machineEl) machineEl.textContent = status.machineState ?? '—';
  if (motionEl) motionEl.textContent = status.motionState ?? '—';
  if (xEl) xEl.textContent = formatPose(status.x);
  if (yEl) yEl.textContent = formatPose(status.y);
  if (zEl) zEl.textContent = formatPose(status.z);

  const fx = readForce(status, ['fx', 'forceX', 'forceFx']);
  const fy = readForce(status, ['fy', 'forceY', 'forceFy']);
  const fz = readForce(status, ['fz', 'forceZ', 'forceFz']);

  if (fxEl) fxEl.textContent = formatForce(fx);
  if (fyEl) fyEl.textContent = formatForce(fy);
  if (fzEl) fzEl.textContent = formatForce(fz);

  setTcpPose(status);
}

export function setConnectionStatus(text, isError) {
  const el = $('gui-status');
  if (!el) return;
  el.textContent = text;
  el.classList.toggle('gui-status--error', Boolean(isError));
}

export function wireGlobalStop() {
  const stopBtn = $('btn-stop-global');
  if (!stopBtn) return;

  stopBtn.addEventListener('click', async () => {
    try {
      await sendCommand({ command: 'stop' });
    } catch (err) {
      setConnectionStatus(err.message || String(err), true);
    }
  });
}
