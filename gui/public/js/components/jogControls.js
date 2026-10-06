'use strict';

import { sendCommand, fetchMachineConfig, fetchMachineStatus } from '../api.js';

/** Far target distance for Dauerhalt (mm). Soft limits / STOP end the move. */
const HOLD_TRAVEL_MM = 1000;

/**
 * Snap a typed jog step to a positive multiple of minStep (at least 1× min).
 * @param {unknown} raw
 * @param {unknown} minStep
 * @returns {number}
 */
export function snapJogStepMm(raw, minStep) {
  const min = Number(minStep);
  const safeMin = Number.isFinite(min) && min > 0 ? min : 0.1;
  const v = Number(raw);
  if (!Number.isFinite(v) || v <= 0) {
    return safeMin;
  }
  if (v < safeMin) {
    return safeMin;
  }
  const n = Math.max(1, Math.round(v / safeMin));
  return Number((n * safeMin).toFixed(8));
}

/**
 * HTML fragment: mode toggle + step input + axis buttons (no outer section wrapper).
 * Compact mode is one short row for the force-calibration wizard.
 * @param {boolean} [compact]
 * @returns {string}
 */
export function jogAxesMarkup(compact = false) {
  const axes = `
        <span class="jog-label">X</span>
        <button type="button" class="btn-jog" data-axis="X" data-direction="-1">−X</button>
        <button type="button" class="btn-jog" data-axis="X" data-direction="1">+X</button>
        <span class="jog-label">Y</span>
        <button type="button" class="btn-jog" data-axis="Y" data-direction="-1">−Y</button>
        <button type="button" class="btn-jog" data-axis="Y" data-direction="1">+Y</button>
        <span class="jog-label">Z</span>
        <button type="button" class="btn-jog" data-axis="Z" data-direction="-1">−Z</button>
        <button type="button" class="btn-jog" data-axis="Z" data-direction="1">+Z</button>`;
  if (compact) {
    return `
      <div class="jog-compact">
        <label class="jog-compact__step">mm
          <input type="number" class="jog-step-input" min="0" step="any" value="1" />
        </label>
        <button type="button" class="jog-mode-btn is-active" data-jog-mode="step">Schritt</button>
        <button type="button" class="jog-mode-btn" data-jog-mode="hold">Halten</button>
        <div class="jog-axis jog-axis--inline">${axes}</div>
      </div>`;
  }
  return `
    <div class="jog-mode-row" role="group" aria-label="Jog-Modus">
      <button type="button" class="jog-mode-btn is-active" data-jog-mode="step">Einzelschritt</button>
      <button type="button" class="jog-mode-btn" data-jog-mode="hold">Dauerhalt</button>
    </div>
    <div class="control-form jog-step-form">
      <label>
        Schrittweite (mm)
        <input type="number" class="jog-step-input" min="0" step="any" value="1" />
      </label>
      <span class="jog-step-hint placeholder-note"></span>
    </div>
    <p class="placeholder-note jog-mode-hint">Einzelschritt: ein Klick = eine Schrittweite. Dauerhalt: halten = verfahren, loslassen = STOP.</p>
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
  `;
}

/**
 * Wire mode/step/axis behaviour onto an already-mounted jog axes fragment.
 *
 * @param {HTMLElement} root - element containing jog markup
 * @param {{
 *   getStatus?: () => object,
 *   setFeedback?: (text: string, isError?: boolean) => void,
 * }} [opts]
 * @returns {() => void} cleanup
 */
export function bindJogAxes(root, opts) {
  const getStatus = (opts && opts.getStatus) || (() => ({}));
  const setFeedback =
    (opts && opts.setFeedback) ||
    (() => {
      /* optional */
    });

  const modeButtons = [...root.querySelectorAll('.jog-mode-btn')];
  const stepInput = root.querySelector('.jog-step-input');
  const stepHint = root.querySelector('.jog-step-hint');
  const jogButtons = [...root.querySelectorAll('.btn-jog')];

  let mode = 'step'; // 'step' | 'hold'
  let minStepMm = 0.1;
  let holdActive = false;
  let holdPointerId = null;

  function applyModeUi() {
    modeButtons.forEach((btn) => {
      btn.classList.toggle('is-active', btn.dataset.jogMode === mode);
    });
    if (stepInput) {
      stepInput.disabled = mode === 'hold';
    }
  }

  function currentStepMm() {
    return snapJogStepMm(stepInput ? stepInput.value : minStepMm, minStepMm);
  }

  function syncStepFromInput() {
    if (!stepInput) return;
    const snapped = currentStepMm();
    stepInput.value = String(snapped);
  }

  async function loadMinStep() {
    try {
      const result = await fetchMachineConfig();
      const cfg = (result && result.config) || {};
      const raw = Number(cfg.jog_min_step_mm);
      minStepMm = Number.isFinite(raw) && raw > 0 ? raw : 0.1;
    } catch {
      minStepMm = 0.1;
    }
    if (stepInput) {
      stepInput.min = String(minStepMm);
      stepInput.step = String(minStepMm);
      const initial = snapJogStepMm(stepInput.value, minStepMm);
      stepInput.value = String(initial);
    }
    if (stepHint) {
      stepHint.textContent = `Mindestschritt / Raster: ${minStepMm} mm`;
    }
  }

  const onModeClick = (event) => {
    const next = event.currentTarget.dataset.jogMode;
    if (next !== 'step' && next !== 'hold') return;
    if (holdActive) {
      void stopHold('Modus gewechselt — STOP');
    }
    mode = next;
    applyModeUi();
  };

  const onStepBlur = () => {
    syncStepFromInput();
  };

  async function runStepJog(axis, direction) {
    const step = currentStepMm();
    if (stepInput) stepInput.value = String(step);
    try {
      await sendCommand({ command: 'jog', axis, direction, step });
      setFeedback(`jog ${axis} ${direction > 0 ? '+' : '−'} ${step} mm`, false);
    } catch (err) {
      setFeedback(err.message || String(err), true);
    }
  }

  async function resolvePose() {
    let status = getStatus() || {};
    let x = Number(status.x);
    let y = Number(status.y);
    let z = Number(status.z);
    if ([x, y, z].every(Number.isFinite)) {
      return { x, y, z };
    }
    try {
      status = await fetchMachineStatus();
      x = Number(status.x);
      y = Number(status.y);
      z = Number(status.z);
    } catch {
      return null;
    }
    if (![x, y, z].every(Number.isFinite)) {
      return null;
    }
    return { x, y, z };
  }

  async function startHold(axis, direction) {
    const pose = await resolvePose();
    if (!pose) {
      setFeedback('Keine gültige Position für Dauerhalt', true);
      return false;
    }
    const { x, y, z } = pose;
    const target = { x, y, z };
    const key = axis.toLowerCase();
    target[key] = target[key] + direction * HOLD_TRAVEL_MM;
    try {
      await sendCommand({ command: 'moveTo', x: target.x, y: target.y, z: target.z });
      setFeedback(`Dauerhalt ${axis} ${direction > 0 ? '+' : '−'} …`, false);
      return true;
    } catch (err) {
      setFeedback(err.message || String(err), true);
      return false;
    }
  }

  async function stopHold(message) {
    holdActive = false;
    holdPointerId = null;
    try {
      await sendCommand({ command: 'stop' });
      if (message) setFeedback(message, false);
    } catch (err) {
      setFeedback(err.message || String(err), true);
    }
  }

  const onJogPointerDown = async (event) => {
    if (event.button !== undefined && event.button !== 0) return;
    const btn = event.currentTarget;
    const axis = btn.dataset.axis;
    const direction = Number(btn.dataset.direction);

    if (mode === 'step') {
      // Click path handled by click listener (avoids double-fire with pointer).
      return;
    }

    event.preventDefault();
    if (holdActive) return;
    holdActive = true;
    holdPointerId = event.pointerId;
    try {
      btn.setPointerCapture(event.pointerId);
    } catch {
      /* ignore */
    }
    const ok = await startHold(axis, direction);
    if (!ok) {
      holdActive = false;
      holdPointerId = null;
    }
  };

  const onJogPointerUp = async (event) => {
    if (mode !== 'hold') return;
    if (!holdActive) return;
    if (holdPointerId != null && event.pointerId !== holdPointerId) return;
    event.preventDefault();
    const btn = event.currentTarget;
    try {
      if (holdPointerId != null) btn.releasePointerCapture(holdPointerId);
    } catch {
      /* ignore */
    }
    await stopHold('STOP (Dauerhalt losgelassen)');
  };

  const onJogClick = async (event) => {
    if (mode !== 'step') {
      event.preventDefault();
      return;
    }
    const btn = event.currentTarget;
    await runStepJog(btn.dataset.axis, Number(btn.dataset.direction));
  };

  const onWindowBlur = () => {
    if (holdActive) {
      void stopHold('STOP (Fenster inaktiv)');
    }
  };

  modeButtons.forEach((btn) => btn.addEventListener('click', onModeClick));
  if (stepInput) {
    stepInput.addEventListener('blur', onStepBlur);
    stepInput.addEventListener('change', onStepBlur);
  }
  jogButtons.forEach((btn) => {
    btn.addEventListener('pointerdown', onJogPointerDown);
    btn.addEventListener('pointerup', onJogPointerUp);
    btn.addEventListener('pointercancel', onJogPointerUp);
    btn.addEventListener('lostpointercapture', onJogPointerUp);
    btn.addEventListener('click', onJogClick);
  });
  window.addEventListener('blur', onWindowBlur);

  applyModeUi();
  void loadMinStep();

  return () => {
    if (holdActive) {
      void sendCommand({ command: 'stop' }).catch(() => {});
      holdActive = false;
    }
    modeButtons.forEach((btn) => btn.removeEventListener('click', onModeClick));
    if (stepInput) {
      stepInput.removeEventListener('blur', onStepBlur);
      stepInput.removeEventListener('change', onStepBlur);
    }
    jogButtons.forEach((btn) => {
      btn.removeEventListener('pointerdown', onJogPointerDown);
      btn.removeEventListener('pointerup', onJogPointerUp);
      btn.removeEventListener('pointercancel', onJogPointerUp);
      btn.removeEventListener('lostpointercapture', onJogPointerUp);
      btn.removeEventListener('click', onJogClick);
    });
    window.removeEventListener('blur', onWindowBlur);
  };
}
