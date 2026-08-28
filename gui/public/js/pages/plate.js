'use strict';

import { navigate } from '../router.js';
import { fixtureOpen, fixtureClose, fetchMachineStatus } from '../api.js';
import { applyFixtureStateDisplay } from '../fixtureStateDisplay.js';

function applyLocalFixture(status) {
  applyFixtureStateDisplay(
    document.getElementById('plate-fixture-state'),
    status && typeof status.fixtureExtended === 'boolean' ? status.fixtureExtended : undefined
  );
}

export function renderPlate(container) {
  const panel = document.createElement('section');
  panel.className = 'panel page-panel';
  panel.innerHTML = `
    <h2>Bauplatte wechseln</h2>
    <p class="placeholder-note">
      Öffne das Spannsystem, wechsle die Bauplatte von Hand und schließe das Spannsystem wieder.
      Soft-STOP bleibt in der Statusleiste.
    </p>
    <p class="placeholder-note">
      Spannsystem: <span id="plate-fixture-state">—</span>
    </p>
    <p id="plate-fixture-feedback" class="command-feedback" aria-live="polite"></p>
    <div class="control-actions">
      <button type="button" id="btn-fixture-open">Spannsystem öffnen/lösen</button>
      <button type="button" id="btn-fixture-close">Spannsystem schließen</button>
      <button type="button" class="btn-back" data-route="menu">Zurück zum Hauptmenü</button>
    </div>
  `;

  const feedbackEl = panel.querySelector('#plate-fixture-feedback');

  async function runFixtureCommand(action, successText) {
    try {
      feedbackEl.textContent = '';
      feedbackEl.classList.remove('command-feedback--error');
      const result = await action();
      applyLocalFixture(result && result.status);
      feedbackEl.textContent = successText;
    } catch (err) {
      feedbackEl.textContent = err.message || String(err);
      feedbackEl.classList.add('command-feedback--error');
    }
  }

  panel.querySelector('#btn-fixture-open').addEventListener('click', () => {
    void runFixtureCommand(fixtureOpen, 'Spannsystem geöffnet.');
  });
  panel.querySelector('#btn-fixture-close').addEventListener('click', () => {
    void runFixtureCommand(fixtureClose, 'Spannsystem geschlossen.');
  });
  panel.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));

  container.appendChild(panel);

  void fetchMachineStatus()
    .then((status) => applyLocalFixture(status))
    .catch(() => applyLocalFixture({}));
}
