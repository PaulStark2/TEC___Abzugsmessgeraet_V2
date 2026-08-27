'use strict';

/**
 * Dynamic Safe-Z (plate frame), mirrors core/SafeZ.hpp:
 *   max_clearance_z = max(max(point.z), probe_z_plate)
 *   safe_z_plate = max_clearance_z + safe_z_buffer
 *   probe_z_plate = probe_center.z - plate_origin.z - tool_offset.z
 * Machine traverse height:
 *   safe_z_machine = plate_origin.z + safe_z_plate + tool_offset.z
 * Probe is included only when probeCenterValid.
 */

function finiteNumber(value) {
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
}

/**
 * @param {Array<{z?: number}>} points plate-frame program points
 * @returns {{ ok: true, maxPointZ: number } | { ok: false, code: string, error: string }}
 */
function computeMaxPlatePointZ(points) {
  const list = Array.isArray(points) ? points : [];
  if (list.length === 0) {
    return {
      ok: false,
      code: 'program_empty',
      error: 'Kein Prüfprogramm geladen. Die Safe-Z-Höhe kann nicht berechnet werden.',
    };
  }
  let maxZ = null;
  for (const p of list) {
    const z = finiteNumber(p && p.z);
    if (z === null) {
      return {
        ok: false,
        code: 'point_z',
        error: 'Prüfpunkt ohne gültige Z-Koordinate — Safe-Z kann nicht berechnet werden.',
      };
    }
    if (maxZ === null || z > maxZ) {
      maxZ = z;
    }
  }
  return { ok: true, maxPointZ: maxZ };
}

/**
 * Probe center is machine-frame; convert Z into plate frame.
 * @returns {{ ok: true, probeZPlate: number } | { ok: false } | { skipped: true }}
 */
function computeProbeZPlate(calibration) {
  if (!calibration || !calibration.probeCenterValid || !calibration.probeCenter) {
    return { skipped: true };
  }
  if (!calibration.plateOriginValid || !calibration.plateOrigin) {
    return { skipped: true };
  }
  const pz = finiteNumber(calibration.probeCenter.z);
  const oz = finiteNumber(calibration.plateOrigin.z);
  const tz = finiteNumber(calibration.toolOffset && calibration.toolOffset.z) ?? 0;
  if (pz === null || oz === null) {
    return {
      ok: false,
      code: 'probe_center',
      error: 'Messtaster-Zentrum.z ist ungültig — Safe-Z kann nicht berechnet werden.',
    };
  }
  return { ok: true, probeZPlate: pz - oz - tz };
}

/**
 * @param {Array<{z?: number}>} points
 * @param {number} safeZBuffer
 * @returns {{ ok: true, maxPointZ: number, safeZBuffer: number, safeZPlate: number } | { ok: false, code: string, error: string }}
 */
function computeSafeZPlate(points, safeZBuffer) {
  const buffer = finiteNumber(safeZBuffer);
  if (buffer === null || !(buffer > 0)) {
    return {
      ok: false,
      code: 'safe_z_buffer',
      error: 'safe_z_buffer fehlt oder ist ungültig (muss > 0 mm sein)',
    };
  }
  const max = computeMaxPlatePointZ(points);
  if (!max.ok) {
    return max;
  }
  return {
    ok: true,
    maxPointZ: max.maxPointZ,
    safeZBuffer: buffer,
    safeZPlate: max.maxPointZ + buffer,
  };
}

/**
 * @param {{ plateOriginValid?: boolean, plateOrigin?: {z?: number}, toolOffset?: {z?: number} }} calibration
 * @param {number} safeZPlate
 * @returns {{ ok: true, safeZMachine: number } | { ok: false, code: string, error: string }}
 */
function computeSafeZMachine(calibration, safeZPlate) {
  const plate = finiteNumber(safeZPlate);
  if (plate === null) {
    return { ok: false, code: 'safe_z_plate', error: 'Safe-Z (Bauplatte) ist ungültig' };
  }
  if (!calibration || !calibration.plateOriginValid || !calibration.plateOrigin) {
    return {
      ok: false,
      code: 'plate_origin',
      error: 'Bauplatten-Ursprung nicht gesetzt — bitte unter Kalibrieren einmessen',
    };
  }
  const oz = finiteNumber(calibration.plateOrigin.z);
  const tz = finiteNumber(calibration.toolOffset && calibration.toolOffset.z) ?? 0;
  if (oz === null) {
    return {
      ok: false,
      code: 'plate_origin',
      error: 'Bauplatten-Ursprung.z ist ungültig',
    };
  }
  return { ok: true, safeZMachine: oz + plate + tz };
}

/**
 * Full resolve: plate points + optional probe + buffer + calibration → machine Safe-Z.
 */
function resolveProgramSafeZ(points, safeZBuffer, calibration) {
  const plate = computeSafeZPlate(points, safeZBuffer);
  if (!plate.ok) {
    return plate;
  }
  const probe = computeProbeZPlate(calibration);
  if (probe.ok === false) {
    return probe;
  }
  let maxClearanceZ = plate.maxPointZ;
  let probeZPlate = null;
  if (probe.ok === true) {
    probeZPlate = probe.probeZPlate;
    if (probeZPlate > maxClearanceZ) {
      maxClearanceZ = probeZPlate;
    }
  }
  const safeZPlate = maxClearanceZ + plate.safeZBuffer;
  const machine = computeSafeZMachine(calibration, safeZPlate);
  if (!machine.ok) {
    return machine;
  }
  return {
    ok: true,
    maxPointZ: plate.maxPointZ,
    probeZPlate,
    maxClearanceZ,
    safeZBuffer: plate.safeZBuffer,
    safeZPlate,
    safeZMachine: machine.safeZMachine,
  };
}

module.exports = {
  computeMaxPlatePointZ,
  computeProbeZPlate,
  computeSafeZPlate,
  computeSafeZMachine,
  resolveProgramSafeZ,
};
