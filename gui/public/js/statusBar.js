'use strict';

import { sendCommand } from './api.js';
import { setTcpPose } from './tcpView.js';
import { deriveDisplayState, formatHomingStatus } from './machineGuiState.js';
import { applyFaultPanel } from './components/faultPanel.js';

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

function applyMachineWarnings(warnings) {
  const el = $('machine-warnings');
  if (!el) {
    return;
  }

  if (!warnings || warnings.length === 0) {
    el.hidden = true;
    el.innerHTML = '';
    return;
  }

  el.hidden = false;
  el.innerHTML = warnings.map((msg) => `<p class="machine-warning">⚠ ${msg}</p>`).join('');
}

/**
 * Update global status bar and 3D TCP (display-only).
 * @param {object} status
 */
export function applyStatus(status) {
  const displayEl = $('gui-display-state');
  const machineEl = $('machine-state');
  const motionEl = $('motion-state');
  const xEl = $('pos-x');
  const yEl = $('pos-y');
  const zEl = $('pos-z');
  const speedEl = $('manual-speed-display');
  const pneumaticEl = $('pneumatic-state');
  const homingEl = $('homing-state');
  const phaseEl = $('procedure-phase');
  const faultEl = $('fault-reason');
  const commWdEl = $('comm-watchdog');
  const forceFreshEl = $('force-fresh');
  const plateCalibEl = $('plate-calib');
  const probeCalibEl = $('probe-calib');
  const spreaderStateEl = $('spreader-state');
  const fxEl = $('force-fx');
  const fyEl = $('force-fy');
  const fzEl = $('force-fz');

  const display = deriveDisplayState(status);
  if (displayEl) {
    displayEl.textContent = display.label;
    displayEl.className = `gui-display-state gui-display-state--${display.label.toLowerCase()}`;
  }
  applyMachineWarnings(display.warnings);
  applyFaultPanel(status);

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
    homingEl.textContent = formatHomingStatus(status);
  }
  if (phaseEl) {
    phaseEl.textContent = status.procedurePhase || '—';
  }
  if (faultEl) {
    faultEl.textContent = status.faultReason && status.faultReason !== 'None' ? status.faultReason : '—';
  }
  if (commWdEl) {
    commWdEl.textContent = status.communicationWatchdogEnabled ? 'aktiv' : 'aus';
  }
  if (forceFreshEl) {
    forceFreshEl.textContent = status.forceSampleFresh ? 'frisch' : 'alt';
  }
  if (plateCalibEl) {
    plateCalibEl.textContent = status.plateOriginValid ? 'gültig' : 'ungültig';
  }
  if (probeCalibEl) {
    probeCalibEl.textContent = status.probeCenterValid ? 'gültig' : 'ungültig';
  }
  if (spreaderStateEl) {
    spreaderStateEl.textContent = status.spreaderState || '—';
  }

  const fx = readForce(status, ['fx', 'forceX', 'forceFx']);
  const fy = readForce(status, ['fy', 'forceY', 'forceFy']);
  const fz = readForce(status, ['fz', 'forceZ', 'forceFz']);

  if (fxEl) fxEl.textContent = formatForce(fx);
  if (fyEl) fyEl.textContent = formatForce(fy);
  if (fzEl) fzEl.textContent = formatForce(fz);

  if (status.machineClientMode || status.implementation) {
    const modeEl = $('machine-client-mode');
    if (modeEl) {
      const mode = status.machineClientMode || '?';
      const impl = status.implementation || '';
      const tick = status.hasTick ? ' tick=yes' : '';
      modeEl.textContent = `${mode}${impl ? ` (${impl})` : ''}${tick}`;
    }
  }

  if (Object.prototype.hasOwnProperty.call(status, 'connected')) {
    if (status.connected === false) {
      setConnectionStatus('Maschine offline — TCP getrennt (keine Bewegung)', true);
    } else if (status.connected === true) {
      setConnectionStatus('Maschine verbunden (Protocol TCP)', false);
    }
  }

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
