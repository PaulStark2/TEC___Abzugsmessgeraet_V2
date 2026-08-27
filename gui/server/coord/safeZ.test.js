'use strict';

const assert = require('assert');
const { computeSafeZPlate, resolveProgramSafeZ } = require('./safeZ');

function testA() {
  const r = computeSafeZPlate([{ z: 0 }, { z: 5 }, { z: 10 }], 30);
  assert.strictEqual(r.safeZPlate, 40);
}

function testB() {
  const r = computeSafeZPlate([{ z: -10 }, { z: -5 }, { z: -2 }], 30);
  assert.strictEqual(r.safeZPlate, 28);
}

function testEmpty() {
  const r = computeSafeZPlate([], 30);
  assert.strictEqual(r.ok, false);
  assert.strictEqual(r.code, 'program_empty');
}

function testMachineTransform() {
  const r = resolveProgramSafeZ([{ z: 10 }], 30, {
    plateOriginValid: true,
    plateOrigin: { x: 0, y: 0, z: 17 },
    toolOffset: { z: 1 },
  });
  assert.strictEqual(r.safeZPlate, 40);
  assert.strictEqual(r.safeZMachine, 58);
}

function testProbeLowerDoesNotRaise() {
  const r = resolveProgramSafeZ([{ z: 10 }], 30, {
    plateOriginValid: true,
    plateOrigin: { x: 0, y: 0, z: 0 },
    toolOffset: { z: 0 },
    probeCenterValid: true,
    probeCenter: { x: 40, y: 40, z: 5 },
  });
  assert.strictEqual(r.ok, true);
  assert.strictEqual(r.maxPointZ, 10);
  assert.strictEqual(r.probeZPlate, 5);
  assert.strictEqual(r.maxClearanceZ, 10);
  assert.strictEqual(r.safeZPlate, 40);
  assert.strictEqual(r.safeZMachine, 40);
}

function testProbeHigherRaisesSafeZ() {
  const r = resolveProgramSafeZ([{ z: 10 }], 30, {
    plateOriginValid: true,
    plateOrigin: { x: 0, y: 0, z: 0 },
    toolOffset: { z: 0 },
    probeCenterValid: true,
    probeCenter: { x: 40, y: 40, z: 25 },
  });
  assert.strictEqual(r.ok, true);
  assert.strictEqual(r.maxPointZ, 10);
  assert.strictEqual(r.probeZPlate, 25);
  assert.strictEqual(r.maxClearanceZ, 25);
  assert.strictEqual(r.safeZPlate, 55);
  assert.strictEqual(r.safeZMachine, 55);
}

testA();
testB();
testEmpty();
testMachineTransform();
testProbeLowerDoesNotRaise();
testProbeHigherRaisesSafeZ();
console.log('safeZ.test.js: PASS');
