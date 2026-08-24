'use strict';

const assert = require('assert');
const { latchMeasuredHome, homeMarkerPose } = require('./homeMarker');

function testLatchOnHomingComplete() {
  const latched = latchMeasuredHome({
    previousHomingState: 'HomingY',
    homingState: 'Homed',
    pose: { x: 2.1, y: 2.2, z: 2.3 },
    previousMeasuredHome: null,
  });
  assert.deepStrictEqual(latched, { x: 2.1, y: 2.2, z: 2.3 });
}

function testDoesNotUseLaterLivePose() {
  const kept = latchMeasuredHome({
    previousHomingState: 'Homed',
    homingState: 'Homed',
    pose: { x: 40, y: 0, z: 8 },
    previousMeasuredHome: { x: 2.1, y: 2.2, z: 2.3 },
  });
  assert.deepStrictEqual(kept, { x: 2.1, y: 2.2, z: 2.3 });
}

function testAlreadyHomedFirstSampleIgnored() {
  const ignored = latchMeasuredHome({
    previousHomingState: null,
    homingState: 'Homed',
    pose: { x: 40, y: 0, z: 8 },
    previousMeasuredHome: null,
  });
  assert.strictEqual(ignored, null);
}

function testMarkerRequiresMeasuredHome() {
  assert.strictEqual(homeMarkerPose({ isHomed: true, measuredHome: null }), null);
  assert.strictEqual(
    homeMarkerPose({ isHomed: true, currentPose: { x: 1, y: 2, z: 3 } }),
    null
  );
  assert.deepStrictEqual(
    homeMarkerPose({ isHomed: true, measuredHome: { x: 2, y: 3, z: 4 } }),
    { x: 2, y: 3, z: 4 }
  );
  assert.strictEqual(
    homeMarkerPose({ isHomed: false, measuredHome: { x: 2, y: 3, z: 4 } }),
    null
  );
}

testLatchOnHomingComplete();
testDoesNotUseLaterLivePose();
testAlreadyHomedFirstSampleIgnored();
testMarkerRequiresMeasuredHome();
console.log('homeMarker.test.js: PASS');
