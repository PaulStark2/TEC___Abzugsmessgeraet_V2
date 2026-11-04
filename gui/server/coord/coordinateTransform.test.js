'use strict';

const assert = require('assert');
const { toMachinePoint, toMachinePoints, toPlatePoint } = require('./coordinateTransform');

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

/** Test D: FullCalib toolOffset=0 → machine = plateOrigin + platePoint */
function testInstallZeroToolOffset() {
  const calib = {
    plateOriginValid: true,
    plateOrigin: { x: 98, y: 78, z: 28 },
    toolOffset: { x: 0, y: 0, z: 0 },
  };
  const m = toMachinePoint(calib, { x: 10, y: 20, z: 5 });
  assert.ok(m);
  assert.ok(Math.abs(m.x - 108) < 1e-9);
  assert.ok(Math.abs(m.y - 98) < 1e-9);
  assert.ok(Math.abs(m.z - 33) < 1e-9);
}

/** After OffsetOnly deviation: machinePoint = plateOrigin + platePoint + toolOffset */
function testOffsetOnlyTransform() {
  const calib = {
    plateOriginValid: true,
    plateOrigin: { x: 98, y: 78, z: 28 },
    toolOffset: { x: -0.2, y: 0.1, z: -0.1 },
  };
  const m = toMachinePoint(calib, { x: 10, y: 20, z: 5 });
  assert.ok(m);
  assert.ok(Math.abs(m.x - 107.8) < 1e-9);
  assert.ok(Math.abs(m.y - 98.1) < 1e-9);
  assert.ok(Math.abs(m.z - 32.9) < 1e-9);
}

function testBatchNullIfInvalid() {
  assert.strictEqual(
    toMachinePoints({ plateOriginValid: false, plateOrigin: { x: 0, y: 0, z: 0 } }, [
      { x: 1, y: 0, z: 0 },
    ]),
    null
  );
}

function testToPlatePointRoundtrip() {
  const calib = {
    plateOriginValid: true,
    plateOrigin: { x: 10, y: 20, z: 5 },
    toolOffset: { x: 0.5, y: -1, z: 2 },
    plateYawRad: 0.3,
  };
  const plate = { x: 12.5, y: -4.25, z: 7.0 };
  const machine = toMachinePoint(calib, plate);
  assert.ok(machine);
  const back = toPlatePoint(calib, machine);
  assert.ok(back);
  assert.ok(Math.abs(back.x - plate.x) < 1e-9);
  assert.ok(Math.abs(back.y - plate.y) < 1e-9);
  assert.ok(Math.abs(back.z - plate.z) < 1e-9);
}

function testToPlatePointRejectsWithoutOrigin() {
  assert.strictEqual(toPlatePoint({ plateOriginValid: false }, { x: 1, y: 2, z: 3 }), null);
}

testRejectWithoutPlateOrigin();
testTransformMatchesCore();
testInstallZeroToolOffset();
testOffsetOnlyTransform();
testBatchNullIfInvalid();
testToPlatePointRoundtrip();
testToPlatePointRejectsWithoutOrigin();
console.log('coordinateTransform.test.js: PASS');
