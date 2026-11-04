'use strict';

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const {
  derivePointProgress,
  derivePhaseProgress,
  estimateIntraPhaseFill,
  POINT_PHASE_LABELS,
} = require('./procedureProgress');

describe('procedureProgress', () => {
  it('labels Clamp as Spreizer aktivieren in point strip', () => {
    const p = derivePhaseProgress({ procedurePhase: 'Clamp', phaseHints: {} });
    assert.equal(p.phaseCurrent, 'Spreizer aktivieren');
    assert.equal(p.milestoneLabels[2], POINT_PHASE_LABELS[2]);
  });

  it('second Unclamp after ForceTare maps to milestone 10', () => {
    const p = derivePhaseProgress({
      procedurePhase: 'Unclamp',
      phaseHints: { forceTareDone: true },
    });
    assert.equal(p.milestoneIndex, 9);
    assert.equal(p.phaseCurrent, 'Spreizer deaktivieren');
  });

  it('overall percent advances within a point via phase fill', () => {
    const early = derivePointProgress({
      totalPoints: 2,
      index0Based: 0,
      processState: 'Running',
      procedurePhase: 'PointApproach',
      intraFill: 0,
    });
    const mid = derivePointProgress({
      totalPoints: 2,
      index0Based: 0,
      processState: 'Running',
      procedurePhase: 'TestOrAge',
      phaseHints: { forceTareDone: true },
      intraFill: 0.5,
    });
    assert.ok(mid.percent > early.percent);
    assert.ok(mid.percent < 100);
  });

  it('estimates insert fill from Fz vs target', () => {
    const fill = estimateIntraPhaseFill({
      procedurePhase: 'InsertInitial',
      machine: { fz: 5 },
      targetForce: 10,
      motionState: 'Moving',
    });
    assert.ok(fill > 0.4 && fill < 0.6);
  });
});
