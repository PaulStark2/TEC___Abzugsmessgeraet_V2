/**
 * Dynamic Safe-Z (mirrors core/SafeZ.hpp / server/coord/safeZ.js).
 * max_clearance_z = max(max(point.z), probe_z_plate)
 * safe_z_plate = max_clearance_z + safe_z_buffer
 * probe_z_plate = probe_center.z - plate_origin.z - tool_offset.z
 * safe_z_machine = plate_origin.z + safe_z_plate + tool_offset.z
 * Probe is included only when probeCenterValid.
 */

function finiteNumber(value) {
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
}

export function computeSafeZPlate(points, safeZBuffer) {
  const list = Array.isArray(points) ? points : [];
  const buffer = finiteNumber(safeZBuffer);
  if (buffer === null || !(buffer > 0)) {
    return null;
  }
  if (list.length === 0) {
    return null;
  }
  let maxPointZ = null;
  for (const p of list) {
    const z = finiteNumber(p && p.z);
    if (z === null) {
      return null;
    }
    if (maxPointZ === null || z > maxPointZ) {
      maxPointZ = z;
    }
  }
  return {
    maxPointZ,
    safeZBuffer: buffer,
    safeZPlate: maxPointZ + buffer,
  };
}

export function resolveProgramSafeZ(points, safeZBuffer, calibration) {
  const plate = computeSafeZPlate(points, safeZBuffer);
  if (!plate) {
    return null;
  }
  if (!calibration || !calibration.plateOriginValid || !calibration.plateOrigin) {
    return null;
  }
  const oz = finiteNumber(calibration.plateOrigin.z);
  const tz = finiteNumber(calibration.toolOffset && calibration.toolOffset.z) ?? 0;
  if (oz === null) {
    return null;
  }
  let maxClearanceZ = plate.maxPointZ;
  let probeZPlate = null;
  if (calibration.probeCenterValid && calibration.probeCenter) {
    const pz = finiteNumber(calibration.probeCenter.z);
    if (pz === null) {
      return null;
    }
    probeZPlate = pz - oz - tz;
    if (probeZPlate > maxClearanceZ) {
      maxClearanceZ = probeZPlate;
    }
  }
  const safeZPlate = maxClearanceZ + plate.safeZBuffer;
  return {
    ...plate,
    probeZPlate,
    maxClearanceZ,
    safeZPlate,
    safeZMachine: oz + safeZPlate + tz,
  };
}
