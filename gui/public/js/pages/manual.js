'use strict';

import { navigate } from '../router.js';
import { subscribeLiveStatus } from '../api.js';
import { mountControlPanel } from '../components/controlPanel.js';
import { mountManualControls } from '../components/manualControls.js';

export function renderManual(container) {
  let liveStatus = {};
  const panel = document.createElement('section');
  panel.className = 'panel page-panel';
  panel.innerHTML = `
    <h2>Handmodus</h2>
    <p class="placeholder-note">
      Manuelle Bedienung gegen den MachineClient. Bewegungslogik liegt ausschließlich
      auf dem Server — die HMI sendet nur Commands. Soft-STOP ist in der Statusleiste.
    </p>
    <div class="control-actions page-actions">
      <button type="button" class="btn-back" data-route="menu">Zurück zum Hauptmenü</button>
    </div>
    <div id="manual-g7-controls" class="manual-controls"></div>
    <div id="manual-g3-controls" class="manual-controls"></div>
  `;

  panel.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));
  container.appendChild(panel);

  const unsubscribe = subscribeLiveStatus((status) => {
    liveStatus = status || {};
  });

  const cleanupG7 = mountManualControls(panel.querySelector('#manual-g7-controls'), {
    getStatus: () => liveStatus,
  });
  const cleanupG3 = mountControlPanel(panel.querySelector('#manual-g3-controls'));

  return () => {
    unsubscribe();
    if (cleanupG7) cleanupG7();
    if (cleanupG3) cleanupG3();
  };
}
