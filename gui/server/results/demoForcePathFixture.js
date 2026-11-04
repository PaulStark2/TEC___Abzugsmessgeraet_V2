'use strict';

/**
 * Realistic Kraft-Weg demo curves for offline GUI validation (no test stand).
 * Physics is simplified: smooth contact ramps + light noise, not a full contact model.
 */

const { LEG_INSERT, LEG_EXTRACT } = require('./forcePathStore');
const { analyzeForcePaths } = require('./forcePathAnalysis');

function clamp(v, lo, hi) {
  return Math.max(lo, Math.min(hi, v));
}

function smoothstep(t) {
  const x = clamp(t, 0, 1);
  return x * x * (3 - 2 * x);
}

/** Deterministic pseudo-noise in [-1, 1]. */
function noise(seed) {
  const x = Math.sin(seed * 12.9898) * 43758.5453;
  return (x - Math.floor(x)) * 2 - 1;
}

/**
 * @param {{ zFree: number, strokeMm: number, dz: number, insertPeakN: number, extractPeakN: number, residualDruckN: number, seed: number }} opts
 */
function buildInsertSamples(opts) {
  const { zFree, strokeMm, dz, insertPeakN, seed } = opts;
  const zSeat = zFree - strokeMm;
  const samples = [];
  for (let z = zFree, i = 0; z >= zSeat - 1e-9; z -= dz, ++i) {
    const progress = (zFree - z) / strokeMm; // 0 → 1 toward seat
    // Contact builds after ~15% stroke, peaks near 85%, slight settle at seat.
    let fz = 0;
    if (progress < 0.12) {
      fz = 0.05 + 0.15 * progress;
    } else if (progress < 0.85) {
      fz = insertPeakN * smoothstep((progress - 0.12) / 0.73);
    } else {
      const settle = smoothstep((progress - 0.85) / 0.15);
      fz = insertPeakN * (1 - 0.12 * settle);
    }
    fz += 0.04 * noise(seed + i * 0.37);
    samples.push({ z: Number(z.toFixed(4)), fz: Number(fz.toFixed(4)) });
  }
  return samples;
}

/**
 * Extract starts at seat under residual Druck, crosses into Zug, ends kraftfrei.
 */
function buildExtractSamples(opts) {
  const { zFree, strokeMm, dz, extractPeakN, residualDruckN, seed } = opts;
  const zSeat = zFree - strokeMm;
  const samples = [];
  for (let z = zSeat, i = 0; z <= zFree + 1e-9; z += dz, ++i) {
    const progress = (z - zSeat) / strokeMm; // 0 at seat → 1 at free
    let fz;
    if (progress < 0.18) {
      // Residual Druck decays toward zero-crossing
      fz = residualDruckN * (1 - smoothstep(progress / 0.18));
    } else if (progress < 0.72) {
      const t = (progress - 0.18) / 0.54;
      // Zug peak around mid of tensile phase
      const envelope = Math.sin(Math.PI * t);
      fz = -extractPeakN * envelope;
    } else {
      // Return to kraftfrei
      const t = (progress - 0.72) / 0.28;
      fz = -extractPeakN * 0.15 * (1 - smoothstep(t));
    }
    fz += 0.04 * noise(seed + 1000 + i * 0.41);
    samples.push({ z: Number(z.toFixed(4)), fz: Number(fz.toFixed(4)) });
  }
  return samples;
}

/**
 * Two points × three iterations with slightly varying peaks / stroke.
 * Sampling ~0.05 mm keeps the chart responsive while looking dense enough.
 */
function buildDemoRawCurves() {
  const pointSpecs = [
    {
      pointIndex: 0,
      name: 'Kontakt A',
      zFree: 12.0,
      baseStroke: 2.05,
      insertPeaks: [3.85, 3.92, 4.08],
      extractPeaks: [2.95, 3.05, 2.88],
      residual: [2.4, 2.55, 2.35],
    },
    {
      pointIndex: 1,
      name: 'Kontakt B',
      zFree: 11.85,
      baseStroke: 1.92,
      insertPeaks: [3.15, 3.28, 3.22],
      extractPeaks: [2.42, 2.55, 2.48],
      residual: [1.9, 2.05, 1.95],
    },
  ];

  const dz = 0.05;
  const curves = [];

  for (const spec of pointSpecs) {
    for (let iter = 0; iter < 3; ++iter) {
      const strokeMm = spec.baseStroke + 0.04 * iter;
      const zFree = spec.zFree + 0.02 * iter;
      const common = {
        zFree,
        strokeMm,
        dz,
        insertPeakN: spec.insertPeaks[iter],
        extractPeakN: spec.extractPeaks[iter],
        residualDruckN: spec.residual[iter],
        seed: spec.pointIndex * 100 + iter * 17,
      };
      curves.push({
        pointIndex: spec.pointIndex,
        iteration: iter,
        leg: LEG_INSERT,
        wireLeg: LEG_INSERT,
        dropped: 0,
        samples: buildInsertSamples(common),
      });
      curves.push({
        pointIndex: spec.pointIndex,
        iteration: iter,
        leg: LEG_EXTRACT,
        wireLeg: LEG_EXTRACT,
        dropped: 0,
        samples: buildExtractSamples(common),
      });
    }
  }

  return { curves, pointNames: Object.fromEntries(pointSpecs.map((p) => [p.pointIndex, p.name])) };
}

function buildDemoResults() {
  const { curves, pointNames } = buildDemoRawCurves();
  const programName = 'Demo — Kraft-Weg (ohne Prüfstand)';
  const analysis = analyzeForcePaths(curves, { programName, pointNames });
  return {
    programName,
    mode: 'test',
    completedAt: new Date().toISOString(),
    backend: 'demo',
    params: { mode: 'test', iterations: 3, demo: true },
    dataCompleteness: 'complete',
    notes: [
      'Demo-Testdaten zur GUI-Validierung. Keine realen Messwerte vom Prüfstand.',
      'Weg: 0 = kraftfrei; negativ = Richtung Sitz. Ausglieder-Spitze nur Zug (Fz < 0).',
    ],
    processState: 'Completed',
    procedurePhase: null,
    analysis,
    curves: analysis.curves,
    rows: [],
  };
}

module.exports = {
  buildDemoRawCurves,
  buildDemoResults,
  LEG_INSERT,
  LEG_EXTRACT,
};
