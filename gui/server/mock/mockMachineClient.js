'use strict';

/**
 * MockMachineClient — sole MachineClient implementation in G2.
 * Owns machine/motion state and pose; browser only displays snapshots.
 *
 * Behaviour: move linearly along a closed waypoint loop.
 * At each waypoint: MotionState AtTarget / MachineState Idle briefly, then continue.
 */

const WAYPOINTS = [
  { x: 0, y: 0, z: 0 },
  { x: 40, y: 10, z: 0 },
  { x: 40, y: 10, z: 50 },
  { x: 10, y: 30, z: 50 },
];

const SPEED_UNITS_PER_SEC = 15;
const HOLD_AT_TARGET_SEC = 0.8;

function dist(a, b) {
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const dz = b.z - a.z;
  return Math.sqrt(dx * dx + dy * dy + dz * dz);
}

function createMockMachineClient() {
  let targetIndex = 1;
  let pose = { ...WAYPOINTS[0] };
  let target = { ...WAYPOINTS[targetIndex] };
  let machineState = 'Active';
  let motionState = 'Moving';
  let holdRemaining = 0;

  function beginHoldAtTarget() {
    pose = { ...target };
    machineState = 'Idle';
    motionState = 'AtTarget';
    holdRemaining = HOLD_AT_TARGET_SEC;
  }

  function beginNextSegment() {
    targetIndex = (targetIndex + 1) % WAYPOINTS.length;
    target = { ...WAYPOINTS[targetIndex] };
    machineState = 'Active';
    motionState = 'Moving';
    holdRemaining = 0;
  }

  function tick(dt) {
    if (!(dt > 0)) {
      return;
    }

    if (holdRemaining > 0) {
      holdRemaining -= dt;
      if (holdRemaining > 0) {
        machineState = 'Idle';
        motionState = 'AtTarget';
        return;
      }
      beginNextSegment();
    }

    if (motionState !== 'Moving') {
      return;
    }

    const remaining = dist(pose, target);
    const step = SPEED_UNITS_PER_SEC * dt;

    if (remaining <= 1e-3 || step >= remaining) {
      beginHoldAtTarget();
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

  function getStatus() {
    return {
      machineState,
      motionState,
      x: pose.x,
      y: pose.y,
      z: pose.z,
    };
  }

  return {
    getStatus,
    tick,
  };
}

module.exports = {
  createMockMachineClient,
};
