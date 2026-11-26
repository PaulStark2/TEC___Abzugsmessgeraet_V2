'use strict';

import { connectStatus, sendCommand } from './api.js';
import { applyMessageSummary, applyStatus, setConnectionStatus, wireGlobalStop } from './statusBar.js';
import { wireFaultPanel } from './components/faultPanel.js';
import { startRouter, navigate } from './router.js';
import { createStartupHomingController, MCS_REQUIRED_MESSAGE } from './homingGate.js';
import { installSessionLock } from './sessionLock.js';

function startApp() {
  setConnectionStatus('GUI läuft — verbinde Live-Status…', false);
  wireGlobalStop();
  wireFaultPanel();
  startRouter();

  const startupHoming = createStartupHomingController({
    startHoming: () => sendCommand({ command: 'home' }),
    onNeedFullCalibration: async () => {
      setConnectionStatus(MCS_REQUIRED_MESSAGE, true);
      navigate('calibrate');
    },
  });

  connectStatus(
    (status) => {
      applyStatus(status);
      startupHoming.onStatus(status).catch((err) => {
        setConnectionStatus(err.message || String(err), true);
      });
    },
    (connected, message) => {
      setConnectionStatus(message || '', !connected);
    },
    (evt) => {
      if (evt && evt.summary) {
        applyMessageSummary(evt.summary);
      }
    }
  );

  // Expose for tests only — not used by pages for motion authority.
  if (typeof window !== 'undefined') {
    window.__guiSendCommand = sendCommand;
  }
}

document.addEventListener('DOMContentLoaded', () => {
  installSessionLock(startApp);
});
