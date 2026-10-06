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
  ['fine_search_speed', 'v_search_fine'],
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
  // Migration: nominal_probe_* -> probe_offset_reference_*
  if (parsed.nominal_probe_x !== undefined) {
    parsed.probe_offset_reference_x = parsed.probe_offset_reference_x ?? parsed.nominal_probe_x;
    delete parsed.nominal_probe_x;
  }
  if (parsed.nominal_probe_y !== undefined) {
    parsed.probe_offset_reference_y = parsed.probe_offset_reference_y ?? parsed.nominal_probe_y;
    delete parsed.nominal_probe_y;
  }
  if (parsed.nominal_probe_z !== undefined) {
    parsed.probe_offset_reference_z = parsed.probe_offset_reference_z ?? parsed.nominal_probe_z;
    delete parsed.nominal_probe_z;
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

function load() {
  try {
    const raw = fs.readFileSync(FILE_PATH, 'utf8');
    const parsed = migrateLegacyKeys(JSON.parse(raw));
    const changed = migrateForceRange(parsed);
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
