'use strict';

/**
 * Helpers for Disk ↔ Core MachineConfig visibility and roundtrip checks.
 */

const CRITICAL_CONFIG_KEYS = Object.freeze([
  'a_testpoints',
  'corner_testpoints',
  'a_homing',
  'phase1_a',
  'phase1_corner',
  'phase2_a',
  'phase2_corner',
  'phase3_a',
  'phase3_corner',
  'v_approach_testpoints',
  'v_fine_testpoints',
  'v_homing',
  'v_unclamp',
]);

const EPS = 1e-4;

function near(a, b) {
  const na = Number(a);
  const nb = Number(b);
  if (!Number.isFinite(na) || !Number.isFinite(nb)) {
    return false;
  }
  return Math.abs(na - nb) <= EPS;
}

/**
 * Compare critical motion fields between two config objects.
 * @returns {{ drift: boolean, mismatchedKeys: string[] }}
 */
function compareCriticalMachineConfig(diskCfg, coreCfg) {
  const mismatchedKeys = [];
  if (!diskCfg || !coreCfg || typeof diskCfg !== 'object' || typeof coreCfg !== 'object') {
    return { drift: true, mismatchedKeys: [...CRITICAL_CONFIG_KEYS] };
  }
  for (const key of CRITICAL_CONFIG_KEYS) {
    if (diskCfg[key] === undefined || coreCfg[key] === undefined) {
      continue;
    }
    if (!near(diskCfg[key], coreCfg[key])) {
      mismatchedKeys.push(key);
    }
  }
  return { drift: mismatchedKeys.length > 0, mismatchedKeys };
}

module.exports = {
  CRITICAL_CONFIG_KEYS,
  compareCriticalMachineConfig,
};
