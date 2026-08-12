'use strict';

const { validateTestProgram } = require('../server/program/validateTestProgram');

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

function baseValidProgram() {
  return {
    version: 1,
    programName: 'Beispielprüfung',
    testPoints: [
      { name: 'Punkt 1', sequenceNumber: 1, x: 10.0, y: 20.0, z: -5.0 },
    ],
  };
}

function testValid_singlePoint() {
  const input = baseValidProgram();
  const out = validateTestProgram(input);
  assert(out.ok === true, 'single point should validate');
  assert(out.value.version === 1, 'version must be preserved');
  assert(out.value.testPoints.length === 1, 'exactly one point expected');
  assert(out.value.testPoints[0].sequenceNumber === 1, 'sequenceNumber must match');
}

function testValid_maxPoints32() {
  const pts = [];
  for (let i = 0; i < 32; i += 1) {
    pts.push({
      name: `P${i + 1}`,
      sequenceNumber: i,
      x: i,
      y: i + 0.1,
      z: -i,
    });
  }

  const out = validateTestProgram({
    testPoints: pts,
  });

  assert(out.ok === true, '32 points should validate');
  assert(out.value.version === 1, 'missing version must default to 1');
  assert(out.value.testPoints.length === 32, 'exactly 32 points expected');
}

function testInvalid_missingTestPoints() {
  const out = validateTestProgram({ version: 1, programName: 'X' });
  assert(out.ok === false, 'missing testPoints should fail');
  assert(out.errors.some((e) => e.path === 'testPoints'), 'should report testPoints error');
}

function testInvalid_testPointsNotArray() {
  const out = validateTestProgram({ testPoints: {} });
  assert(out.ok === false, 'testPoints not array should fail');
}

function testInvalid_emptyArray() {
  const out = validateTestProgram({ testPoints: [] });
  assert(out.ok === false, 'empty array should fail');
}

function testInvalid_missingName() {
  const out = validateTestProgram({ testPoints: [{ sequenceNumber: 0, x: 0, y: 0, z: 0 }] });
  assert(out.ok === false, 'missing name should fail');
}

function testInvalid_missingSequenceNumber() {
  const out = validateTestProgram({ testPoints: [{ name: 'P', x: 0, y: 0, z: 0 }] });
  assert(out.ok === false, 'missing sequenceNumber should fail');
}

function testInvalid_nonIntegerSequenceNumber() {
  const out = validateTestProgram({
    testPoints: [{ name: 'P', sequenceNumber: 1.2, x: 0, y: 0, z: 0 }],
  });
  assert(out.ok === false, 'non-integer sequenceNumber should fail');
}

function testInvalid_negativeSequenceNumber() {
  const out = validateTestProgram({
    testPoints: [{ name: 'P', sequenceNumber: -1, x: 0, y: 0, z: 0 }],
  });
  assert(out.ok === false, 'negative sequenceNumber should fail');
}

function testInvalid_duplicateSequenceNumber() {
  const out = validateTestProgram({
    testPoints: [
      { name: 'A', sequenceNumber: 1, x: 0, y: 0, z: 0 },
      { name: 'B', sequenceNumber: 1, x: 1, y: 1, z: 1 },
    ],
  });
  assert(out.ok === false, 'duplicate sequenceNumber should fail');
  assert(
    out.errors.some((e) => String(e.path).includes('sequenceNumber')),
    'should report duplicated sequenceNumber'
  );
}

function testInvalid_missingAxis() {
  const out = validateTestProgram({
    testPoints: [{ name: 'A', sequenceNumber: 0, x: 0, y: 0 }], // missing z
  });
  assert(out.ok === false, 'missing z should fail');
}

function testInvalid_nonNumericAxis() {
  const out = validateTestProgram({
    testPoints: [{ name: 'A', sequenceNumber: 0, x: 'nope', y: 0, z: 0 }],
  });
  assert(out.ok === false, 'non numeric x should fail');
}

function testInvalid_NaNInfinity() {
  const outNaN = validateTestProgram({
    testPoints: [{ name: 'A', sequenceNumber: 0, x: NaN, y: 0, z: 0 }],
  });
  assert(outNaN.ok === false, 'NaN should fail');

  const outInf = validateTestProgram({
    testPoints: [{ name: 'A', sequenceNumber: 0, x: Infinity, y: 0, z: 0 }],
  });
  assert(outInf.ok === false, 'Infinity should fail');
}

function testInvalid_moreThan32Points() {
  const pts = [];
  for (let i = 0; i < 33; i += 1) {
    pts.push({ name: `P${i}`, sequenceNumber: i, x: i, y: i, z: i });
  }
  const out = validateTestProgram({ testPoints: pts });
  assert(out.ok === false, '33 points should fail');
  assert(out.errors.some((e) => e.path === 'testPoints'), 'should report testPoints size');
}

function testInvalid_unknownPropertyTypos() {
  const out = validateTestProgram({
    testPoints: [{ name: 'P', sequenceNumber: 0, sequnceNumber: 1, x: 0, y: 0, z: 0 }],
  });
  assert(out.ok === false, 'unknown property should fail');
}

async function run() {
  testValid_singlePoint();
  testValid_maxPoints32();

  testInvalid_missingTestPoints();
  testInvalid_testPointsNotArray();
  testInvalid_emptyArray();
  testInvalid_missingName();
  testInvalid_missingSequenceNumber();
  testInvalid_nonIntegerSequenceNumber();
  testInvalid_negativeSequenceNumber();
  testInvalid_duplicateSequenceNumber();
  testInvalid_missingAxis();
  testInvalid_nonNumericAxis();
  testInvalid_NaNInfinity();
  testInvalid_moreThan32Points();
  testInvalid_unknownPropertyTypos();

  console.log('G5 validateTestProgram: PASS');
}

run().catch((err) => {
  console.error('G5 validateTestProgram: FAIL', err.message);
  process.exit(1);
});

