/**
 * Dynamic Safe-Z (mirrors core/SafeZ.hpp / server/coord/safeZ.js).
 * safe_z_plate = max(point.z) + safe_z_buffer
 * safe_z_machine = plate_origin.z + safe_z_plate + tool_offset.z
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
  return {
    ...plate,
    safeZMachine: oz + plate.safeZPlate + tz,
  };
}
