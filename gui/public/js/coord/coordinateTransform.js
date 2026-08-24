/**
 * Plate-frame → machine-frame (mirrors core/CoordinateTransform.hpp).
 * machine = plateOrigin + platePoint + toolOffset
 */

export function toMachinePoint(calibration, platePoint) {
  if (!calibration || !calibration.plateOriginValid || !calibration.plateOrigin) {
    return null;
  }
  const o = calibration.plateOrigin;
  const t = calibration.toolOffset || { x: 0, y: 0, z: 0 };
  const px = Number(platePoint.x);
  const py = Number(platePoint.y);
  const pz = Number(platePoint.z);
  if (![o.x, o.y, o.z, t.x, t.y, t.z, px, py, pz].every(Number.isFinite)) {
    return null;
  }
  return {
    x: o.x + px + t.x,
    y: o.y + py + t.y,
    z: o.z + pz + t.z,
  };
}

export function toMachinePoints(calibration, platePoints) {
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
