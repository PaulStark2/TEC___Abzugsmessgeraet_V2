'use strict';

/**
 * Unit tests for calibrationStore MCS migration / commit / no early invalidate.
 */

const assert = require('assert');
const fs = require('fs');
const path = require('path');

const dataDir = path.join(__dirname, '..', 'data');
const calibFile = path.join(dataDir, 'calibration.json');

function withTempCalib(fn) {
  const backup = fs.existsSync(calibFile) ? fs.readFileSync(calibFile, 'utf8') : null;
  try {
    return fn();
  } finally {
    if (backup !== null) {
      fs.writeFileSync(calibFile, backup, 'utf8');
    } else if (fs.existsSync(calibFile)) {
      fs.unlinkSync(calibFile);
    }
  }
}

const store = require('./calibrationStore');

function testLegacyWithoutMcs() {
  withTempCalib(() => {
    fs.writeFileSync(
      calibFile,
      JSON.stringify({
        plateOrigin: { x: 1, y: 2, z: 3 },
        plateOriginValid: true,
        probeCenter: { x: 4, y: 5, z: 6 },
        probeCenterValid: true,
        toolOffset: { x: 0, y: 0, z: 0 },
      }),
      'utf8'
    );
    const c = store.load();
    assert.strictEqual(c.machineCoordinateSystem.valid, false);
    assert.strictEqual(c.plateOriginValid, false);
    assert.strictEqual(c.probeCenterValid, false);
  });
}

function testCommitReplacesMcsAndClearsGeometry() {
  withTempCalib(() => {
    store.setMachineCoordinateSystem({ x: -2, y: -2, z: -2 });
    store.setPlateOrigin({ x: -10, y: -20, z: -30 });
    store.setProbeCenter({ x: -40, y: -40, z: -10 }, { x: 0, y: 0, z: 0 });
    let c = store.load();
    assert.strictEqual(c.plateOriginValid, true);
    assert.strictEqual(c.probeCenterValid, true);
    assert.strictEqual(c.machineCoordinateSystem.origin, store.MCS_ORIGIN_POS_MAX_SWITCHES);

    // Early invalidate must NOT destroy committed state (no-op).
    const before = fs.readFileSync(calibFile, 'utf8');
    store.invalidateForDefineMcs();
    assert.strictEqual(fs.readFileSync(calibFile, 'utf8'), before);
    c = store.load();
    assert.strictEqual(c.machineCoordinateSystem.valid, true);
    assert.strictEqual(c.plateOriginValid, true);

    // Commit of a new MCS clears geometry.
    store.setMachineCoordinateSystem({ x: -2, y: -2, z: -2 });
    c = store.load();
    assert.strictEqual(c.machineCoordinateSystem.valid, true);
    assert.strictEqual(c.machineCoordinateSystem.origin, store.MCS_ORIGIN_POS_MAX_SWITCHES);
    assert.deepStrictEqual(c.machineCoordinateSystem.homePosition, { x: -2, y: -2, z: -2 });
    assert.strictEqual(c.plateOriginValid, false);
    assert.strictEqual(c.probeCenterValid, false);
  });
}

function testDefineDoesNotTouchDiskViaInvalidate() {
  withTempCalib(() => {
    store.setMachineCoordinateSystem({ x: -2, y: -2, z: -2 });
    store.setPlateOrigin({ x: 1, y: 2, z: 3 });
    const snap = JSON.parse(fs.readFileSync(calibFile, 'utf8'));
    store.invalidateForDefineMcs();
    const after = JSON.parse(fs.readFileSync(calibFile, 'utf8'));
    assert.deepStrictEqual(after.machineCoordinateSystem, snap.machineCoordinateSystem);
    assert.strictEqual(after.plateOriginValid, true);
  });
}

function testLegacyIdentityMcsWithoutOriginIsRejected() {
  withTempCalib(() => {
    fs.writeFileSync(
      calibFile,
      JSON.stringify({
        machineCoordinateSystem: { valid: true, homePosition: { x: 98, y: 98, z: 48 } },
        plateOrigin: { x: 55, y: 57, z: 17 },
        plateOriginValid: true,
        probeCenter: { x: 40, y: 40, z: 10 },
        probeCenterValid: true,
      }),
      'utf8'
    );
    const c = store.load();
    assert.strictEqual(c.machineCoordinateSystem.valid, false);
    assert.strictEqual(c.machineCoordinateSystem.origin, null);
    assert.strictEqual(c.plateOriginValid, false);
    assert.strictEqual(c.probeCenterValid, false);
  });
}

function testLegacyToolOffsetSemantics() {
  withTempCalib(() => {
    store.setMachineCoordinateSystem({ x: -2, y: -2, z: -2 });
    store.setProbeCenter({ x: 50, y: 60, z: 60 }, { x: -10, y: -20, z: -50 });
    assert.strictEqual(store.isLegacyToolOffsetSemantics(store.load()), true);
    store.setProbeCenter({ x: 100, y: 120, z: 40 }, { x: 0, y: 0, z: 0 });
    assert.strictEqual(store.isLegacyToolOffsetSemantics(store.load()), false);
  });
}

function testInstallPersistKeepsProbeCenter() {
  withTempCalib(() => {
    store.setMachineCoordinateSystem({ x: -2, y: -2, z: -2 });
    store.setProbeCenter({ x: 100, y: 120, z: 40 }, { x: 0, y: 0, z: 0 });
    let c = store.load();
    assert.strictEqual(c.probeCenter.x, 100);
    assert.strictEqual(c.toolOffset.z, 0);
    // OffsetOnly persist: update tool only
    store.save({
      ...c,
      toolOffset: { x: -0.2, y: 0.1, z: -0.1 },
    });
    c = store.load();
    assert.strictEqual(c.probeCenter.x, 100);
    assert.strictEqual(c.probeCenter.y, 120);
    assert.strictEqual(c.probeCenter.z, 40);
    assert.ok(Math.abs(c.toolOffset.x - -0.2) < 1e-9);
  });
}

testLegacyWithoutMcs();
testCommitReplacesMcsAndClearsGeometry();
testDefineDoesNotTouchDiskViaInvalidate();
testLegacyIdentityMcsWithoutOriginIsRejected();
testLegacyToolOffsetSemantics();
testInstallPersistKeepsProbeCenter();
console.log('calibrationStore.test.js: PASS');
