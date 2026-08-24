/**
 * Physical tool/probe sizes are MachineConfig-only.
 * SimPlant may own disc center (x/y/z); never a second persistent geometry config.
 */

function finiteNumber(value) {
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
}

export function physicalGeometryFromMachineConfig(config) {
  if (!config || typeof config !== 'object') {
    return null;
  }
  const diameter = finiteNumber(config.measuring_head_diameter);
  const thickness = finiteNumber(config.measuring_head_thickness);
  const toolDiameter = finiteNumber(config.tool_diameter);
  const toolCylindricalHeight = finiteNumber(config.tool_cylindrical_height);
  if (diameter === null || thickness === null || toolDiameter === null || toolCylindricalHeight === null) {
    return null;
  }
  return { diameter, thickness, toolDiameter, toolCylindricalHeight };
}

export function plantProbeDiscCenterOnly(body) {
  const out = {};
  if (!body || typeof body !== 'object') {
    return out;
  }
  for (const key of ['x', 'y', 'z']) {
    if (body[key] === undefined) {
      continue;
    }
    const n = finiteNumber(body[key]);
    if (n !== null) {
      out[key] = n;
    }
  }
  return out;
}

export function probeVisualFromSources({ plantProbe, machineConfig }) {
  const geo = physicalGeometryFromMachineConfig(machineConfig);
  const center = plantProbe && typeof plantProbe === 'object' ? plantProbe : {};
  return {
    x: finiteNumber(center.x),
    y: finiteNumber(center.y),
    z: finiteNumber(center.z),
    diameter: geo ? geo.diameter : finiteNumber(center.diameter),
    thickness: geo ? geo.thickness : finiteNumber(center.thickness),
    toolDiameter: geo ? geo.toolDiameter : finiteNumber(center.toolDiameter),
    toolCylindricalHeight: geo
      ? geo.toolCylindricalHeight
      : finiteNumber(center.toolCylindricalHeight),
  };
}
