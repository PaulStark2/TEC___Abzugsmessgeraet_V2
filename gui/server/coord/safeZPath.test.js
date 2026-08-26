'use strict';

const assert = require('assert');
const { buildApproachSegments, buildProcedurePlannedPath } = require('./safeZPath');

function testRaiseXyCoarseFine() {
  const segs = buildApproachSegments({ x: 0, y: 0, z: 0 }, { x: 10, y: 5, z: 1 }, 40, {
    approachStandoffMm: 15,
    includeFine: true,
  });
  assert.strictEqual(segs.length, 5);
  assert.strictEqual(segs[0].type, 'SafeZ');
  assert.strictEqual(segs[1].type, 'X');
  assert.strictEqual(segs[2].type, 'Y');
  assert.strictEqual(segs[3].type, 'ZCoarse');
  assert.strictEqual(segs[3].to.z, 16);
  assert.strictEqual(segs[4].type, 'ZFine');
  assert.strictEqual(segs[4].to.z, 1);
  assert.strictEqual(segs[4].speedLabel, 'v_fine_testpoints');
}

function testNoDiagonal() {
  const segs = buildApproachSegments({ x: 0, y: 0, z: 40 }, { x: 4, y: 0, z: 2 }, 40, {
    approachStandoffMm: 15,
  });
  assert.strictEqual(segs[0].type, 'X');
  assert.ok(segs[0].to.z === 40);
  for (const s of segs) {
    const dx = Math.abs(s.to.x - s.from.x);
    const dy = Math.abs(s.to.y - s.from.y);
    const dz = Math.abs(s.to.z - s.from.z);
    if (s.type === 'SafeZ' || s.type === 'Z' || s.type === 'ZCoarse' || s.type === 'ZFine') {
      assert.ok(dx < 1e-6 && dy < 1e-6);
    }
    if (s.type === 'X' || s.type === 'Y') {
      assert.ok(dz < 1e-6);
      assert.ok(dx < 1e-6 || dy < 1e-6); // only one horizontal axis
    }
  }
}

function testProcedureIncludesProbeThenPoints() {
  const path = buildProcedurePlannedPath(
    { x: 0, y: 0, z: 40 },
    [{ x: 20, y: 0, z: 1 }],
    40,
    { x: 40, y: 40, z: 5 },
    { approachStandoffMm: 15 }
  );
  assert.ok(path.segments.some((s) => s.phase === 'ProbeApproach'));
  assert.ok(path.segments.some((s) => s.type === 'ZCoarse'));
  assert.ok(path.segments.some((s) => s.phase === 'PointApproach' && s.type === 'ZFine'));
  assert.ok(!path.segments.some((s) => s.phase === 'InsertInitial'));
  assert.ok(!path.segments.some((s) => s.phase === 'MoveSafeEnd' && (s.type === 'X' || s.type === 'Y')));
  for (const s of path.segments) {
    const dx = Math.abs(s.to.x - s.from.x);
    const dy = Math.abs(s.to.y - s.from.y);
    const dz = Math.abs(s.to.z - s.from.z);
    if (s.type === 'SafeZ' || s.type === 'Z' || s.type === 'ZCoarse' || s.type === 'ZFine' || s.type === 'ZMeasure') {
      assert.ok(dx < 1e-6 && dy < 1e-6, s.type);
    }
    if (s.type === 'X' || s.type === 'Y') {
      assert.ok(dz < 1e-6);
    }
  }
}

testRaiseXyCoarseFine();
testNoDiagonal();
testProcedureIncludesProbeThenPoints();
console.log('safeZPath.test.js: PASS');
