'use strict';

/**
 * Sim visual sizes for probe disc / spreader are plant-owned (optional defaults).
 * MachineConfig no longer carries measuring_head / tool diameter fields.
 */

const DEFAULT_PHYSICAL_GEOMETRY = Object.freeze({
  diameter: 8,
  thickness: 3,
  toolDiameter: 2.5,
  toolCylindricalHeight: 15,
});

const PHYSICAL_GEOMETRY_KEYS = Object.freeze([
  'diameter',
  'thickness',
  'toolDiameter',
  'toolCylindricalHeight',
]);

const PLANT_GEOMETRY_KEYS = PHYSICAL_GEOMETRY_KEYS;

function finiteNumber(value) {
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
}

/**
 * Resolve plant (or visual) geometry with defaults for missing fields.
 * @param {object|null|undefined} source
 * @returns {{ diameter: number, thickness: number, toolDiameter: number, toolCylindricalHeight: number }}
 */
function resolvePhysicalGeometry(source) {
  const src = source && typeof source === 'object' ? source : {};
  const out = {};
  for (const key of PHYSICAL_GEOMETRY_KEYS) {
    const n = finiteNumber(src[key]);
    out[key] = n !== null ? n : DEFAULT_PHYSICAL_GEOMETRY[key];
  }
  return out;
}

/**
 * MachineConfig no longer owns disc sizes. Always returns null.
 * Kept for call-site compatibility (sim page used to sync readouts from config).
 */
function physicalGeometryFromMachineConfig(_config) {
  return null;
}

/** Position-only patch for SimPlant probe disc (geometry is plant defaults / plant state). */
function plantProbeDiscCenterOnly(body) {
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

function probeVisualFromSources({ plantProbe, machineConfig: _machineConfig }) {
  const center = plantProbe && typeof plantProbe === 'object' ? plantProbe : {};
  const geo = resolvePhysicalGeometry(center);
  return {
    x: finiteNumber(center.x),
    y: finiteNumber(center.y),
    z: finiteNumber(center.z),
    diameter: geo.diameter,
    thickness: geo.thickness,
    toolDiameter: geo.toolDiameter,
    toolCylindricalHeight: geo.toolCylindricalHeight,
  };
}

module.exports = {
  DEFAULT_PHYSICAL_GEOMETRY,
  PHYSICAL_GEOMETRY_KEYS,
  PLANT_GEOMETRY_KEYS,
  resolvePhysicalGeometry,
  physicalGeometryFromMachineConfig,
  plantProbeDiscCenterOnly,
  probeVisualFromSources,
};
