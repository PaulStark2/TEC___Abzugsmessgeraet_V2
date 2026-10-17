'use strict';

/**
 * MockMachineClient — sole MachineClient implementation (G2/G3/G7).
 * Owns machine/motion state and pose; browser only displays snapshots.
 *
 * G3: command-driven moveTo / pause / resume / stop (M5-aligned semantics).
 * G7: setSpeed, jog, pneumaticOpen/Close, home stub.
 */

const DEFAULT_SPEED_UNITS_PER_SEC = 15;
const MIN_SPEED = 1;
const MAX_SPEED = 50;
const DEFAULT_JOG_STEP = 1;

function dist(a, b) {
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const dz = b.z - a.z;
  return Math.sqrt(dx * dx + dy * dy + dz * dz);
}

function createMockMachineClient() {
  let pose = { x: 0, y: 0, z: 0 };
  let target = { x: 0, y: 0, z: 0 };
  let machineState = 'Idle';
  let motionState = 'Idle';
  let manualSpeedUnitsPerSec = DEFAULT_SPEED_UNITS_PER_SEC;
  let pneumaticExtended = false;
  /** @type {'Active' | 'Homing'} */
  let motionMachineState = 'Active';

  function getStatus() {
    return {
      machineState,
      motionState,
      x: pose.x,
      y: pose.y,
      z: pose.z,
      manualSpeed: manualSpeedUnitsPerSec,
      pneumaticExtended,
      homingInProgress: machineState === 'Homing',
    };
  }

  function beginMotion(nextTarget, activeMachineState) {
    target = { ...nextTarget };
    motionMachineState = activeMachineState;

    if (dist(pose, target) <= 1e-3) {
      pose = { ...target };
      machineState = 'Idle';
      motionState = 'AtTarget';
      return { ok: true, status: getStatus() };
    }

    machineState = activeMachineState;
    motionState = 'Moving';
    return { ok: true, status: getStatus() };
  }

  function moveTo(x, y, z) {
    const nx = Number(x);
    const ny = Number(y);
    const nz = Number(z);
    if (![nx, ny, nz].every(Number.isFinite)) {
      return { ok: false, error: 'moveTo requires finite x, y, z' };
    }

    return beginMotion({ x: nx, y: ny, z: nz }, 'Active');
  }

  function setSpeed(speed) {
    const value = Number(speed);
    if (!Number.isFinite(value)) {
      return { ok: false, error: 'setSpeed requires finite speed' };
    }
    manualSpeedUnitsPerSec = Math.min(MAX_SPEED, Math.max(MIN_SPEED, value));
    return { ok: true, status: getStatus() };
  }

  function jog(axis, direction, step) {
    const axisKey = String(axis || '').toUpperCase();
    if (!['X', 'Y', 'Z'].includes(axisKey)) {
      return { ok: false, error: 'jog axis must be X, Y, or Z' };
    }

    const dir = Number(direction);
    if (dir !== 1 && dir !== -1) {
      return { ok: false, error: 'jog direction must be 1 or -1' };
    }

    const stepSize = step === undefined ? DEFAULT_JOG_STEP : Number(step);
    if (!Number.isFinite(stepSize) || stepSize <= 0) {
      return { ok: false, error: 'jog step must be a positive finite number' };
    }

    const delta = dir * stepSize;
    const nextTarget = { ...pose };
    if (axisKey === 'X') nextTarget.x += delta;
    if (axisKey === 'Y') nextTarget.y += delta;
    if (axisKey === 'Z') nextTarget.z += delta;

    return beginMotion(nextTarget, 'Active');
  }

  function pneumaticOpen() {
    pneumaticExtended = true;
    return { ok: true, status: getStatus() };
  }

  function pneumaticClose() {
    pneumaticExtended = false;
    return { ok: true, status: getStatus() };
  }

  function home() {
    return beginMotion({ x: 0, y: 0, z: 0 }, 'Homing');
  }

  function forceTare() {
    return { ok: true, status: getStatus() };
  }

  function defineMcs() {
    return beginMotion({ x: 0, y: 0, z: 0 }, 'Homing');
  }

  function invalidateHoming() {
    return { ok: true, status: getStatus() };
  }

  function restoreMcs() {
    return { ok: true, status: getStatus() };
  }

  function clearFault() {
    return {
      ok: false,
      error: 'Mock: Fault-Reset erfordert Controller (Agent 1)',
      status: getStatus(),
    };
  }

  function pause() {
    if (machineState !== 'Active' && machineState !== 'Homing') {
      return { ok: true, status: getStatus() };
    }
    machineState = 'Paused';
    motionState = 'Idle';
    return { ok: true, status: getStatus() };
  }

  function resume() {
    if (machineState !== 'Paused') {
      return { ok: true, status: getStatus() };
    }

    if (dist(pose, target) <= 1e-3) {
      pose = { ...target };
      machineState = 'Idle';
      motionState = 'AtTarget';
      return { ok: true, status: getStatus() };
    }

    machineState = motionMachineState;
    motionState = 'Moving';
    return { ok: true, status: getStatus() };
  }

  function stop() {
    if (machineState === 'Stopped' || machineState === 'Fault') {
      return { ok: true, status: getStatus() };
    }
    // Soft stop: freeze pose in place. Not a hardware E-Stop.
    machineState = 'Stopped';
    motionState = 'Idle';
    return { ok: true, status: getStatus() };
  }

  function tick(dt) {
    if (!(dt > 0)) {
      return;
    }
    if (machineState !== motionMachineState) {
      return;
    }
    if (motionState !== 'Moving') {
      return;
    }

    const remaining = dist(pose, target);
    const step = manualSpeedUnitsPerSec * dt;

    if (remaining <= 1e-3 || step >= remaining) {
      pose = { ...target };
      machineState = 'Idle';
      motionState = 'AtTarget';
      return;
    }

    const t = step / remaining;
    pose = {
      x: pose.x + (target.x - pose.x) * t,
      y: pose.y + (target.y - pose.y) * t,
      z: pose.z + (target.z - pose.z) * t,
    };
    motionState = 'Moving';
  }

  return {
    getStatus,
    tick,
    moveTo,
    setSpeed,
    jog,
    pneumaticOpen,
    pneumaticClose,
    setCollisionGuard() {
      return { ok: true, status: getStatus() };
    },
    home,
    forceTare,
    defineMcs,
    invalidateHoming,
    restoreMcs,
    clearFault,
    pause,
    resume,
    stop,
  };
}

module.exports = {
  createMockMachineClient,
};
