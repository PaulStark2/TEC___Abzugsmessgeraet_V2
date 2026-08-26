'use strict';

const fs = require('fs');
const path = require('path');

const FILE_PATH = path.join(__dirname, '..', 'data', 'calibration.json');

function emptyMcs() {
  return {
    valid: false,
    homePosition: null,
  };
}

function empty() {
  return {
    machineCoordinateSystem: emptyMcs(),
    plateOrigin: null,
    plateOriginValid: false,
    probeCenter: null,
    probeCenterValid: false,
    toolOffset: null,
  };
}

function finitePose(pose) {
  if (!pose || typeof pose !== 'object') {
    return null;
  }
  const x = Number(pose.x);
  const y = Number(pose.y);
  const z = Number(pose.z);
  if (![x, y, z].every(Number.isFinite)) {
    return null;
  }
  return { x, y, z };
}

/** Normalize / migrate raw JSON (legacy files lack machineCoordinateSystem). */
function normalize(parsed) {
  const base = empty();
  if (!parsed || typeof parsed !== 'object') {
    return base;
  }

  const mcsIn = parsed.machineCoordinateSystem;
  let mcs = emptyMcs();
  if (mcsIn && typeof mcsIn === 'object') {
    const home = finitePose(mcsIn.homePosition);
    const valid = mcsIn.valid === true && home !== null;
    mcs = {
      valid,
      homePosition: valid ? home : null,
    };
  }
  // Legacy file without MCS block → mcs_valid = false (never invent home).

  const plate = finitePose(parsed.plateOrigin);
  const probe = finitePose(parsed.probeCenter);
  const tool = finitePose(parsed.toolOffset);

  // Geometry is only valid when MCS is valid and flags/poses are present.
  const plateOriginValid = mcs.valid === true && parsed.plateOriginValid === true && plate !== null;
  const probeCenterValid = mcs.valid === true && parsed.probeCenterValid === true && probe !== null;

  return {
    machineCoordinateSystem: mcs,
    plateOrigin: plate,
    plateOriginValid,
    probeCenter: probe,
    probeCenterValid,
    toolOffset: tool,
  };
}

function load() {
  try {
    const parsed = JSON.parse(fs.readFileSync(FILE_PATH, 'utf8'));
    return normalize(parsed);
  } catch {
    return empty();
  }
}

function save(state) {
  const dir = path.dirname(FILE_PATH);
  if (!fs.existsSync(dir)) {
    fs.mkdirSync(dir, { recursive: true });
  }
  const next = normalize({ ...empty(), ...state });
  fs.writeFileSync(FILE_PATH, `${JSON.stringify(next, null, 2)}\n`, 'utf8');
  return next;
}

/**
 * Commit a newly defined MCS to disk.
 * Replaces the previous committed MCS and discards geometry (old MCS frame).
 */
function setMachineCoordinateSystem(homePosition) {
  const home = finitePose(homePosition);
  if (!home) {
    throw new Error('homePosition x,y,z must be finite');
  }
  return save({
    machineCoordinateSystem: { valid: true, homePosition: home },
    plateOrigin: null,
    plateOriginValid: false,
    probeCenter: null,
    probeCenterValid: false,
    toolOffset: null,
  });
}

function clearGeometry() {
  const current = load();
  return save({
    ...current,
    plateOrigin: null,
    plateOriginValid: false,
    probeCenter: null,
    probeCenterValid: false,
    toolOffset: null,
  });
}

/**
 * @deprecated Do not call on CmdDefineMcs start — that destroyed committed
 * calibration before commit. Kept as a no-op read for any leftover callers.
 * Commit point is setMachineCoordinateSystem only.
 */
function invalidateForDefineMcs() {
  return load();
}

function setPlateOrigin(pose) {
  const current = load();
  return save({
    ...current,
    plateOrigin: { x: pose.x, y: pose.y, z: pose.z },
    plateOriginValid: true,
  });
}

function setProbeCenter(pose, toolOffset) {
  const current = load();
  const next = {
    ...current,
    probeCenter: { x: pose.x, y: pose.y, z: pose.z },
    probeCenterValid: true,
  };
  if (toolOffset) {
    next.toolOffset = { x: toolOffset.x, y: toolOffset.y, z: toolOffset.z };
  }
  return save(next);
}

/**
 * Detect old semantics toolOffset ≈ (40,40,10) - probeCenter with large magnitude.
 * Does not auto-correct; callers should require FullCalib.
 */
function isLegacyToolOffsetSemantics(calib, reference = { x: 40, y: 40, z: 10 }) {
  const state = calib || load();
  if (!state.probeCenter || !state.toolOffset) {
    return false;
  }
  const t = state.toolOffset;
  const p = state.probeCenter;
  const expected = {
    x: reference.x - p.x,
    y: reference.y - p.y,
    z: reference.z - p.z,
  };
  const near = (a, b) => Math.abs(a - b) < 0.5;
  if (!near(t.x, expected.x) || !near(t.y, expected.y) || !near(t.z, expected.z)) {
    return false;
  }
  return Math.hypot(t.x, t.y, t.z) > 5;
}

module.exports = {
  load,
  save,
  normalize,
  setMachineCoordinateSystem,
  clearGeometry,
  invalidateForDefineMcs,
  setPlateOrigin,
  setProbeCenter,
  isLegacyToolOffsetSemantics,
  FILE_PATH,
};
