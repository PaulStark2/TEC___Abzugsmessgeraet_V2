'use strict';

/**
 * MockMachineClient — sole MachineClient implementation (G2/G3).
 * Owns machine/motion state and pose; browser only displays snapshots.
 *
 * G3: command-driven moveTo / pause / resume / stop (M5-aligned semantics).
 * No autonomous waypoint loop — idle until a command arrives.
 */

const SPEED_UNITS_PER_SEC = 15;

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

  function getStatus() {
    return {
      machineState,
      motionState,
      x: pose.x,
      y: pose.y,
      z: pose.z,
    };
  }

  function moveTo(x, y, z) {
    const nx = Number(x);
    const ny = Number(y);
    const nz = Number(z);
    if (![nx, ny, nz].every(Number.isFinite)) {
      return { ok: false, error: 'moveTo requires finite x, y, z' };
    }

    target = { x: nx, y: ny, z: nz };

    if (dist(pose, target) <= 1e-3) {
      pose = { ...target };
      machineState = 'Idle';
      motionState = 'AtTarget';
      return { ok: true, status: getStatus() };
    }

    machineState = 'Active';
    motionState = 'Moving';
    return { ok: true, status: getStatus() };
  }

  function pause() {
    if (machineState !== 'Active') {
      return { ok: true, status: getStatus() };
    }
    machineState = 'Paused';
    // Pose frozen; motion not advancing while Paused.
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

    machineState = 'Active';
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
    // Only Active advances motion (Paused/Stopped/Idle/Fault do not).
    if (machineState !== 'Active' || motionState !== 'Moving') {
      return;
    }

    const remaining = dist(pose, target);
    const step = SPEED_UNITS_PER_SEC * dt;

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
    machineState = 'Active';
    motionState = 'Moving';
  }

  return {
    getStatus,
    tick,
    moveTo,
    pause,
    resume,
    stop,
  };
}

module.exports = {
  createMockMachineClient,
};
