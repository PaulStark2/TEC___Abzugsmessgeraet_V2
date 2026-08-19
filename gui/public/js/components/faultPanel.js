'use strict';

import { sendCommand } from '../api.js';
import { setConnectionStatus } from '../statusBar.js';

function $(id) {
  return document.getElementById(id);
}

/**
 * Show/hide fault banner from controller status (display-only).
 * @param {object} status
 */
export function applyFaultPanel(status) {
  const banner = $('fault-banner');
  const detail = $('fault-detail');
  if (!banner) {
    return;
  }

  const isFault = status.machineState === 'Fault' || status.eStop === true;
  banner.hidden = !isFault;

  if (detail) {
    if (status.eStop === true && status.machineState === 'Fault') {
      detail.textContent = `E-Stop / ${status.faultReason || 'Controller Fault'}`;
    } else if (status.eStop === true) {
      detail.textContent = 'E-Stop aktiv';
    } else if (status.machineState === 'Fault') {
      detail.textContent = status.faultReason && status.faultReason !== 'None'
        ? `Controller Fault: ${status.faultReason}`
        : 'Controller Fault';
    } else {
      detail.textContent = '—';
    }
  }
}

export function wireFaultPanel() {
  const btn = $('btn-clear-fault');
  if (!btn) {
    return;
  }

  btn.addEventListener('click', async () => {
    try {
      const result = await sendCommand({ command: 'clearFault' });
      if (!result.ok) {
        setConnectionStatus(result.error || 'Fault-Reset abgelehnt', true);
        return;
      }
      setConnectionStatus('Fault-Reset-Befehl gesendet — warte auf Controller…', false);
    } catch (err) {
      setConnectionStatus(err.message || String(err), true);
    }
  });
}
