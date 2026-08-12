'use strict';

import { navigate } from '../router.js';

/**
 * Generic placeholder page (G4).
 * @param {HTMLElement} container
 * @param {{ title: string, description: string, milestone?: string }} options
 */
export function renderPlaceholder(container, options) {
  const panel = document.createElement('section');
  panel.className = 'panel page-panel';
  panel.innerHTML = `
    <h2>${options.title}</h2>
    <p class="placeholder-note">${options.description}</p>
    ${options.milestone ? `<p class="placeholder-note milestone-hint">Geplant: ${options.milestone}</p>` : ''}
    <div class="control-actions">
      <button type="button" class="btn-back" data-route="menu">Zurück zum Hauptmenü</button>
    </div>
  `;

  const backBtn = panel.querySelector('.btn-back');
  backBtn.addEventListener('click', () => navigate('menu'));

  container.appendChild(panel);
}
