'use strict';

const assert = require('assert');
const {
  liveWorkspaceOffset,
  shouldRefreshWorkspaceOffset,
  latchWorkspaceReferenceOffset,
  pickWorkspaceReferenceOffset,
} = require('./workspaceReferenceOffset');

function testLiveOffset() {
  const live = liveWorkspaceOffset({ x: 98, y: 98, z: 48 }, { x: -2, y: -2, z: -2 });
  assert.deepStrictEqual(live, { x: 100, y: 100, z: 50 });
  assert.strictEqual(liveWorkspaceOffset(null, { x: 0, y: 0, z: 0 }), null);
}

function testRefreshRules() {
  assert.strictEqual(
    shouldRefreshWorkspaceOffset({
      motionState: 'Idle',
      processState: 'Idle',
      homingState: 'Homed',
      previousHomingState: 'Homed',
      hasLatchedOffset: true,
    }),
    true
  );
  assert.strictEqual(
    shouldRefreshWorkspaceOffset({
      motionState: 'Moving',
      processState: 'Idle',
      homingState: 'Homed',
      previousHomingState: 'Homed',
      hasLatchedOffset: true,
    }),
    false
  );
  assert.strictEqual(
    shouldRefreshWorkspaceOffset({
      motionState: 'Idle',
      processState: 'Running',
      homingState: 'Homed',
      previousHomingState: 'Homed',
      hasLatchedOffset: true,
    }),
    false
  );
  assert.strictEqual(
    shouldRefreshWorkspaceOffset({
      motionState: 'Moving',
      processState: 'Idle',
      homingState: 'HomingX',
      previousHomingState: 'HomingZ',
      hasLatchedOffset: true,
    }),
    true
  );
  assert.strictEqual(
    shouldRefreshWorkspaceOffset({
      motionState: 'Idle',
      processState: 'Idle',
      homingState: 'Homed',
      previousHomingState: 'HomingY',
      hasLatchedOffset: true,
    }),
    true
  );
  assert.strictEqual(
    shouldRefreshWorkspaceOffset({
      motionState: 'Moving',
      processState: 'Idle',
      homingState: 'Homed',
      previousHomingState: 'Homed',
      hasLatchedOffset: false,
    }),
    true
  );
}

function testLatchFreezesWhileMoving() {
  const rest = latchWorkspaceReferenceOffset({
    plantPose: { x: 98, y: 98, z: 48 },
    statusPose: { x: -2, y: -2, z: -2 },
    previousOffset: null,
    motionState: 'Idle',
    processState: 'Idle',
    homingState: 'Homed',
    previousHomingState: 'Homed',
  });
  assert.deepStrictEqual(rest, { x: 100, y: 100, z: 50 });

  const moving = latchWorkspaceReferenceOffset({
    plantPose: { x: 90, y: 98, z: 48 },
    statusPose: { x: -12, y: -2, z: -2 },
    previousOffset: rest,
    motionState: 'Moving',
    processState: 'Idle',
    homingState: 'Homed',
    previousHomingState: 'Homed',
  });
  assert.deepStrictEqual(moving, { x: 100, y: 100, z: 50 });

  const liveWouldBe = liveWorkspaceOffset({ x: 90, y: 98, z: 48 }, { x: -15, y: -2, z: -2 });
  assert.notDeepStrictEqual(liveWouldBe, moving);
}

function testMissingPlantKeepsPrevious() {
  const kept = latchWorkspaceReferenceOffset({
    plantPose: null,
    statusPose: { x: -2, y: -2, z: -2 },
    previousOffset: { x: 100, y: 100, z: 50 },
    motionState: 'Idle',
    processState: 'Idle',
    homingState: 'Homed',
    previousHomingState: 'Homed',
  });
  assert.deepStrictEqual(kept, { x: 100, y: 100, z: 50 });
}

testLiveOffset();
testRefreshRules();
testLatchFreezesWhileMoving();
testMissingPlantKeepsPrevious();

function testPickPrefersCoreOffset() {
  const core = { x: 100, y: 100, z: 50 };
  const guessed = { x: 92, y: 100, z: 50 };
  assert.deepStrictEqual(pickWorkspaceReferenceOffset(core, guessed), core);
  assert.deepStrictEqual(pickWorkspaceReferenceOffset(null, guessed), guessed);
  assert.deepStrictEqual(pickWorkspaceReferenceOffset(undefined, null), { x: 0, y: 0, z: 0 });
}

testPickPrefersCoreOffset();
console.log('workspaceReferenceOffset.test.js: PASS');
