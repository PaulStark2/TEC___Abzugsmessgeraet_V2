'use strict';

// G1: confirm static JS loads. Live machine status comes in G2+.
document.addEventListener('DOMContentLoaded', () => {
  const statusEl = document.getElementById('gui-status');
  if (statusEl) {
    statusEl.textContent = 'GUI läuft (G1 Scaffold — noch keine Maschinenanbindung)';
  }
});
