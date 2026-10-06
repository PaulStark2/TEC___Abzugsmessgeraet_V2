'use strict';

import { sendCommand } from '../api.js';
import { bindJogAxes, jogAxesMarkup } from './jogControls.js';

const SPEED_OPTIONS = [5, 10, 15, 25, 40];

/**
 * Reusable jog panel with speed selector, step/hold modes, and live position.
 *
 * @param {HTMLElement} container
 * @param {{ getStatus?: () => object }} [opts]
 * @returns {() => void} cleanup
 */
export function mountJogPanel(container, opts) {
  const getStatus = (opts && opts.getStatus) || (() => ({}));
  const compact = Boolean(opts && opts.compact);

  container.innerHTML = compact
    ? `
    <div class="jog-panel jog-panel--compact">
      ${jogAxesMarkup(true)}
      <p class="jog-position"></p>
    </div>
  `
    : `
    <div class="manual-section">
      <div class="control-form manual-speed-form">
        <label>
          Geschwindigkeit (Einheiten/s)
          <select class="jog-speed-select">
            ${SPEED_OPTIONS.map((s) => `<option value="${s}"${s === 15 ? ' selected' : ''}>${s}</option>`).join('')}
          </select>
        </label>
        <button type="button" class="jog-apply-speed">Geschwindigkeit setzen</button>
      </div>
      ${jogAxesMarkup(false)}
      <div class="jog-position" style="margin-top:8px;font-family:monospace;font-size:0.95rem;"></div>
      <p class="command-feedback jog-feedback" aria-live="polite"></p>
    </div>
  `;

  const speedSelect = container.querySelector('.jog-speed-select');
  const applySpeedBtn = container.querySelector('.jog-apply-speed');
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

  if (applySpeedBtn) {
    applySpeedBtn.addEventListener('click', onApplySpeed);
  }
  const unbindJog = bindJogAxes(container, { getStatus, setFeedback });

  return () => {
    window.clearInterval(posTimer);
    if (applySpeedBtn) {
      applySpeedBtn.removeEventListener('click', onApplySpeed);
    }
    unbindJog();
  };
}
