'use strict';

/**
 * Shared jog step helpers (HMI). Raster = jog_min_step_mm from machine-config.json.
 */

/**
 * Snap a typed jog step to a positive multiple of minStep (at least 1× min).
 * @param {unknown} raw
 * @param {unknown} minStep
 * @returns {number}
 */
function snapJogStepMm(raw, minStep) {
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
 * True if step is a positive finite multiple of minStep (within float tolerance).
 * @param {unknown} step
 * @param {unknown} minStep
 * @returns {boolean}
 */
function isJogStepAllowed(step, minStep) {
  const s = Number(step);
  const min = Number(minStep);
  if (!(s > 0) || !Number.isFinite(s) || !(min > 0) || !Number.isFinite(min)) {
    return false;
  }
  if (s + 1e-9 < min) {
    return false;
  }
  const q = s / min;
  return Math.abs(q - Math.round(q)) <= 1e-6;
}

module.exports = {
  snapJogStepMm,
  isJogStepAllowed,
};
