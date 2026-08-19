'use strict';

import { sendCommand } from '../api.js';

const SPEED_OPTIONS = [5, 10, 15, 25, 40];

/**
 * Reusable jog panel with speed selector, X/Y/Z +/- buttons, and live position.
 * Uses the same sendCommand({command:'jog'}) path as the manual controls.
 *
 * @param {HTMLElement} container
 * @param {{ getStatus?: () => object }} [opts]
 * @returns {() => void} cleanup
 */
export function mountJogPanel(container, opts) {
  const getStatus = (opts && opts.getStatus) || (() => ({}));

  container.innerHTML = `
    <div class="manual-section">
      <div class="control-form manual-speed-form">
        <label>
          Schrittweite (Einheiten/s)
          <select class="jog-speed-select">
            ${SPEED_OPTIONS.map((s) => `<option value="${s}"${s === 15 ? ' selected' : ''}>${s}</option>`).join('')}
          </select>
        </label>
        <button type="button" class="jog-apply-speed">Geschwindigkeit setzen</button>
      </div>
      <div class="jog-grid" style="margin-top:8px;">
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
      <div class="jog-position" style="margin-top:8px;font-family:monospace;font-size:0.95rem;"></div>
      <p class="command-feedback jog-feedback" aria-live="polite"></p>
    </div>
  `;

  const speedSelect = container.querySelector('.jog-speed-select');
  const applySpeedBtn = container.querySelector('.jog-apply-speed');
  const jogButtons = [...container.querySelectorAll('.btn-jog')];
  const feedbackEl = container.querySelector('.jog-feedback');
  const positionEl = container.querySelector('.jog-position');

  function setFeedback(text, isError) {
    if (!feedbackEl) return;
    feedbackEl.textContent = text || '';
    feedbackEl.classList.toggle('command-feedback--error', Boolean(isError));
  }

  function updatePosition() {
    const s = getStatus();
    if (s && Number.isFinite(s.x)) {
      positionEl.textContent =
        `X: ${Number(s.x).toFixed(3)}   Y: ${Number(s.y).toFixed(3)}   Z: ${Number(s.z).toFixed(3)}`;
    }
  }

  const posTimer = window.setInterval(updatePosition, 250);
  updatePosition();

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

  applySpeedBtn.addEventListener('click', onApplySpeed);
  jogButtons.forEach((btn) => btn.addEventListener('click', onJog));

  return () => {
    window.clearInterval(posTimer);
    applySpeedBtn.removeEventListener('click', onApplySpeed);
    jogButtons.forEach((btn) => btn.removeEventListener('click', onJog));
  };
}
