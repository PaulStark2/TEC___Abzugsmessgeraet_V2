'use strict';

import { navigate } from '../router.js';
import { mountControlPanel } from '../components/controlPanel.js';

export function renderManual(container) {
  const panel = document.createElement('section');
  panel.className = 'panel page-panel';
  panel.innerHTML = `
    <h2>Handmodus</h2>
    <p class="placeholder-note">
      Bewegungssteuerung aus G3. Erweiterungen (Jog, Spreizer, Homing) folgen in G7.
    </p>
    <div class="control-actions page-actions">
      <button type="button" class="btn-back" data-route="menu">Zurück zum Hauptmenü</button>
    </div>
    <div id="manual-controls" class="manual-controls"></div>
  `;

  panel.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));
  container.appendChild(panel);

  const controlsRoot = panel.querySelector('#manual-controls');
  const cleanupControls = mountControlPanel(controlsRoot);

  return () => {
    if (cleanupControls) cleanupControls();
  };
}
