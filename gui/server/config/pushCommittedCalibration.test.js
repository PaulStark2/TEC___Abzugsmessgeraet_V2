'use strict';

/**
 * T3-style: rollback pushes committed MCS+geometry; disk unchanged by define.
 */

const assert = require('assert');
const fs = require('fs');
const path = require('path');

const dataDir = path.join(__dirname, '..', 'data');
const calibFile = path.join(dataDir, 'calibration.json');
const store = require('./calibrationStore');
const { pushCommittedCalibration } = require('./pushCommittedCalibration');
const { MCS_REQUIRED_MESSAGE } = require('./mcsMessages');

async function withTempCalibAsync(fn) {
  const backup = fs.existsSync(calibFile) ? fs.readFileSync(calibFile, 'utf8') : null;
  try {
    return await fn();
  } finally {
    if (backup !== null) {
      fs.writeFileSync(calibFile, backup, 'utf8');
    } else if (fs.existsSync(calibFile)) {
      fs.unlinkSync(calibFile);
    }
  }
}

async function testPushRestoresCommitted() {
  await withTempCalibAsync(async () => {
    store.setMachineCoordinateSystem({ x: 98, y: 98, z: 48 });
    store.setPlateOrigin({ x: 55, y: 57, z: 17 });
    store.setProbeCenter({ x: 40, y: 40, z: 10 }, { x: 0.1, y: 0.2, z: 0.3 });
    const diskBefore = fs.readFileSync(calibFile, 'utf8');

    const calls = [];
    const client = {
      async restoreMcs(x, y, z) {
        calls.push(['restoreMcs', x, y, z]);
        return { ok: true };
      },
      async setPlateOrigin(x, y, z) {
        calls.push(['setPlateOrigin', x, y, z]);
        return { ok: true };
      },
      async setProbeCenter(x, y, z) {
        calls.push(['setProbeCenter', x, y, z]);
        return { ok: true };
      },
    };

    const result = await pushCommittedCalibration(client);
    assert.strictEqual(result.ok, true);
    assert.strictEqual(result.restored, true);
    assert.deepStrictEqual(calls[0], ['restoreMcs', 98, 98, 48]);
    assert.deepStrictEqual(calls[1], ['setPlateOrigin', 55, 57, 17]);
    assert.deepStrictEqual(calls[2], ['setProbeCenter', 40, 40, 10]);
    assert.strictEqual(fs.readFileSync(calibFile, 'utf8'), diskBefore);
  });
}

async function testPushNoMcsIsNoOp() {
  await withTempCalibAsync(async () => {
    fs.writeFileSync(
      calibFile,
      JSON.stringify({
        machineCoordinateSystem: { valid: false, homePosition: null },
        plateOriginValid: false,
        probeCenterValid: false,
      }),
      'utf8'
    );
    const calls = [];
    const client = {
      async restoreMcs() {
        calls.push('restore');
        return { ok: true };
      },
    };
    const result = await pushCommittedCalibration(client);
    assert.strictEqual(result.ok, true);
    assert.strictEqual(result.restored, false);
    assert.strictEqual(result.reason, 'no_committed_mcs');
    assert.strictEqual(calls.length, 0);
    assert.ok(MCS_REQUIRED_MESSAGE.includes('Maschinenkoordinatensystem'));
  });
}

async function main() {
  await testPushRestoresCommitted();
  await testPushNoMcsIsNoOp();
  console.log('pushCommittedCalibration.test.js: PASS');
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
