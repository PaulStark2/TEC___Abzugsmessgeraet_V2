'use strict';

import { setupTcpView } from './tcpView.js';
import { connectStatus, sendCommand } from './api.js';
import { applyStatus, setConnectionStatus, wireGlobalStop } from './statusBar.js';
import { startRouter } from './router.js';

document.addEventListener('DOMContentLoaded', () => {
  setConnectionStatus('GUI läuft — verbinde Live-Status…', false);
  setupTcpView();
  wireGlobalStop();
  startRouter();

  connectStatus(
    (status) => applyStatus(status),
    (connected, message) => {
      setConnectionStatus(message || '', !connected);
    }
  );

  // Expose for tests only — not used by pages for motion authority.
  if (typeof window !== 'undefined') {
    window.__guiSendCommand = sendCommand;
  }
});
