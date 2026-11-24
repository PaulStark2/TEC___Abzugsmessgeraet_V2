'use strict';

/**
 * Offline Kraft-Weg evaluation from streamed MsgForcePathChunk curves.
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

/** Peak among samples with Fz < 0 only (Zug). Used for Ausgliedern stats. */
function peakTensileForce(samples) {
  let peak = 0;
  let peakIndex = -1;
  let seenTensile = false;
  for (let i = 0; i < samples.length; ++i) {
    const fz = Number(samples[i].fz);
    if (!Number.isFinite(fz)) continue;
    if (fz < 0) {
      seenTensile = true;
      const a = -fz;
      if (peakIndex < 0 || a > peak) {
        peak = a;
        peakIndex = i;
      }
    }
  }
  if (!seenTensile) {
    return { peakN: null, peakIndex: -1 };
  }
  return { peakN: peak, peakIndex };
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

function normalizeWireLeg(wireLeg) {
  return wireLeg === LEG_INSERT || wireLeg === LEG_EXTRACT ? wireLeg : LEG_INSERT;
}

/**
 * Direction from path geometry (not peak |Fz| — extract curves often start under
 * residual Druck, so max|Fz| would mis-label them as Eingliedern).
 * +Δz → Ausgliedern, −Δz → Eingliedern. Fallback: clear Zug (min Fz < 0) → Extract.
 */
function legFromCurveMotion(samples, wireLeg) {
  const fallback = normalizeWireLeg(wireLeg);
  if (samples.length >= 2) {
    const dz = Number(samples[samples.length - 1].z) - Number(samples[0].z);
    if (Number.isFinite(dz) && Math.abs(dz) > 1e-3) {
      return dz > 0 ? LEG_EXTRACT : LEG_INSERT;
    }
  }
  let minFz = Infinity;
  for (const s of samples) {
    const fz = Number(s.fz);
    if (Number.isFinite(fz)) minFz = Math.min(minFz, fz);
  }
  if (Number.isFinite(minFz) && minFz < -1e-3) {
    return LEG_EXTRACT;
  }
  return fallback;
}

function classifyCurve(curve) {
  const samples = (curve.samples || []).map((s, i) => {
    const z = Number(s.z);
    const fz = Number(s.fz);
    return { index: i, z, fz };
  });
  const wireLeg = Number(curve.wireLeg != null ? curve.wireLeg : curve.leg);
  const leg = legFromCurveMotion(samples, wireLeg);
  return {
    pointIndex: curve.pointIndex,
    iteration: curve.iteration,
    leg,
    wireLeg: normalizeWireLeg(wireLeg),
    dropped: curve.dropped || 0,
    samples,
  };
}

/**
 * Shared stroke frame per (point, iteration):
 * z_free = kraftfrei / Ausgliederpunkt → zRel = 0
 * seat is more negative (Eingliederpunkt).
 */
function applyStrokeNormalization(curves) {
  const byKey = new Map();
  for (const c of curves) {
    const key = `${c.pointIndex}:${c.iteration}`;
    if (!byKey.has(key)) byKey.set(key, []);
    byKey.get(key).push(c);
  }

  for (const group of byKey.values()) {
    const insert = group.find((c) => c.leg === LEG_INSERT);
    const extract = group.find((c) => c.leg === LEG_EXTRACT);
    let zFree = null;
    if (insert && insert.samples.length) {
      zFree = insert.samples[0].z;
    } else if (extract && extract.samples.length) {
      zFree = extract.samples[extract.samples.length - 1].z;
    }
    if (zFree == null || !Number.isFinite(zFree)) {
      for (const c of group) {
        const z0 = c.samples.length ? c.samples[0].z : 0;
        c.zFree = z0;
        c.samples = c.samples.map((s) => ({ ...s, zRel: s.z - z0 }));
      }
      continue;
    }
    for (const c of group) {
      c.zFree = zFree;
      c.samples = c.samples.map((s) => ({ ...s, zRel: s.z - zFree }));
    }
  }
  return curves;
}

function attachPeaks(curve) {
  if (curve.leg === LEG_EXTRACT) {
    const { peakN, peakIndex } = peakTensileForce(curve.samples);
    curve.peakN = peakN;
    curve.peakSampleIndex = peakIndex;
    curve.peakRegion = 'tensile';
  } else {
    const { peakN, peakIndex } = peakAbsForce(curve.samples);
    curve.peakN = peakN;
    curve.peakSampleIndex = peakIndex;
    curve.peakRegion = 'all';
  }
  return curve;
}

function enrichCurve(curve) {
  // Single-curve path (tests): classify + local zRel fallback + peak.
  const classified = classifyCurve(curve);
  applyStrokeNormalization([classified]);
  return attachPeaks(classified);
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
 * @param {{ programName?: string, buildJobNumber?: string, pointNames?: Record<number,string>|string[], pointIdentities?: Record<number, object> }} meta
 */
function analyzeForcePaths(rawCurves, meta = {}) {
  let curves = (rawCurves || []).map(classifyCurve);
  curves = applyStrokeNormalization(curves);
  curves = curves.map(attachPeaks);

  const pointNames = meta.pointNames || {};
  const identities = meta.pointIdentities || {};

  const pointIndexSet = new Set();
  for (const c of curves) pointIndexSet.add(c.pointIndex);
  const pointIndices = [...pointIndexSet].sort((a, b) => a - b);

  const points = pointIndices.map((pointIndex) => {
    const identity = identities[pointIndex] || {};
    const name =
      identity.name ||
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
      buildJobNumber: identity.buildJobNumber || meta.buildJobNumber || null,
      plate: identity.plate || null,
      orderUid: identity.orderUid || null,
      toothNumber: identity.toothNumber != null ? identity.toothNumber : null,
      x: identity.x != null ? identity.x : null,
      y: identity.y != null ? identity.y : null,
      zFloor: identity.z != null ? identity.z : null,
      zApproach: identity.zApproach != null ? identity.zApproach : null,
      curves: pointCurves,
      insert: statsForPeaks(insertPeaks),
      extract: statsForPeaks(extractPeaks),
    };
  });

  return {
    programName: meta.programName || null,
    buildJobNumber: meta.buildJobNumber || null,
    curveCount: curves.length,
    points,
    curves,
  };
}

module.exports = {
  LEG_INSERT,
  LEG_EXTRACT,
  peakAbsForce,
  peakTensileForce,
  mean,
  sampleStddev,
  legFromCurveMotion,
  enrichCurve,
  analyzeForcePaths,
};
