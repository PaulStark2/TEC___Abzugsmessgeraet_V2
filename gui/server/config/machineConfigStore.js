'use strict';

const fs = require('fs');
const path = require('path');
const { defaultMachineConfigPayload } = require('./parameterCatalog');

const FILE_PATH = path.join(__dirname, '..', 'data', 'machine-config.json');

const LEGACY_KEY_RENAMES = [
  ['v_approach', 'v_approach_testpoints'],
  ['v_slow', 'v_fine_testpoints'],
  ['v_measure', 'v_unclamp'],
  ['acceleration', 'a_testpoints'],
  ['probe_search_speed', 'v_search'],
  ['v_approach_probe', 'probe_v_approach'],
];

/** Removed Messtaster / disc-geometry keys (bore-based Einmessen). */
const LEGACY_PROBE_KEYS_DROP = [
  'v_transfer_probe',
  'v_search_fine',
  'a_probe_transfer',
  'corner_probe_transfer',
  'a_probe_search',
  'a_probe_brake',
  'probe_search_distance',
  'probe_backoff',
  'search_depth',
  'num_fine_searches',
  'probe_circle_fit_tol_mm',
  'probe_contact_force_N',
  'head_diameter',
  'measuring_head_diameter',
  'measuring_head_thickness',
  'tool_diameter',
  'tool_cylindrical_height',
  'probe_xy_search_margin',
  'probe_offset_reference_x',
  'probe_offset_reference_y',
  'probe_offset_reference_z',
  'nominal_probe_x',
  'nominal_probe_y',
  'nominal_probe_z',
  'fine_search_speed',
];

function ensureDir() {
  const dir = path.dirname(FILE_PATH);
  if (!fs.existsSync(dir)) {
    fs.mkdirSync(dir, { recursive: true });
  }
}

/**
 * Migrate renamed MachineConfig keys in a parsed JSON object (mutates and returns it).
 */
function migrateLegacyKeys(parsed) {
  if (!parsed || typeof parsed !== 'object') {
    return parsed;
  }
  // Legacy absolute machine-frame safe_z is replaced by safe_z_buffer (plate-relative).
  if (Object.prototype.hasOwnProperty.call(parsed, 'safe_z')) {
    delete parsed.safe_z;
  }
  for (const [oldKey, newKey] of LEGACY_KEY_RENAMES) {
    if (parsed[oldKey] !== undefined) {
      if (parsed[newKey] === undefined) {
        parsed[newKey] = parsed[oldKey];
      }
      delete parsed[oldKey];
    }
  }
  for (const key of LEGACY_PROBE_KEYS_DROP) {
    if (Object.prototype.hasOwnProperty.call(parsed, key)) {
      delete parsed[key];
    }
  }
  return parsed;
}

const LEGACY_COLLISION_KEYS = [
  'collision_approach_N',
  'collision_retract_N',
  'collision_clamp_N',
  'collision_unclamp_N',
  'collision_contact_N',
  'collision_measure_N',
];

/** Files from before the ±30 N range have no force_range_N. Cap old 50 N collision defaults. */
function migrateForceRange(parsed) {
  if (!parsed || typeof parsed !== 'object' || parsed.force_range_N !== undefined) {
    return false;
  }
  parsed.force_range_N = 30;
  parsed.force_hard_limit_N = 30;
  for (const key of LEGACY_COLLISION_KEYS) {
    if (Number(parsed[key]) > 30) {
      parsed[key] = 30;
    }
  }
  return true;
}

/** Files that only stored one limit per phase keep that value on X, Y and Z. */
function migratePerAxisLimits(parsed) {
  if (!parsed || typeof parsed !== 'object') {
    return false;
  }
  const groups = [
    ['force_hard_limit_N', 'force_hard_limit_x_N', 'force_hard_limit_y_N'],
    ['collision_approach_N', 'collision_approach_x_N', 'collision_approach_y_N'],
    ['collision_retract_N', 'collision_retract_x_N', 'collision_retract_y_N'],
    ['collision_clamp_N', 'collision_clamp_x_N', 'collision_clamp_y_N'],
    ['collision_unclamp_N', 'collision_unclamp_x_N', 'collision_unclamp_y_N'],
    ['collision_contact_N', 'collision_contact_x_N', 'collision_contact_y_N'],
    ['collision_measure_N', 'collision_measure_x_N', 'collision_measure_y_N'],
  ];
  let changed = false;
  for (const [zKey, xKey, yKey] of groups) {
    if (parsed[zKey] === undefined) {
      continue;
    }
    if (parsed[xKey] === undefined) {
      parsed[xKey] = parsed[zKey];
      changed = true;
    }
    if (parsed[yKey] === undefined) {
      parsed[yKey] = parsed[zKey];
      changed = true;
    }
  }
  return changed;
}

/**
 * Older configs stored collision_measure_* as absolute Newton (typically ≤ range_N ≤ 100).
 * Measure is now percent of Prüfkraft (> 100). Rewrite legacy absolute values to 150 %.
 */
function migrateMeasurePercent(parsed) {
  if (!parsed || typeof parsed !== 'object') {
    return false;
  }
  const keys = ['collision_measure_N', 'collision_measure_x_N', 'collision_measure_y_N'];
  let changed = false;
  for (const key of keys) {
    const value = Number(parsed[key]);
    if (!Number.isFinite(value)) {
      continue;
    }
    if (value > 0 && value <= 100) {
      parsed[key] = 150;
      changed = true;
    }
  }
  return changed;
}

function load() {
  try {
    const raw = fs.readFileSync(FILE_PATH, 'utf8');
    const parsed = migrateLegacyKeys(JSON.parse(raw));
    const changed =
      migrateForceRange(parsed) || migratePerAxisLimits(parsed) || migrateMeasurePercent(parsed);
    const merged = { ...defaultMachineConfigPayload(), ...parsed };
    if (changed) {
      ensureDir();
      fs.writeFileSync(FILE_PATH, `${JSON.stringify(merged, null, 2)}\n`, 'utf8');
    }
    return merged;
  } catch {
    return defaultMachineConfigPayload();
  }
}

function save(config) {
  ensureDir();
  const merged = { ...defaultMachineConfigPayload(), ...config };
  fs.writeFileSync(FILE_PATH, `${JSON.stringify(merged, null, 2)}\n`, 'utf8');
  return merged;
}

module.exports = {
  FILE_PATH,
  load,
  save,
  migrateLegacyKeys,
};
