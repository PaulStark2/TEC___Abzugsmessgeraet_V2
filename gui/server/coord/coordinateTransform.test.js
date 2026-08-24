'use strict';

const assert = require('assert');
const { toMachinePoint, toMachinePoints } = require('./coordinateTransform');

function testRejectWithoutPlateOrigin() {
  assert.strictEqual(toMachinePoint({ plateOriginValid: false }, { x: 1, y: 2, z: 3 }), null);
  assert.strictEqual(toMachinePoint({}, { x: 1, y: 2, z: 3 }), null);
}

function testTransformMatchesCore() {
  const calib = {
    plateOriginValid: true,
    plateOrigin: { x: 10, y: 20, z: 5 },
    toolOffset: { x: 0.5, y: -1, z: 2 },
  };
  const m = toMachinePoint(calib, { x: 1, y: 2, z: 3 });
  assert.ok(m);
  assert.ok(Math.abs(m.x - 11.5) < 1e-9);
  assert.ok(Math.abs(m.y - 21) < 1e-9);
  assert.ok(Math.abs(m.z - 10) < 1e-9);
}

function testBatchNullIfInvalid() {
  assert.strictEqual(
    toMachinePoints({ plateOriginValid: false, plateOrigin: { x: 0, y: 0, z: 0 } }, [
      { x: 1, y: 0, z: 0 },
    ]),
    null
  );
}

testRejectWithoutPlateOrigin();
testTransformMatchesCore();
testBatchNullIfInvalid();
console.log('coordinateTransform.test.js: PASS');
