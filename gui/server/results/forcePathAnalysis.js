'use strict';

/**
 * Offline Kraft-Weg evaluation from streamed MsgForcePathChunk curves.
 * Peak force = max |Fz| per curve; mean and sample stddev over iterations.
 * See docs/Measurement_Evaluation.md.
 */

const { LEG_INSERT, LEG_EXTRACT } = require('./forcePathStore');

function peakAbsForce(samples) {
  let peak = 0;
  let peakIndex = -1;
  for (let i = 0; i < samples.length; ++i) {
    const a = Math.abs(Number(samples[i].fz));
    if (!Number.isFinite(a)) continue;
    if (peakIndex < 0 || a > peak) {
      peak = a;
      peakIndex = i;
    }
  }
  return { peakN: peakIndex < 0 ? null : peak, peakIndex };
}

function mean(values) {
  if (!values.length) return null;
  let s = 0;
  for (const v of values) s += v;
  return s / values.length;
}

/** Sample standard deviation (Bessel, n−1). null if fewer than 2 values. */
function sampleStddev(values) {
  if (values.length < 2) return null;
  const m = mean(values);
  let acc = 0;
  for (const v of values) {
    const d = v - m;
    acc += d * d;
  }
  return Math.sqrt(acc / (values.length - 1));
}

function enrichCurve(curve) {
  const samples = (curve.samples || []).map((s, i) => {
    const z = Number(s.z);
    const fz = Number(s.fz);
    return { index: i, z, fz };
  });
  const z0 = samples.length ? samples[0].z : 0;
  const withRel = samples.map((s) => ({
    ...s,
    zRel: s.z - z0,
  }));
  const { peakN, peakIndex } = peakAbsForce(withRel);
  return {
    pointIndex: curve.pointIndex,
    iteration: curve.iteration,
    leg: curve.leg,
    dropped: curve.dropped || 0,
    samples: withRel,
    peakN,
    peakSampleIndex: peakIndex,
  };
}

function statsForPeaks(peaks) {
  const finite = peaks.filter((p) => Number.isFinite(p));
  return {
    count: finite.length,
    meanPeakN: mean(finite),
    stddevPeakN: sampleStddev(finite),
    peaksN: finite,
  };
}

/**
 * @param {Array} rawCurves from forcePathStore.getCurves()
 * @param {{ programName?: string, pointNames?: Record<number,string>|string[] }} meta
 */
function analyzeForcePaths(rawCurves, meta = {}) {
  const curves = (rawCurves || []).map(enrichCurve);
  const pointNames = meta.pointNames || {};

  const pointIndexSet = new Set();
  for (const c of curves) pointIndexSet.add(c.pointIndex);
  const pointIndices = [...pointIndexSet].sort((a, b) => a - b);

  const points = pointIndices.map((pointIndex) => {
    const name =
      (Array.isArray(pointNames) ? pointNames[pointIndex] : pointNames[pointIndex]) ||
      `Punkt ${pointIndex + 1}`;
    const pointCurves = curves.filter((c) => c.pointIndex === pointIndex);
    const insertPeaks = pointCurves
      .filter((c) => c.leg === LEG_INSERT)
      .map((c) => c.peakN)
      .filter((p) => p != null);
    const extractPeaks = pointCurves
      .filter((c) => c.leg === LEG_EXTRACT)
      .map((c) => c.peakN)
      .filter((p) => p != null);
    return {
      pointIndex,
      name,
      curves: pointCurves,
      insert: statsForPeaks(insertPeaks),
      extract: statsForPeaks(extractPeaks),
    };
  });

  return {
    programName: meta.programName || null,
    curveCount: curves.length,
    points,
    curves,
  };
}

module.exports = {
  LEG_INSERT,
  LEG_EXTRACT,
  peakAbsForce,
  mean,
  sampleStddev,
  enrichCurve,
  analyzeForcePaths,
};
