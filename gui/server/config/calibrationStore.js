'use strict';

const fs = require('fs');
const path = require('path');

const FILE_PATH = path.join(__dirname, '..', 'data', 'calibration.json');

function empty() {
  return {
    plateOrigin: null,
    plateOriginValid: false,
    probeCenter: null,
    probeCenterValid: false,
  };
}

function load() {
  try {
    const parsed = JSON.parse(fs.readFileSync(FILE_PATH, 'utf8'));
    return { ...empty(), ...parsed };
  } catch {
    return empty();
  }
}

function save(state) {
  const dir = path.dirname(FILE_PATH);
  if (!fs.existsSync(dir)) {
    fs.mkdirSync(dir, { recursive: true });
  }
  const next = { ...empty(), ...state };
  fs.writeFileSync(FILE_PATH, `${JSON.stringify(next, null, 2)}\n`, 'utf8');
  return next;
}

function setPlateOrigin(pose) {
  const current = load();
  return save({
    ...current,
    plateOrigin: { x: pose.x, y: pose.y, z: pose.z },
    plateOriginValid: true,
  });
}

function setProbeCenter(pose) {
  const current = load();
  return save({
    ...current,
    probeCenter: { x: pose.x, y: pose.y, z: pose.z },
    probeCenterValid: true,
  });
}

module.exports = {
  load,
  save,
  setPlateOrigin,
  setProbeCenter,
};
