'use strict';

/**
 * Persist SimUI plant settings (limits, probe disc position, force stiffness).
 * Separate from MachineConfig — plant-only virtual sensor world (M15).
 */

const fs = require('fs');
const path = require('path');

const FILE_PATH =
  process.env.SIM_PLANT_STORE_PATH || path.join(__dirname, '..', 'data', 'sim-plant.json');

function defaultSimPlantPayload() {
  return {
    limits: {
      xMin: 0,
      xMax: 100,
      yMin: 0,
      yMax: 100,
      zMin: 0,
      zMax: 50,
    },
    probeDisc: {
      x: 40,
      y: 40,
      z: 10,
    },
    forceStiffnessNPerMm: 5,
  };
}

function ensureDir() {
  const dir = path.dirname(FILE_PATH);
  if (!fs.existsSync(dir)) {
    fs.mkdirSync(dir, { recursive: true });
  }
}

function isFiniteNumber(value) {
  return typeof value === 'number' && Number.isFinite(value);
}

function normalizeLimits(limits, fallback) {
  const base = fallback || defaultSimPlantPayload().limits;
  const next = { ...base };
  if (!limits || typeof limits !== 'object') {
    return next;
  }
  for (const key of ['xMin', 'xMax', 'yMin', 'yMax', 'zMin', 'zMax']) {
    if (limits[key] !== undefined) {
      const n = Number(limits[key]);
      if (isFiniteNumber(n)) {
        next[key] = n;
      }
    }
  }
  return next;
}

function normalizeProbeDisc(disc, fallback) {
  const base = fallback || defaultSimPlantPayload().probeDisc;
  const next = { ...base };
  if (!disc || typeof disc !== 'object') {
    return next;
  }
  for (const key of ['x', 'y', 'z']) {
    if (disc[key] !== undefined) {
      const n = Number(disc[key]);
      if (isFiniteNumber(n)) {
        next[key] = n;
      }
    }
  }
  return next;
}

function load() {
  const defaults = defaultSimPlantPayload();
  try {
    const raw = fs.readFileSync(FILE_PATH, 'utf8');
    const parsed = JSON.parse(raw);
    return {
      limits: normalizeLimits(parsed.limits, defaults.limits),
      probeDisc: normalizeProbeDisc(parsed.probeDisc, defaults.probeDisc),
      forceStiffnessNPerMm: isFiniteNumber(Number(parsed.forceStiffnessNPerMm))
        ? Number(parsed.forceStiffnessNPerMm)
        : defaults.forceStiffnessNPerMm,
    };
  } catch {
    return defaults;
  }
}

function save(partial) {
  const current = load();
  const next = {
    limits: normalizeLimits(partial && partial.limits, current.limits),
    probeDisc: normalizeProbeDisc(partial && partial.probeDisc, current.probeDisc),
    forceStiffnessNPerMm:
      partial && isFiniteNumber(Number(partial.forceStiffnessNPerMm))
        ? Number(partial.forceStiffnessNPerMm)
        : current.forceStiffnessNPerMm,
  };
  ensureDir();
  fs.writeFileSync(FILE_PATH, `${JSON.stringify(next, null, 2)}\n`, 'utf8');
  return next;
}

module.exports = {
  FILE_PATH,
  defaultSimPlantPayload,
  load,
  save,
  normalizeLimits,
  normalizeProbeDisc,
};
