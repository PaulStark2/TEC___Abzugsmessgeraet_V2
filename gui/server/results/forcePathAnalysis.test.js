'use strict';

const assert = require('node:assert/strict');
const {
  peakAbsForce,
  peakTensileForce,
  mean,
  sampleStddev,
  analyzeForcePaths,
  LEG_INSERT,
  LEG_EXTRACT,
} = require('./forcePathAnalysis');

function testPeakAndStats() {
  const { peakN, peakIndex } = peakAbsForce([
    { fz: -1.2 },
    { fz: 0.5 },
    { fz: -3.4 },
    { fz: 2.0 },
  ]);
  assert.equal(peakN, 3.4);
  assert.equal(peakIndex, 2);
  assert.equal(mean([2, 4, 6]), 4);
  const sd = sampleStddev([2, 4, 6]);
  assert.ok(Math.abs(sd - Math.sqrt(4)) < 1e-9);
  assert.equal(sampleStddev([1]), null);
}

function testTensilePeakIgnoresResidualPressure() {
  const samples = [
    { fz: 4.0 }, // residual Druck at seat
    { fz: 1.5 },
    { fz: 0.1 },
    { fz: -0.2 },
    { fz: -3.1 }, // true Zug peak
    { fz: -0.5 },
  ];
  const all = peakAbsForce(samples);
  assert.equal(all.peakN, 4.0);
  const zug = peakTensileForce(samples);
  assert.equal(zug.peakN, 3.1);
  assert.equal(zug.peakIndex, 4);
  assert.equal(peakTensileForce([{ fz: 1 }, { fz: 2 }]).peakN, null);
}

function testAnalyzeOverlayAndExtractPeak() {
  // Shared stroke: free z=10, seat z=8 → zRel in [-2, 0]
  const analysis = analyzeForcePaths(
    [
      {
        pointIndex: 0,
        iteration: 0,
        leg: LEG_INSERT,
        samples: [
          { z: 10, fz: 0.1 },
          { z: 9.0, fz: 2.0 },
          { z: 8.0, fz: 3.5 },
        ],
      },
      {
        pointIndex: 0,
        iteration: 0,
        leg: LEG_EXTRACT,
        samples: [
          { z: 8.0, fz: 3.2 }, // residual Druck — must not set extract peak
          { z: 9.0, fz: -0.5 },
          { z: 9.5, fz: -2.8 },
          { z: 10.0, fz: -0.1 },
        ],
      },
      {
        pointIndex: 0,
        iteration: 1,
        leg: LEG_INSERT,
        samples: [
          { z: 10.05, fz: 0.1 },
          { z: 8.05, fz: 3.7 },
        ],
      },
      {
        pointIndex: 0,
        iteration: 1,
        leg: LEG_EXTRACT,
        samples: [
          { z: 8.05, fz: 3.0 },
          { z: 10.05, fz: -2.6 },
        ],
      },
    ],
    { programName: 'Demo', pointNames: { 0: 'P1' } }
  );
  assert.equal(analysis.points.length, 1);
  const p = analysis.points[0];
  assert.equal(p.insert.count, 2);
  assert.ok(Math.abs(p.insert.meanPeakN - (3.5 + 3.7) / 2) < 1e-9);
  assert.equal(p.extract.count, 2);
  assert.ok(Math.abs(p.extract.meanPeakN - (2.8 + 2.6) / 2) < 1e-9);

  const ex0 = p.curves.find((c) => c.iteration === 0 && c.leg === LEG_EXTRACT);
  assert.equal(ex0.peakN, 2.8);
  assert.ok(Math.abs(ex0.samples[0].zRel - -2.0) < 1e-9);
  assert.ok(Math.abs(ex0.samples[ex0.samples.length - 1].zRel - 0) < 1e-9);

  const in0 = p.curves.find((c) => c.iteration === 0 && c.leg === LEG_INSERT);
  assert.ok(Math.abs(in0.samples[0].zRel - 0) < 1e-9);
  assert.ok(Math.abs(in0.samples[in0.samples.length - 1].zRel - -2.0) < 1e-9);
}

testPeakAndStats();
testTensilePeakIgnoresResidualPressure();
testAnalyzeOverlayAndExtractPeak();
console.log('forcePathAnalysis.test.js OK');
