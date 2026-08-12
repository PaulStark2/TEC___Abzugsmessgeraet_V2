'use strict';

import { sendCommand } from '../api.js';

const SPEED_OPTIONS = [5, 10, 15, 25, 40];

/**
 * G7 manual controls: speed, jog, spreader, homing stub.
 * @param {HTMLElement} container
 * @returns {() => void} cleanup
 */
export function mountManualControls(container) {
  container.innerHTML = `
    <div class="manual-section">
      <h3>Geschwindigkeit</h3>
      <div class="control-form manual-speed-form">
        <label>
          Jog-/Move-Geschwindigkeit (Einheiten/s)
          <select id="manual-speed">
            ${SPEED_OPTIONS.map((s) => `<option value="${s}"${s === 15 ? ' selected' : ''}>${s}</option>`).join('')}
          </select>
        </label>
        <button type="button" id="btn-apply-speed">Geschwindigkeit setzen</button>
      </div>
    </div>

    <div class="manual-section">
      <h3>Jog</h3>
      <p class="placeholder-note">Einzel-Schritt pro Klick. Soft-STOP in der Statusleiste.</p>
      <div class="jog-grid">
        <div class="jog-axis">
          <span class="jog-label">X</span>
          <button type="button" class="btn-jog" data-axis="X" data-direction="-1">−X</button>
          <button type="button" class="btn-jog" data-axis="X" data-direction="1">+X</button>
        </div>
        <div class="jog-axis">
          <span class="jog-label">Y</span>
          <button type="button" class="btn-jog" data-axis="Y" data-direction="-1">−Y</button>
          <button type="button" class="btn-jog" data-axis="Y" data-direction="1">+Y</button>
        </div>
        <div class="jog-axis">
          <span class="jog-label">Z</span>
          <button type="button" class="btn-jog" data-axis="Z" data-direction="-1">−Z</button>
          <button type="button" class="btn-jog" data-axis="Z" data-direction="1">+Z</button>
        </div>
      </div>
    </div>

    <div class="manual-section">
      <h3>Spreizer</h3>
      <div class="control-actions">
        <button type="button" id="btn-pneumatic-open">Spreizer öffnen</button>
        <button type="button" id="btn-pneumatic-close">Spreizer schließen</button>
      </div>
    </div>

    <div class="manual-section">
      <h3>Homing (Mock-Stub)</h3>
      <p class="placeholder-note">Vereinfachtes Mock-Homing nach (0, 0, 0). Keine STM32-Sequenz.</p>
      <div class="control-actions">
        <button type="button" id="btn-home">Homing starten</button>
      </div>
    </div>

    <p id="manual-feedback" class="command-feedback" aria-live="polite"></p>
  `;

  const speedSelect = container.querySelector('#manual-speed');
  const applySpeedBtn = container.querySelector('#btn-apply-speed');
  const jogButtons = [...container.querySelectorAll('.btn-jog')];
  const openBtn = container.querySelector('#btn-pneumatic-open');
  const closeBtn = container.querySelector('#btn-pneumatic-close');
  const homeBtn = container.querySelector('#btn-home');
  const feedbackEl = container.querySelector('#manual-feedback');

  function setFeedback(text, isError) {
    if (!feedbackEl) return;
    feedbackEl.textContent = text || '';
    feedbackEl.classList.toggle('command-feedback--error', Boolean(isError));
  }

  const onApplySpeed = async () => {
    const speed = Number(speedSelect.value);
    try {
      await sendCommand({ command: 'setSpeed', speed });
      setFeedback(`Geschwindigkeit auf ${speed} gesetzt`, false);
    } catch (err) {
      setFeedback(err.message || String(err), true);
    }
  };

  const onJog = async (event) => {
    const btn = event.currentTarget;
    const axis = btn.dataset.axis;
    const direction = Number(btn.dataset.direction);
    try {
      await sendCommand({ command: 'jog', axis, direction });
      setFeedback(`jog ${axis} ${direction > 0 ? '+' : '−'}`, false);
    } catch (err) {
      setFeedback(err.message || String(err), true);
    }
  };

  const onOpen = async () => {
    try {
      await sendCommand({ command: 'pneumaticOpen' });
      setFeedback('Spreizer geöffnet', false);
    } catch (err) {
      setFeedback(err.message || String(err), true);
    }
  };

  const onClose = async () => {
    try {
      await sendCommand({ command: 'pneumaticClose' });
      setFeedback('Spreizer geschlossen', false);
    } catch (err) {
      setFeedback(err.message || String(err), true);
    }
  };

  const onHome = async () => {
    try {
      await sendCommand({ command: 'home' });
      setFeedback('Homing (Mock) gestartet', false);
    } catch (err) {
      setFeedback(err.message || String(err), true);
    }
  };

  applySpeedBtn.addEventListener('click', onApplySpeed);
  jogButtons.forEach((btn) => btn.addEventListener('click', onJog));
  openBtn.addEventListener('click', onOpen);
  closeBtn.addEventListener('click', onClose);
  homeBtn.addEventListener('click', onHome);

  return () => {
    applySpeedBtn.removeEventListener('click', onApplySpeed);
    jogButtons.forEach((btn) => btn.removeEventListener('click', onJog));
    openBtn.removeEventListener('click', onOpen);
    closeBtn.removeEventListener('click', onClose);
    homeBtn.removeEventListener('click', onHome);
  };
}
