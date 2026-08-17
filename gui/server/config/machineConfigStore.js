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
