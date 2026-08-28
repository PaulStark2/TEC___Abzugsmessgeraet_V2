'use strict';

/**
 * Convert machine-frame overlay poses to the gantry (raw) scene:
 *   raw = machine + referenceOffset,  referenceOffset = raw − machine
 *
 * Prefer Core referenceOffset from the sim getState envelope (display field next
 * to pose, not a plant sensor). The plant−status latch is only a fallback.
 */

const HOMING_SEEKING = new Set(['HomingZ', 'HomingX', 'HomingY']);

function isFinitePose(pose) {
  return Boolean(
    pose &&
      typeof pose.x === 'number' &&
      Number.isFinite(pose.x) &&
      typeof pose.y === 'number' &&
      Number.isFinite(pose.y) &&
      typeof pose.z === 'number' &&
      Number.isFinite(pose.z)
  );
}

function copyPose(pose) {
  return { x: pose.x, y: pose.y, z: pose.z };
}

function zeroOffset() {
  return { x: 0, y: 0, z: 0 };
}

function liveWorkspaceOffset(plantPose, statusPose) {
  if (!isFinitePose(plantPose) || !isFinitePose(statusPose)) {
    return null;
  }
  return {
    x: plantPose.x - statusPose.x,
    y: plantPose.y - statusPose.y,
    z: plantPose.z - statusPose.z,
  };
}

function shouldRefreshWorkspaceOffset({
  motionState,
  processState,
  homingState,
  previousHomingState,
  hasLatchedOffset,
}) {
  if (HOMING_SEEKING.has(homingState)) {
    return true;
  }
  if (homingState === 'Homed' && HOMING_SEEKING.has(previousHomingState)) {
    return true;
  }
  if (!hasLatchedOffset) {
    return true;
  }
  if (processState === 'Running') {
    return false;
  }
  if (motionState === 'Moving') {
    return false;
  }
  return true;
}

function latchWorkspaceReferenceOffset({
  plantPose,
  statusPose,
  previousOffset,
  motionState,
  processState,
  homingState,
  previousHomingState,
}) {
  const live = liveWorkspaceOffset(plantPose, statusPose);
  const hasLatched = isFinitePose(previousOffset);
  if (!live) {
    return hasLatched ? copyPose(previousOffset) : zeroOffset();
  }
  if (
    shouldRefreshWorkspaceOffset({
      motionState,
      processState,
      homingState,
      previousHomingState,
      hasLatchedOffset: hasLatched,
    })
  ) {
    return live;
  }
  return copyPose(previousOffset);
}

function pickWorkspaceReferenceOffset(coreOffset, fallback) {
  if (isFinitePose(coreOffset)) {
    return copyPose(coreOffset);
  }
  if (isFinitePose(fallback)) {
    return copyPose(fallback);
  }
  return zeroOffset();
}

module.exports = {
  isFinitePose,
  liveWorkspaceOffset,
  shouldRefreshWorkspaceOffset,
  latchWorkspaceReferenceOffset,
  pickWorkspaceReferenceOffset,
};
