'use strict';

/**
 * Preflight for automatic procedure start.
 * Mirrors Core guards; Safe-Z is dynamic from the program (plate frame)
 * and a valid Messtaster when it sits higher than the test points.
 */

const { resolveProgramSafeZ } = require('../coord/safeZ');
const { toMachinePoints } = require('../coord/coordinateTransform');
const { buildProcedurePlannedPath, findWorkspaceViolation } = require('../coord/safeZPath');

function resolveStartPose(input, calib) {
  const given = input.workspace && input.workspace.startPose;
  if (given && [given.x, given.y, given.z].every(Number.isFinite)) {
    return { x: given.x, y: given.y, z: given.z };
  }
  const home = calib.machineCoordinateSystem && calib.machineCoordinateSystem.homePosition;
  if (home && [home.x, home.y, home.z].every(Number.isFinite)) {
    return { x: home.x, y: home.y, z: home.z };
  }
  return null;
}

/**
 * @param {{
 *   calibration: object,
 *   machineConfig: object,
 *   points: Array<{sequenceNumber?: number, name?: string, x?: number, y?: number, z?: number}>,
 *   workspace?: {
 *     limits?: { xMin?: number, xMax?: number, yMin?: number, yMax?: number, zMin?: number, zMax?: number },
 *     referenceOffset?: { x?: number, y?: number, z?: number },
 *     startPose?: { x?: number, y?: number, z?: number },
 *   },
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

  const limits = input.workspace && input.workspace.limits;
  if (limits) {
    const machinePoints = toMachinePoints(calib, points);
    if (!machinePoints) {
      return {
        ok: false,
        code: 'transform',
        error: 'Prüfpunkte konnten nicht ins Maschinenkoordinatensystem transformiert werden.',
      };
    }
    const startPose = resolveStartPose(input, calib);
    if (!startPose) {
      return {
        ok: false,
        code: 'workspace',
        error: 'Startpose für die Bahnprüfung fehlt.',
      };
    }
    const planned = buildProcedurePlannedPath(
      startPose,
      machinePoints,
      safeZ.safeZMachine,
      calib.probeCenter,
      {
        approachStandoffMm: cfg.approach_distance,
        includeProbe: true,
      }
    );
    const check = findWorkspaceViolation(
      planned.vertices,
      limits,
      input.workspace.referenceOffset
    );
    if (!check.ok) {
      return check;
    }
  }

  return { ok: true, safeZ };
}

module.exports = {
  validateProcedureStartPreconditions,
};
