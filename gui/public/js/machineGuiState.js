'use strict';

import { formatLimitWarnings } from './machineWarnings.js';

const HOMING_AXIS_LABELS = Object.freeze({
  HomingZ: 'Z',
  HomingX: 'X',
  HomingY: 'Y',
});

/**
 * Display-only mapping from controller status → operator-facing label.
 * @param {object} status
 * @returns {{ label: string, warnings: string[] }}
 */
export function deriveDisplayState(status) {
  const warnings = formatLimitWarnings(status.limits);

  if (Object.prototype.hasOwnProperty.call(status, 'connected') && status.connected === false) {
    return { label: 'OFFLINE', warnings };
  }

  if (status.machineState === 'Fault' || status.eStop === true) {
    return { label: 'FAULT', warnings };
  }

  if (warnings.length > 0 || status.limitWarning === true) {
    return { label: 'WARNING', warnings };
  }

  if (
    status.machineState === 'Homing' ||
    status.homingState === 'HomingZ' ||
    status.homingState === 'HomingX' ||
    status.homingState === 'HomingY' ||
    status.homingPhase === 'in_progress' ||
    status.homingInProgress === true
  ) {
    return { label: 'HOMING', warnings };
  }

  if (
    status.isHomed === true
  ) {
    return { label: 'HOMED', warnings };
  }

  if (status.machineState === 'Paused') {
    return { label: 'PAUSED', warnings };
  }

  if (status.machineState === 'Active') {
    return { label: 'RUNNING', warnings };
  }

  if (status.machineState === 'Stopped') {
    return { label: 'STOPPED', warnings };
  }

  return { label: 'IDLE', warnings };
}

/**
 * @param {object} status
 * @returns {string}
 */
export function formatHomingStatus(status) {
  if (status.homingState === 'HomingError') {
    return 'Homing-Fehler';
  }

  const axisFromState = HOMING_AXIS_LABELS[status.homingState];
  if (axisFromState) {
    return `Homing läuft – ${axisFromState}-Achse`;
  }

  const axis = status.homingAxis;
  if (axis && typeof axis === 'string') {
    return `Homing läuft – ${axis}-Achse`;
  }

  if (status.homingInProgress === true || status.machineState === 'Homing') {
    return 'Homing läuft…';
  }

  const referenced = status.isHomed === true;

  if (!referenced) {
    return 'Nicht referenziert';
  }
  return 'Homed';
}
