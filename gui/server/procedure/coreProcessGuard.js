'use strict';

/**
 * GUI/adapter helpers: block procedure start while any Core process is Running.
 * Does not stop/abort running processes.
 */

const M19_PHASES = new Set(['ProbeCalibrate', 'WaitPlateCalibration']);

const PROCEDURE_PHASES = new Set([
  'Homing',
  'MoveSafePose',
  'ProbeApproach',
  'PointApproach',
  'PreClampXyCorrect',
  'Clamp',
  'InsertInitial',
  'OptimizeInsert',
  'TestOrAge',
  'Unclamp',
  'NextPoint',
  'MoveSafeEnd',
  'ForceTare',
]);

const PHASE_LABELS_DE = Object.freeze({
  Idle: 'Bereit',
  Homing: 'Homing',
  MoveSafePose: 'Sichere Pose anfahren',
  ProbeApproach: 'Messtaster anfahren',
  ProbeCalibrate: 'Messtaster kalibrieren',
  WaitPlateCalibration: 'Warte auf Bauplatten-Kalibrierung',
  PointApproach: 'Prüfpunkt anfahren',
  PreClampXyCorrect: 'Vor dem Klemmen (XY)',
  Clamp: 'Klemmen',
  InsertInitial: 'Einführen',
  OptimizeInsert: 'Einführen optimieren',
  TestOrAge: 'Prüfen / Altern',
  Unclamp: 'Öffnen',
  NextPoint: 'Nächster Punkt',
  MoveSafeEnd: 'Sichere Endpose',
  ForceTare: 'Ruheprobe',
  Completed: 'Abgeschlossen',
  Aborted: 'Abgebrochen',
});

/**
 * @param {unknown} processState
 * @returns {boolean}
 */
function isCoreProcessRunning(processState) {
  return processState === 'Running';
}

/**
 * @param {unknown} procedurePhase
 * @returns {'probe_calib' | 'procedure' | 'other'}
 */
function classifyRunningPhase(procedurePhase) {
  const phase = String(procedurePhase || '');
  if (M19_PHASES.has(phase)) return 'probe_calib';
  if (PROCEDURE_PHASES.has(phase)) return 'procedure';
  return 'other';
}

/**
 * @param {unknown} procedurePhase
 * @returns {string}
 */
function formatProcedurePhaseLabel(procedurePhase) {
  const phase = String(procedurePhase || '');
  if (!phase || phase === 'Idle') return '—';
  return PHASE_LABELS_DE[phase] || phase;
}

/**
 * Operator-facing summary while Core processState === Running.
 * @param {unknown} procedurePhase
 * @returns {{
 *   kind: 'probe_calib' | 'procedure' | 'other',
 *   processLabel: string,
 *   phaseLabel: string,
 *   startBlockedMessage: string,
 * }}
 */
function describeRunningCoreProcess(procedurePhase) {
  const kind = classifyRunningPhase(procedurePhase);
  const phaseLabel = formatProcedurePhaseLabel(procedurePhase);

  if (kind === 'probe_calib') {
    return {
      kind,
      processLabel: 'Kalibrierung läuft',
      phaseLabel,
      startBlockedMessage:
        'Die Messtaster-Kalibrierung läuft noch. Die Prüfprozedur kann erst gestartet werden, wenn die Kalibrierung abgeschlossen ist.',
    };
  }

  if (kind === 'procedure') {
    return {
      kind,
      processLabel: 'Prüfprozedur läuft',
      phaseLabel,
      startBlockedMessage: 'Die Prüfprozedur läuft bereits.',
    };
  }

  return {
    kind,
    processLabel: 'Maschinenprozess läuft',
    phaseLabel: phaseLabel === '—' ? String(procedurePhase || '—') : phaseLabel,
    startBlockedMessage:
      'Ein anderer Maschinenprozess läuft noch. Die Prüfprozedur kann erst danach gestartet werden.',
  };
}

/**
 * Preflight before the CmdSetMachineConfig…CmdStart chain.
 * @param {{ processState?: unknown, procedurePhase?: unknown } | null | undefined} machineStatus
 * @returns {{ ok: true } | { ok: false, code: string, error: string, kind: string, procedurePhase: string|null }}
 */
function validateCoreNotRunningForProcedureStart(machineStatus) {
  const status = machineStatus || {};
  if (!isCoreProcessRunning(status.processState)) {
    return { ok: true };
  }

  const described = describeRunningCoreProcess(status.procedurePhase);
  return {
    ok: false,
    code: 'core_process_running',
    error: described.startBlockedMessage,
    kind: described.kind,
    procedurePhase: status.procedurePhase == null ? null : String(status.procedurePhase),
  };
}

/**
 * Whether the procedure Start button should be disabled (UI only).
 * Uses Core processState, not only the GUI procedure store.
 * @param {{ procedureState?: unknown, coreProcessState?: unknown }} input
 */
function isProcedureStartButtonDisabled(input) {
  const procedureState = input && input.procedureState;
  const active = procedureState === 'running' || procedureState === 'paused';
  return active || isCoreProcessRunning(input && input.coreProcessState);
}

module.exports = {
  M19_PHASES,
  PROCEDURE_PHASES,
  PHASE_LABELS_DE,
  isCoreProcessRunning,
  classifyRunningPhase,
  formatProcedurePhaseLabel,
  describeRunningCoreProcess,
  validateCoreNotRunningForProcedureStart,
  isProcedureStartButtonDisabled,
};
