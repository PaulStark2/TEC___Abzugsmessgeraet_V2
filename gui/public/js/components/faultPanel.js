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
  const enableBtn = $('btn-enable-fault-manual');
  if (!banner) {
    return;
  }

  const isFault = status.machineState === 'Fault' || status.eStop === true;
  banner.hidden = !isFault;

  if (enableBtn) {
    // Recovery jog only makes sense in a latched controller Fault without E-Stop.
    enableBtn.hidden = !(status.machineState === 'Fault' && status.eStop !== true);
    enableBtn.disabled = status.faultManual === true;
    enableBtn.textContent =
      status.faultManual === true ? 'Handmodus aktiv' : 'Handmodus trotz Fault';
  }

  if (detail) {
    if (status.eStop === true && status.machineState === 'Fault') {
      detail.textContent = `E-Stop / ${status.faultReason || 'Controller Fault'}`;
    } else if (status.eStop === true) {
      detail.textContent = 'E-Stop aktiv';
    } else if (status.machineState === 'Fault' && status.faultManual === true) {
      detail.textContent = status.faultReason && status.faultReason !== 'None'
        ? `Fault — Handmodus freigegeben (${status.faultReason})`
        : 'Fault — Handmodus freigegeben';
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
  const clearBtn = $('btn-clear-fault');
  if (clearBtn) {
    clearBtn.addEventListener('click', async () => {
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

  const enableBtn = $('btn-enable-fault-manual');
  if (enableBtn) {
    enableBtn.addEventListener('click', async () => {
      try {
        const result = await sendCommand({ command: 'enableFaultManual' });
        if (!result.ok) {
          setConnectionStatus(result.error || 'Handmodus trotz Fault abgelehnt', true);
          return;
        }
        setConnectionStatus(
          'Handmodus trotz Fault freigegeben — mit begrenzter Geschwindigkeit/Schrittweite freifahren, danach Fault zurücksetzen.',
          false,
        );
      } catch (err) {
        setConnectionStatus(err.message || String(err), true);
      }
    });
  }
}
