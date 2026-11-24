'use strict';

const { validateTestProgram } = require('../server/program/validateTestProgram');
const { rawJob } = require('../server/program/sampleJob');

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

function testAcceptsCompanyFileShape() {
  const out = validateTestProgram({
    schemaVersion: 2,
    unit: 'mm',
    coordinateSystem: { xyOrigin: 'ignored' },
    calculation: { floorClearanceMM: 2 },
    complete: true,
    job: {
      buildJobNumber: '9516',
      plates: [
        {
          plate: 'A',
          orders: [
            {
              orderUid: 'FID-1',
              teeth: [
                { toothNumber: 34, point: { x: 1, y: 2, z: 3 } },
                { toothNumber: 12, point: { x: 4, y: 5, z: 6 } },
              ],
            },
          ],
        },
      ],
    },
  });
  assert(out.ok === true, 'company file should pass');
  assert(out.value.buildJobNumber === '9516', 'build job');
  assert(out.value.testPoints.length === 2, 'two teeth');
  assert(out.value.testPoints[0].toothNumber === 34, 'file order kept on the flat list');
  assert(out.value.programName === '9516', 'programName aliases the build job');
}

function testRejectsFlatFormat() {
  const out = validateTestProgram({
    version: 1,
    programName: 'Alt',
    testPoints: [{ name: 'A', sequenceNumber: 1, x: 0, y: 0, z: 0 }],
  });
  assert(out.ok === false, 'flat format rejected');
  assert(out.errors.some((e) => e.path === 'job'), 'reports missing job');
}

function testRejectsDuplicateTooth() {
  const out = validateTestProgram(
    rawJob([
      { toothNumber: 11, x: 0, y: 0, z: 1 },
      { toothNumber: 11, x: 1, y: 0, z: 1 },
    ])
  );
  assert(out.ok === false, 'duplicate tooth rejected');
}

function testAllowsMoreThan32Teeth() {
  const teeth = [];
  for (let i = 1; i <= 40; i += 1) {
    teeth.push({ toothNumber: i, x: i, y: 0, z: 1 });
  }
  const out = validateTestProgram(rawJob(teeth));
  assert(out.ok === true, 'file may exceed 32 teeth');
  assert(out.value.testPoints.length === 40, 'all teeth kept');
}

function run() {
  testAcceptsCompanyFileShape();
  testRejectsFlatFormat();
  testRejectsDuplicateTooth();
  testAllowsMoreThan32Teeth();
  console.log('G5 validateTestProgram: PASS');
}

try {
  run();
} catch (err) {
  console.error('G5 validateTestProgram: FAIL', err.message);
  process.exit(1);
}
