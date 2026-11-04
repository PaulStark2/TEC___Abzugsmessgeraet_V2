'use strict';

/**
 * Plate-frame → machine-frame (mirrors core/CoordinateTransform.hpp).
 * machine.xy = plateOrigin.xy + Rz(plateYaw) * platePoint.xy + toolOffset.xy
 * machine.z  = plateOrigin.z + platePoint.z + toolOffset.z
 */

function toMachinePoint(calibration, platePoint) {
  if (!calibration || !calibration.plateOriginValid || !calibration.plateOrigin) {
    return null;
  }
  const o = calibration.plateOrigin;
  const t = calibration.toolOffset || { x: 0, y: 0, z: 0 };
  const yaw = Number(calibration.plateYawRad) || 0;
  const px = Number(platePoint.x);
  const py = Number(platePoint.y);
  const pz = Number(platePoint.z);
  if (![o.x, o.y, o.z, t.x, t.y, t.z, px, py, pz, yaw].every(Number.isFinite)) {
    return null;
  }
  const c = Math.cos(yaw);
  const s = Math.sin(yaw);
  const rx = c * px - s * py;
  const ry = s * px + c * py;
  return {
    x: o.x + rx + t.x,
    y: o.y + ry + t.y,
    z: o.z + pz + t.z,
  };
}

function toMachinePoints(calibration, platePoints) {
  if (!Array.isArray(platePoints)) {
    return null;
  }
  const out = [];
  for (const p of platePoints) {
    const m = toMachinePoint(calibration, p);
    if (!m) {
      return null;
    }
    out.push({
      ...p,
      x: m.x,
      y: m.y,
      z: m.z,
      frame: 'machine',
      plateX: p.x,
      plateY: p.y,
      plateZ: p.z,
    });
  }
  return out;
}

/**
 * Machine-frame TCP → plate-frame (inverse of toMachinePoint).
 */
function toPlatePoint(calibration, machinePoint) {
  if (!calibration || !calibration.plateOriginValid || !calibration.plateOrigin) {
    return null;
  }
  if (!machinePoint || typeof machinePoint !== 'object') {
    return null;
  }
  const o = calibration.plateOrigin;
  const t = calibration.toolOffset || { x: 0, y: 0, z: 0 };
  const yaw = Number(calibration.plateYawRad) || 0;
  const mx = Number(machinePoint.x);
  const my = Number(machinePoint.y);
  const mz = Number(machinePoint.z);
  if (![o.x, o.y, o.z, t.x, t.y, t.z, mx, my, mz, yaw].every(Number.isFinite)) {
    return null;
  }
  const dx = mx - o.x - t.x;
  const dy = my - o.y - t.y;
  const c = Math.cos(yaw);
  const s = Math.sin(yaw);
  return {
    x: c * dx + s * dy,
    y: -s * dx + c * dy,
    z: mz - o.z - t.z,
  };
}

module.exports = {
  toMachinePoint,
  toMachinePoints,
  toPlatePoint,
};
