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
  const speedEl = $('manual-speed-display');
  const pneumaticEl = $('pneumatic-state');
  const homingEl = $('homing-state');
  const fxEl = $('force-fx');
  const fyEl = $('force-fy');
  const fzEl = $('force-fz');

  if (machineEl) machineEl.textContent = status.machineState ?? '—';
  if (motionEl) motionEl.textContent = status.motionState ?? '—';
  if (xEl) xEl.textContent = formatPose(status.x);
  if (yEl) yEl.textContent = formatPose(status.y);
  if (zEl) zEl.textContent = formatPose(status.z);

  if (speedEl) {
    speedEl.textContent =
      status.manualSpeed !== undefined && Number.isFinite(Number(status.manualSpeed))
        ? Number(status.manualSpeed).toFixed(0)
        : '—';
  }

  if (pneumaticEl) {
    if (status.pneumaticExtended === true) {
      pneumaticEl.textContent = 'Offen';
    } else if (status.pneumaticExtended === false) {
      pneumaticEl.textContent = 'Geschlossen';
    } else {
      pneumaticEl.textContent = '—';
    }
  }

  if (homingEl) {
    if (status.homingInProgress === true || status.machineState === 'Homing') {
      homingEl.textContent = 'Aktiv';
    } else if (status.machineState === 'Idle' && status.motionState === 'AtTarget') {
      homingEl.textContent = 'Bereit';
    } else {
      homingEl.textContent = '—';
    }
  }

  const fx = readForce(status, ['fx', 'forceX', 'forceFx']);
  const fy = readForce(status, ['fy', 'forceY', 'forceFy']);
  const fz = readForce(status, ['fz', 'forceZ', 'forceFz']);

  if (fxEl) fxEl.textContent = formatForce(fx);
  if (fyEl) fyEl.textContent = formatForce(fy);
  if (fzEl) fzEl.textContent = formatForce(fz);

  // Protocol/mock identity from server (authoritative).
  if (status.machineClientMode || status.implementation) {
    const modeEl = $('machine-client-mode');
    if (modeEl) {
      const mode = status.machineClientMode || '?';
      const impl = status.implementation || '';
      const tick = status.hasTick ? ' tick=yes' : '';
      modeEl.textContent = `${mode}${impl ? ` (${impl})` : ''}${tick}`;
    }
  }

  // Protocol client exposes machine TCP liveness via status.connected.
  // Mock/sim omit the field — leave GUI WebSocket status message unchanged.
  if (Object.prototype.hasOwnProperty.call(status, 'connected')) {
    if (status.connected === false) {
      setConnectionStatus('Maschine offline — TCP getrennt (keine Bewegung)', true);
    } else if (status.connected === true) {
      setConnectionStatus('Maschine verbunden (Protocol TCP)', false);
    }
  }

  // Display-only: never invent motion. Pose stays at last reported values when offline.
  // ONLY call site that moves the Three.js TCP mesh (besides initial 0,0,0).
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
