'use strict';

/**
 * Push the committed calibration.json state into the live Core.
 * Used on connect and on Full-Calibration abort-before-commit rollback.
 */

const calibrationStore = require('./calibrationStore');

/**
 * @param {object} machineClient
 * @param {object} [calibOverride] optional pre-loaded calibration
 * @returns {Promise<{
 *   ok: boolean,
 *   restored?: boolean,
 *   reason?: string,
 *   error?: string,
 *   calibration: object,
 * }>}
 */
async function pushCommittedCalibration(machineClient, calibOverride) {
  const calib = calibOverride || calibrationStore.load();
  const mcs = calib.machineCoordinateSystem;

  if (!mcs || mcs.valid !== true || !mcs.homePosition) {
    return {
      ok: true,
      restored: false,
      reason: 'no_committed_mcs',
      calibration: calib,
    };
  }

  if (!machineClient || typeof machineClient.restoreMcs !== 'function') {
    return {
      ok: false,
      error: 'restoreMcs not supported by client',
      calibration: calib,
    };
  }

  const home = mcs.homePosition;
  const restored = await machineClient.restoreMcs(home.x, home.y, home.z);
  if (!restored || !restored.ok) {
    return {
      ok: false,
      error: (restored && restored.error) || 'CmdRestoreMcs failed',
      calibration: calib,
    };
  }

  if (
    calib.plateOriginValid &&
    calib.plateOrigin &&
    typeof machineClient.setPlateOrigin === 'function'
  ) {
    const plate = await machineClient.setPlateOrigin(
      calib.plateOrigin.x,
      calib.plateOrigin.y,
      calib.plateOrigin.z
    );
    if (!plate || !plate.ok) {
      return {
        ok: false,
        error: (plate && plate.error) || 'CmdSetPlateOrigin failed',
        calibration: calib,
      };
    }
  }

  if (
    calib.probeCenterValid &&
    calib.probeCenter &&
    typeof machineClient.setProbeCenter === 'function'
  ) {
    const probe = await machineClient.setProbeCenter(
      calib.probeCenter.x,
      calib.probeCenter.y,
      calib.probeCenter.z
    );
    if (!probe || !probe.ok) {
      return {
        ok: false,
        error: (probe && probe.error) || 'CmdSetProbeCenter failed',
        calibration: calib,
      };
    }
  }

  return { ok: true, restored: true, calibration: calib };
}

module.exports = {
  pushCommittedCalibration,
};
