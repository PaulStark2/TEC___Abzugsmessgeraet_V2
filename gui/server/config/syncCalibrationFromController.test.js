'use strict';

/**
 * Persistenz: FullCalib Install + OffsetOnly tool-only sync, reload, legacy reference ignored.
 */

const assert = require('assert');
const fs = require('fs');
const path = require('path');

const dataDir = path.join(__dirname, '..', 'data');
const calibFile = path.join(dataDir, 'calibration.json');
const cfgFile = path.join(dataDir, 'machine-config.json');

const calibrationStore = require('./calibrationStore');
const machineConfigStore = require('./machineConfigStore');
const { syncCalibrationFromController } = require('./syncCalibrationFromController');
const { toMachinePoint } = require('../coord/coordinateTransform');

function withTempFiles(fn) {
  const calibBackup = fs.existsSync(calibFile) ? fs.readFileSync(calibFile, 'utf8') : null;
  const cfgBackup = fs.existsSync(cfgFile) ? fs.readFileSync(cfgFile, 'utf8') : null;
  return Promise.resolve()
    .then(() => fn())
    .finally(() => {
      if (calibBackup !== null) {
        fs.writeFileSync(calibFile, calibBackup, 'utf8');
      } else if (fs.existsSync(calibFile)) {
        fs.unlinkSync(calibFile);
      }
      if (cfgBackup !== null) {
        fs.writeFileSync(cfgFile, cfgBackup, 'utf8');
      } else if (fs.existsSync(cfgFile)) {
        fs.unlinkSync(cfgFile);
      }
    });
}

function near(a, b, eps = 1e-6) {
  return Math.abs(Number(a) - Number(b)) <= eps;
}

async function testFullCalibPersistReload() {
  await withTempFiles(async () => {
    calibrationStore.setMachineCoordinateSystem({ x: 100, y: 100, z: 50 });
    machineConfigStore.save({
      ...machineConfigStore.load(),
      probe_offset_reference_x: 40,
      probe_offset_reference_y: 40,
      probe_offset_reference_z: 10,
      tool_offset_x: 99,
      tool_offset_y: 99,
      tool_offset_z: 99,
    });

    const client = {
      getStatus: () => ({ connected: true, probeCenterValid: true }),
      async getMachineConfig() {
        return {
          ok: true,
          config: {
            tool_offset_x: 0,
            tool_offset_y: 0,
            tool_offset_z: 0,
            probe_center_x: 100,
            probe_center_y: 120,
            probe_center_z: 40,
          },
        };
      },
    };

    const result = await syncCalibrationFromController(client, { persistProbeCenter: true });
    assert.strictEqual(result.ok, true);
    assert.strictEqual(result.persistProbeCenter, true);

    // Reload from disk (simulate restart).
    const calib = calibrationStore.load();
    const cfg = machineConfigStore.load();
    assert.ok(near(calib.probeCenter.x, 100));
    assert.ok(near(calib.probeCenter.y, 120));
    assert.ok(near(calib.probeCenter.z, 40));
    assert.ok(near(calib.toolOffset.x, 0));
    assert.ok(near(calib.toolOffset.y, 0));
    assert.ok(near(calib.toolOffset.z, 0));
    assert.ok(near(cfg.tool_offset_x, 0));
    assert.ok(near(cfg.tool_offset_y, 0));
    assert.ok(near(cfg.tool_offset_z, 0));
    // Must NOT become legacy reference (40,40,10).
    assert.ok(!near(calib.probeCenter.x, 40) || !near(calib.probeCenter.y, 40));
    assert.ok(near(cfg.probe_offset_reference_x, 40));
  });
}

async function testOffsetOnlyPersistReloadIgnoresLegacyReference() {
  await withTempFiles(async () => {
    calibrationStore.setMachineCoordinateSystem({ x: 100, y: 100, z: 50 });
    calibrationStore.setProbeCenter({ x: 100, y: 120, z: 40 }, { x: 0, y: 0, z: 0 });
    machineConfigStore.save({
      ...machineConfigStore.load(),
      probe_offset_reference_x: 40,
      probe_offset_reference_y: 40,
      probe_offset_reference_z: 10,
      tool_offset_x: 0,
      tool_offset_y: 0,
      tool_offset_z: 0,
    });

    const client = {
      getStatus: () => ({ connected: true, probeCenterValid: true }),
      async getMachineConfig() {
        return {
          ok: true,
          config: {
            tool_offset_x: -0.2,
            tool_offset_y: 0.1,
            tool_offset_z: -0.1,
            // Controller may still report probe_center; tool-only must ignore for rewrite.
            probe_center_x: 100.2,
            probe_center_y: 119.9,
            probe_center_z: 40.1,
          },
        };
      },
    };

    const result = await syncCalibrationFromController(client, { persistProbeCenter: false });
    assert.strictEqual(result.ok, true);
    assert.strictEqual(result.persistProbeCenter, false);

    const calib = calibrationStore.load();
    const cfg = machineConfigStore.load();
    assert.ok(near(calib.probeCenter.x, 100));
    assert.ok(near(calib.probeCenter.y, 120));
    assert.ok(near(calib.probeCenter.z, 40));
    assert.ok(near(calib.toolOffset.x, -0.2));
    assert.ok(near(calib.toolOffset.y, 0.1));
    assert.ok(near(calib.toolOffset.z, -0.1));
    assert.ok(near(cfg.tool_offset_x, -0.2));
    assert.ok(near(cfg.tool_offset_y, 0.1));
    assert.ok(near(cfg.tool_offset_z, -0.1));

    // Legacy reference must not reconstruct probeCenter.
    const legacyReconX = cfg.probe_offset_reference_x - cfg.tool_offset_x;
    assert.ok(Math.abs(legacyReconX - 100) > 1, 'must not equal reference-tool reconstruction');

    // Transform with OffsetOnly tool.
    const m = toMachinePoint(
      {
        plateOriginValid: true,
        plateOrigin: { x: 98, y: 78, z: 28 },
        toolOffset: calib.toolOffset,
      },
      { x: 10, y: 20, z: 5 }
    );
    assert.ok(m);
    assert.ok(near(m.x, 107.8));
    assert.ok(near(m.y, 98.1));
    assert.ok(near(m.z, 32.9));
  });
}

async function main() {
  await testFullCalibPersistReload();
  await testOffsetOnlyPersistReloadIgnoresLegacyReference();
  console.log('syncCalibrationFromController.test.js: PASS');
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
