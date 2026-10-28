'use strict';

const assert = require('node:assert/strict');
const {
  peakAbsForce,
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

function testAnalyze() {
  const analysis = analyzeForcePaths(
    [
      {
        pointIndex: 0,
        iteration: 0,
        leg: LEG_INSERT,
        samples: [
          { z: 10, fz: -1 },
          { z: 9.5, fz: -2.5 },
          { z: 9.0, fz: -2.0 },
        ],
      },
      {
        pointIndex: 0,
        iteration: 0,
        leg: LEG_EXTRACT,
        samples: [
          { z: 9.0, fz: -2.2 },
          { z: 9.5, fz: -0.5 },
        ],
      },
      {
        pointIndex: 0,
        iteration: 1,
        leg: LEG_INSERT,
        samples: [
          { z: 10, fz: -1.1 },
          { z: 9.4, fz: -2.7 },
        ],
      },
    ],
    { programName: 'Demo', pointNames: { 0: 'P1' } }
  );
  assert.equal(analysis.points.length, 1);
  const p = analysis.points[0];
  assert.equal(p.name, 'P1');
  assert.equal(p.insert.count, 2);
  assert.ok(Math.abs(p.insert.meanPeakN - (2.5 + 2.7) / 2) < 1e-9);
  assert.equal(p.extract.count, 1);
  assert.equal(p.extract.stddevPeakN, null);
  assert.ok(Math.abs(p.curves[0].samples[1].zRel - -0.5) < 1e-9);
}

testPeakAndStats();
testAnalyze();
console.log('forcePathAnalysis.test.js OK');
