/**
 * Home marker uses Core measured home (CalibrationSet.home_offset after homing).
 * Never fall back to a later live TCP pose.
 */

const SEEKING_STATES = new Set(['HomingZ', 'HomingX', 'HomingY']);

export function isFinitePose(pose) {
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

export function latchMeasuredHome({
  previousHomingState,
  homingState,
  pose,
  previousMeasuredHome,
}) {
  const kept =
    previousMeasuredHome && isFinitePose(previousMeasuredHome) ? copyPose(previousMeasuredHome) : null;
  if (homingState === 'Homed' && SEEKING_STATES.has(previousHomingState) && isFinitePose(pose)) {
    return copyPose(pose);
  }
  return kept;
}

export function homeMarkerPose({ isHomed, measuredHome }) {
  if (isHomed !== true) {
    return null;
  }
  if (!isFinitePose(measuredHome)) {
    return null;
  }
  return copyPose(measuredHome);
}
