'use strict';

const assert = require('assert');
const {
  computeSafeZPlate,
  computeSafeZMachine,
  resolveProgramSafeZ,
} = require('../coord/safeZ');
const { validateProcedureStartPreconditions } = require('./startPreconditions');

function testA_simple() {
  const r = computeSafeZPlate([{ z: 0 }, { z: 5 }, { z: 10 }], 30);
  assert.strictEqual(r.ok, true);
  assert.strictEqual(r.maxPointZ, 10);
  assert.strictEqual(r.safeZPlate, 40);
}

function testB_negativeZ() {
  const r = computeSafeZPlate([{ z: -10 }, { z: -5 }, { z: -2 }], 30);
  assert.strictEqual(r.ok, true);
  assert.strictEqual(r.maxPointZ, -2);
  assert.strictEqual(r.safeZPlate, 28);
}

function testC_programChange() {
  const a = computeSafeZPlate([{ z: 10 }], 30);
  const b = computeSafeZPlate([{ z: 25 }], 30);
  assert.strictEqual(a.safeZPlate, 40);
  assert.strictEqual(b.safeZPlate, 55);
}

function testD_bufferChange() {
  const a = computeSafeZPlate([{ z: 10 }], 30);
  const b = computeSafeZPlate([{ z: 10 }], 50);
  assert.strictEqual(a.safeZPlate, 40);
  assert.strictEqual(b.safeZPlate, 60);
}

function validCalib(extra = {}) {
  return {
    machineCoordinateSystem: {
      valid: true,
      homePosition: { x: 98, y: 98, z: 48 },
    },
    plateOriginValid: true,
    plateOrigin: { x: 0, y: 0, z: 0 },
    probeCenterValid: true,
    probeCenter: { x: 1, y: 1, z: 1 },
    ...extra,
  };
}

function testE_noProgram() {
  const r = validateProcedureStartPreconditions({
    calibration: validCalib(),
    machineConfig: { safe_z_buffer: 30 },
    points: [],
  });
  assert.strictEqual(r.ok, false);
  assert.strictEqual(r.code, 'program_empty');
  assert.ok(r.error.includes('Safe-Z'));
}

function testF_plateOriginDoesNotChangePlateSafeZ() {
  const points = [{ z: 10 }];
  const plate = computeSafeZPlate(points, 30);
  assert.strictEqual(plate.safeZPlate, 40);

  const m0 = computeSafeZMachine(
    { plateOriginValid: true, plateOrigin: { x: 0, y: 0, z: 0 }, toolOffset: { z: 0 } },
    plate.safeZPlate
  );
  const m17 = computeSafeZMachine(
    { plateOriginValid: true, plateOrigin: { x: 0, y: 0, z: 17 }, toolOffset: { z: 0 } },
    plate.safeZPlate
  );
  assert.strictEqual(m0.safeZMachine, 40);
  assert.strictEqual(m17.safeZMachine, 57);
}

function testG_toolOffset() {
  const r = resolveProgramSafeZ([{ z: 10 }], 30, {
    plateOriginValid: true,
    plateOrigin: { x: 0, y: 0, z: 5 },
    toolOffset: { x: 0, y: 0, z: 2 },
  });
  assert.strictEqual(r.ok, true);
  assert.strictEqual(r.safeZPlate, 40);
  assert.strictEqual(r.safeZMachine, 47); // 5 + 40 + 2
}

function testRejectsMissingProbe() {
  const result = validateProcedureStartPreconditions({
    calibration: validCalib({
      probeCenterValid: false,
      probeCenter: null,
    }),
    machineConfig: { safe_z_buffer: 30 },
    points: [{ sequenceNumber: 1, x: 0, y: 0, z: 0 }],
  });
  assert.strictEqual(result.ok, false);
  assert.strictEqual(result.code, 'probe_center');
}

function testRejectsMissingMcs() {
  const result = validateProcedureStartPreconditions({
    calibration: {
      machineCoordinateSystem: { valid: false, homePosition: null },
      plateOriginValid: true,
      plateOrigin: { x: 0, y: 0, z: 0 },
      probeCenterValid: true,
      probeCenter: { x: 1, y: 1, z: 1 },
    },
    machineConfig: { safe_z_buffer: 30 },
    points: [{ sequenceNumber: 1, x: 0, y: 0, z: 0 }],
  });
  assert.strictEqual(result.ok, false);
  assert.strictEqual(result.code, 'mcs');
}

function testAcceptsValidProgram() {
  const result = validateProcedureStartPreconditions({
    calibration: validCalib({
      plateOrigin: { x: 55, y: 57, z: 17 },
      probeCenter: { x: 40, y: 40, z: 10 },
      toolOffset: { x: 0, y: 0, z: 0 },
    }),
    machineConfig: { safe_z_buffer: 30 },
    points: [{ sequenceNumber: 1, x: 0, y: 0, z: 0 }],
  });
  assert.strictEqual(result.ok, true);
  assert.strictEqual(result.safeZ.safeZPlate, 30);
  assert.strictEqual(result.safeZ.safeZMachine, 47);
}

testA_simple();
testB_negativeZ();
testC_programChange();
testD_bufferChange();
testE_noProgram();
testF_plateOriginDoesNotChangePlateSafeZ();
testG_toolOffset();
testRejectsMissingProbe();
testRejectsMissingMcs();
testAcceptsValidProgram();
console.log('startPreconditions.test.js: PASS');
