'use strict';

const assert = require('assert');
const { buildApproachSegments, buildProcedurePlannedPath } = require('./safeZPath');
const {
  buildPointLabels,
  buildSegmentLabels,
  collectOverlayLabels,
} = require('./overlayLabels');

function testPointLabelsUseSameMachinePoints() {
  const points = [{ sequenceNumber: 7, x: 10.25, y: 2, z: 1.5 }];
  const labels = buildPointLabels(points);
  assert.strictEqual(labels.length, 1);
  assert.strictEqual(labels[0].id, '7');
  assert.strictEqual(labels[0].x, 10.25);
  assert.strictEqual(labels[0].y, 2);
  assert.strictEqual(labels[0].z, 1.5);
  assert.ok(labels[0].text.includes('7'));
  assert.ok(labels[0].text.includes('10.3') || labels[0].text.includes('10.2'));
}

function testSegmentLabelsMatchPlannedPath() {
  const segs = buildApproachSegments({ x: 0, y: 0, z: 0 }, { x: 10, y: 5, z: 1 }, 40, {
    approachStandoffMm: 15,
    includeFine: true,
  });
  const config = { v_approach_testpoints: 12, v_fine_testpoints: 2, v_unclamp: 1 };
  const labels = buildSegmentLabels(segs, config);
  assert.strictEqual(labels.length, segs.length);
  assert.strictEqual(labels[0].type, 'SafeZ');
  assert.ok(labels[0].speedText.includes('v_approach_testpoints'));
  assert.ok(labels[0].speedText.includes('12'));
  assert.strictEqual(labels[0].direction, '+Z');
  assert.strictEqual(labels[1].type, 'X');
  assert.ok(labels[1].direction.includes('+10.0'));
  assert.strictEqual(labels[2].type, 'Y');
  assert.ok(labels[2].direction.includes('+5.0'));
  assert.strictEqual(labels[3].type, 'ZCoarse');
  assert.ok(labels[3].speedText.includes('v_approach_testpoints'));
  assert.strictEqual(labels[4].type, 'ZFine');
  assert.strictEqual(labels[4].direction, '−Z');
  assert.ok(labels[4].speedText.includes('v_fine_testpoints'));
  assert.ok(labels[4].speedText.includes('2'));
}

function testToggleHidesLabels() {
  const path = buildProcedurePlannedPath(
    { x: 0, y: 0, z: 10 },
    [{ sequenceNumber: 1, x: 20, y: 0, z: 1 }],
    10,
    null
  );
  const hidden = collectOverlayLabels({
    points: [{ sequenceNumber: 1, x: 20, y: 0, z: 1 }],
    segments: path.segments,
    machineConfig: { v_approach_testpoints: 10, v_fine_testpoints: 2, v_unclamp: 1 },
    showPointLabels: false,
    showSegmentLabels: false,
  });
  assert.strictEqual(hidden.length, 0);

  const pointsOnly = collectOverlayLabels({
    points: [{ sequenceNumber: 1, x: 20, y: 0, z: 1 }],
    segments: path.segments,
    machineConfig: { v_approach_testpoints: 10, v_fine_testpoints: 2, v_unclamp: 1 },
    showPointLabels: true,
    showSegmentLabels: false,
  });
  assert.strictEqual(pointsOnly.length, 1);
  assert.strictEqual(pointsOnly[0].kind, 'point');

  const both = collectOverlayLabels({
    points: [{ sequenceNumber: 1, x: 20, y: 0, z: 1 }],
    segments: path.segments,
    machineConfig: { v_approach_testpoints: 10, v_fine_testpoints: 2, v_unclamp: 1 },
    showPointLabels: true,
    showSegmentLabels: true,
  });
  assert.ok(both.length > 1);
  assert.ok(both.some((l) => l.kind === 'point'));
  assert.ok(both.some((l) => l.kind === 'segment'));
}

testPointLabelsUseSameMachinePoints();
testSegmentLabelsMatchPlannedPath();
testToggleHidesLabels();
console.log('overlayLabels.test.js: PASS');
