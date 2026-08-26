'use strict';

/**
 * Preflight for automatic procedure start.
 * Mirrors Core guards; Safe-Z is dynamic from the program (plate frame).
 */

const { resolveProgramSafeZ } = require('../coord/safeZ');

/**
 * @param {{
 *   calibration: object,
 *   machineConfig: object,
 *   points: Array<{sequenceNumber?: number, name?: string, z?: number}>,
 * }} input
 * @returns {{ ok: true, safeZ?: object } | { ok: false, error: string, code: string, detail?: object }}
 */
function validateProcedureStartPreconditions(input) {
  const calib = input.calibration || {};
  const cfg = input.machineConfig || {};
  const points = Array.isArray(input.points) ? input.points : [];

  const mcs = calib.machineCoordinateSystem;
  if (!mcs || mcs.valid !== true || !mcs.homePosition) {
    return {
      ok: false,
      code: 'mcs',
      error:
        'Kein gültiges Maschinenkoordinatensystem — bitte unter Kalibrieren „MCS und Bauplatte/Messtaster definieren und kalibrieren“ durchführen',
    };
  }

  if (!calib.plateOriginValid || !calib.plateOrigin) {
    return {
      ok: false,
      code: 'plate_origin',
      error: 'Bauplatten-Ursprung nicht gesetzt — bitte unter Kalibrieren einmessen',
    };
  }

  if (!calib.probeCenterValid || !calib.probeCenter) {
    return {
      ok: false,
      code: 'probe_center',
      error: 'Messtaster-Zentrum nicht gesetzt — bitte M19-Kalibrierung durchführen',
    };
  }

  if (points.length === 0) {
    return {
      ok: false,
      code: 'program_empty',
      error: 'Kein Prüfprogramm geladen. Die Safe-Z-Höhe kann nicht berechnet werden.',
    };
  }

  const safeZ = resolveProgramSafeZ(points, cfg.safe_z_buffer, calib);
  if (!safeZ.ok) {
    return {
      ok: false,
      code: safeZ.code || 'safe_z',
      error: safeZ.error || 'Safe-Z kann nicht berechnet werden',
    };
  }

  return { ok: true, safeZ };
}

module.exports = {
  validateProcedureStartPreconditions,
};
