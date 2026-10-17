'use strict';

import { sendCommand } from '../api.js';
import { MCS_REQUIRED_MESSAGE } from '../homingGate.js';
import { bindJogAxes, jogAxesMarkup } from './jogControls.js';

const SPEED_OPTIONS = [5, 10, 15, 25, 40];

/**
 * G7 manual controls: speed, jog (step/hold), spreader, homing stub.
 * @param {HTMLElement} container
 * @param {{ getStatus?: () => object }} [opts]
 * @returns {() => void} cleanup
 */
export function mountManualControls(container, opts) {
  const getStatus = (opts && opts.getStatus) || (() => ({}));

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
      ${jogAxesMarkup()}
    </div>

    <div class="manual-section">
      <h3>Kraft</h3>
      <p class="placeholder-note">Ruheprobe im Stillstand: 20 Messwerte im Abstand von 100 ms, Dauer etwa 1,9 s. Die Achsen dürfen dabei nicht fahren.</p>
      <div class="control-actions">
        <button type="button" id="btn-force-tare">Ruheprobe</button>
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
      <h3>Homing</h3>
      <p class="placeholder-note">Controller-gesteuertes Homing (CmdHome). Keine Sequenz in der GUI.</p>
      <div class="control-actions">
        <button type="button" id="btn-home">HOME</button>
      </div>
    </div>

    <p id="manual-feedback" class="command-feedback" aria-live="polite"></p>
  `;

  const speedSelect = container.querySelector('#manual-speed');
  const applySpeedBtn = container.querySelector('#btn-apply-speed');
  const openBtn = container.querySelector('#btn-pneumatic-open');
  const closeBtn = container.querySelector('#btn-pneumatic-close');
  const tareBtn = container.querySelector('#btn-force-tare');
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

  const onTare = async () => {
    try {
      await sendCommand({ command: 'forceTare' });
      setFeedback('Ruheprobe gestartet (etwa 1,9 s). Achsen nicht verfahren.', false);
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
      let mcsValid = false;
      try {
        const response = await fetch('/api/machine');
        const result = await response.json();
        mcsValid = Boolean(result && result.status && result.status.mcsValid === true);
      } catch {
        mcsValid = false;
      }
      if (!mcsValid) {
        setFeedback(MCS_REQUIRED_MESSAGE, true);
        return;
      }
      await sendCommand({ command: 'home' });
      setFeedback('HOME-Befehl gesendet', false);
    } catch (err) {
      setFeedback(err.message || String(err), true);
    }
  };

  applySpeedBtn.addEventListener('click', onApplySpeed);
  tareBtn.addEventListener('click', onTare);
  openBtn.addEventListener('click', onOpen);
  closeBtn.addEventListener('click', onClose);
  homeBtn.addEventListener('click', onHome);
  const unbindJog = bindJogAxes(container, { getStatus, setFeedback });

  return () => {
    applySpeedBtn.removeEventListener('click', onApplySpeed);
    tareBtn.removeEventListener('click', onTare);
    openBtn.removeEventListener('click', onOpen);
    closeBtn.removeEventListener('click', onClose);
    homeBtn.removeEventListener('click', onHome);
    unbindJog();
  };
}
