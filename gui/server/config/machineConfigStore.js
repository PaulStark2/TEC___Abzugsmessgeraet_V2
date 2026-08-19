'use strict';

const fs = require('fs');
const path = require('path');
const { defaultMachineConfigPayload } = require('./parameterCatalog');

const FILE_PATH = path.join(__dirname, '..', 'data', 'machine-config.json');

function ensureDir() {
  const dir = path.dirname(FILE_PATH);
  if (!fs.existsSync(dir)) {
    fs.mkdirSync(dir, { recursive: true });
  }
}

function load() {
  try {
    const raw = fs.readFileSync(FILE_PATH, 'utf8');
    const parsed = JSON.parse(raw);
    // Migration: nominal_probe_* -> probe_offset_reference_*
    // (keeps legacy user configs working after the semantic rename).
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

    return { ...defaultMachineConfigPayload(), ...parsed };
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
};
