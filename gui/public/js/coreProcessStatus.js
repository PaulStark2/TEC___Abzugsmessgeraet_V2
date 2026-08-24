'use strict';

/**
 * Client-side mirror of server/procedure/coreProcessGuard.js (display + button gates).
 * Keep message mapping in sync with the server module.
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
  Completed: 'Abgeschlossen',
  Aborted: 'Abgebrochen',
});

export function isCoreProcessRunning(processState) {
  return processState === 'Running';
}

export function classifyRunningPhase(procedurePhase) {
  const phase = String(procedurePhase || '');
  if (M19_PHASES.has(phase)) return 'probe_calib';
  if (PROCEDURE_PHASES.has(phase)) return 'procedure';
  return 'other';
}

export function formatProcedurePhaseLabel(procedurePhase) {
  const phase = String(procedurePhase || '');
  if (!phase || phase === 'Idle') return '—';
  return PHASE_LABELS_DE[phase] || phase;
}

export function describeRunningCoreProcess(procedurePhase) {
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

export function isProcedureStartButtonDisabled({ procedureState, coreProcessState }) {
  const active = procedureState === 'running' || procedureState === 'paused';
  return active || isCoreProcessRunning(coreProcessState);
}
