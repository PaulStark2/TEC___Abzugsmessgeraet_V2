'use strict';

import { setupTcpView } from './tcpView.js';
import { connectStatus, sendCommand } from './api.js';
import { applyStatus, setConnectionStatus, wireGlobalStop } from './statusBar.js';
import { wireFaultPanel } from './components/faultPanel.js';
import { startRouter } from './router.js';
import { createStartupHomingController } from './homingGate.js';

document.addEventListener('DOMContentLoaded', () => {
  setConnectionStatus('GUI läuft — verbinde Live-Status…', false);
  setupTcpView();
  wireGlobalStop();
  wireFaultPanel();
  startRouter();

  const startupHoming = createStartupHomingController({
    startHoming: () => sendCommand({ command: 'home' }),
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
    }
  );

  // Expose for tests only — not used by pages for motion authority.
  if (typeof window !== 'undefined') {
    window.__guiSendCommand = sendCommand;
  }
});
