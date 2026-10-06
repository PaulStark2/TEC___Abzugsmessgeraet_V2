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
async function pushForceZ(machineClient, calib) {
  if (!calib.forceZ || !calib.forceZ.valid || !calib.forceZ.active || !machineClient) {
    return { ok: true };
  }
  const sourceName = calib.forceZ.source || 'auto';
  const source = sourceName === 'linear' ? 1 : sourceName === 'quadratic' ? 2 : sourceName === 'external' ? 3 : 0;
  if (source === 3 && typeof machineClient.selectForceCurve === 'function') {
    const external = calib.forceZ.external || {};
    const selected = await machineClient.selectForceCurve({
      source: 3,
      confirm: true,
      unit: external.unit === 'volt' ? 1 : 0,
      commit: true,
      beta0: external.beta0,
      beta1: external.beta1,
      beta2: external.beta2 || 0,
    });
    if (!selected || !selected.ok) {
      return { ok: false, error: (selected && selected.error) || 'CmdSelectForceCurve failed' };
    }
  } else if (typeof machineClient.setForceCalibration === 'function' && Array.isArray(calib.forceZ.points) && calib.forceZ.points.length >= 2) {
    const pushed = await machineClient.setForceCalibration({
      source,
      confirm: true,
      commit: true,
      points: calib.forceZ.points.map((point) => ({
        adcNet: point.adcNet,
        massG: point.massG,
        nominalG: point.nominalG,
        direction: point.direction === 'tension' ? 0 : point.direction === 'compression' ? 1 : 2,
      })),
    });
    if (!pushed || !pushed.ok) {
      return { ok: false, error: (pushed && pushed.error) || 'CmdSetForceCalibration failed' };
    }
  }
  return { ok: true };
}

async function pushCommittedCalibration(machineClient, calibOverride) {
  const calib = calibOverride || calibrationStore.load();
  const mcs = calib.machineCoordinateSystem;

  if (!mcs || mcs.valid !== true) {
    const force = await pushForceZ(machineClient, calib);
    if (!force.ok) {
      return { ok: false, error: force.error, calibration: calib };
    }
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

  const home = mcs.homePosition || { x: 0, y: 0, z: 0 };
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

  const force = await pushForceZ(machineClient, calib);
  if (!force.ok) {
    return { ok: false, error: force.error, calibration: calib };
  }

  return { ok: true, restored: true, calibration: calib };
}

module.exports = {
  pushCommittedCalibration,
};
