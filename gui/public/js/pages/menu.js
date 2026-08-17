'use strict';

import { navigate } from '../router.js';

const MENU_ITEMS = [
  { route: 'load-points', label: 'Prüftstellen laden' },
  { route: 'select-points', label: 'Prüfstellen auswählen' },
  { route: 'procedure', label: 'Prüf-/Alterungsprozedur' },
  { route: 'results', label: 'Prüfergebnisse' },
  { route: 'manual', label: 'Handmodus' },
  { route: 'plate', label: 'Bauplatte wechseln' },
  { route: 'calibrate', label: 'Kalibrieren' },
  { route: 'parameters', label: 'Parametereinstellungen' },
];

export function renderMenu(container) {
  const panel = document.createElement('section');
  panel.className = 'panel page-panel';
  panel.innerHTML = `
    <h2>Hauptmenü</h2>
    <p class="placeholder-note">Wählen Sie einen Bereich. Status und 3D-Ansicht bleiben sichtbar.</p>
    <nav class="menu-nav" aria-label="Hauptmenü"></nav>
  `;

  const nav = panel.querySelector('.menu-nav');
  for (const item of MENU_ITEMS) {
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'menu-nav__item';
    btn.textContent = item.label;
    btn.addEventListener('click', () => navigate(item.route));
    nav.appendChild(btn);
  }

  container.appendChild(panel);
}
