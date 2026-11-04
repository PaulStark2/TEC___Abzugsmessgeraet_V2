'use strict';

/**
 * Unified procedure progress display semantics (mock + protocol).
 *
 * Controller uses 0-based currentPointIndex = count of completed points while Running.
 * UI shows pointsDone / totalPoints where pointsDone = completed point count.
 *
 * Per-point phase milestones (operator labels) drive the blue progress bar and
 * prev/current/next phase strip.
 */

const POINT_PHASE_SEQUENCE = Object.freeze([
  'PointApproach',
  'PreClampXyCorrect',
  'Clamp',
  'InsertInitial',
  'Unclamp',
  'ForceTare',
  'PreClampXyCorrect',
  'Clamp',
  'TestOrAge',
  'Unclamp',
  'ProbeRemeasure',
]);

const POINT_PHASE_LABELS = Object.freeze([
  'Prüfpunkt anfahren',
  'Bohrung zentrieren',
  'Spreizer aktivieren',
  'Initiales Eingliedern auf Prüfkraft',
  'Spreizer deaktivieren',
  'Zurücksetzen und Ruheprobe',
  'Bohrung zentrieren',
  'Spreizer aktivieren',
  'Prüfen / Altern',
  'Spreizer deaktivieren',
  'Versatz prüfen',
]);

const SETUP_PHASE_SEQUENCE = Object.freeze([
  'ProbeApproach',
  'ProbeCalibrate',
  'MoveSafePose',
]);

const SETUP_PHASE_LABELS = Object.freeze([
  'Einmessbohrung anfahren',
  'Einmessbohrung kalibrieren',
  'Zur sicheren Pose',
]);

const END_PHASE_SEQUENCE = Object.freeze(['MoveSafeEnd', 'Completed']);
const END_PHASE_LABELS = Object.freeze(['Zur Endpose', 'Abgeschlossen']);

const PHASE_LABEL_OVERRIDES = Object.freeze({
  PointApproach: 'Prüfpunkt anfahren',
  PreClampXyCorrect: 'Bohrung zentrieren',
  Clamp: 'Spreizer aktivieren',
  InsertInitial: 'Initiales Eingliedern auf Prüfkraft',
  Unclamp: 'Spreizer deaktivieren',
  ForceTare: 'Zurücksetzen und Ruheprobe',
  TestOrAge: 'Prüfen / Altern',
  ProbeRemeasure: 'Versatz prüfen',
  MoveSafePose: 'Zur sicheren Pose',
  MoveSafeEnd: 'Zur Endpose',
  NextPoint: 'Nächster Punkt',
  ProbeApproach: 'Einmessbohrung anfahren',
  ProbeCalibrate: 'Einmessbohrung kalibrieren',
});

function operatorPhaseLabel(phase, mode) {
  if (!phase) return '—';
  if (phase === 'TestOrAge') {
    return mode === 'aging' ? 'Altern' : 'Prüfen';
  }
  return PHASE_LABEL_OVERRIDES[phase] || phase;
}

/**
 * Resolve which occurrence of a repeated phase we are in by walking Core phase history
 * via a simple heuristic: ForceTare seen => second PreClamp/Clamp/Unclamp half.
 */
function resolvePointMilestoneIndex(procedurePhase, phaseHints = {}) {
  const phase = procedurePhase || '';
  if (!POINT_PHASE_SEQUENCE.includes(phase) && phase !== 'NextPoint') {
    return -1;
  }
  if (phase === 'NextPoint') {
    return POINT_PHASE_SEQUENCE.length - 1;
  }

  const afterSeat = Boolean(phaseHints.afterSeatUnclampDone || phaseHints.forceTareDone);
  const indices = [];
  for (let i = 0; i < POINT_PHASE_SEQUENCE.length; i += 1) {
    if (POINT_PHASE_SEQUENCE[i] === phase) indices.push(i);
  }
  if (indices.length === 0) return -1;
  if (indices.length === 1) return indices[0];

  // Repeated phases: PreClamp, Clamp, Unclamp appear twice.
  if (phase === 'Unclamp') {
    return afterSeat || phaseHints.unclampPass === 2 ? indices[1] : indices[0];
  }
  if (phase === 'PreClampXyCorrect' || phase === 'Clamp') {
    return afterSeat ? indices[1] : indices[0];
  }
  return indices[0];
}

function buildPhaseStrip(labels, index) {
  const n = labels.length;
  if (n === 0) {
    return { phasePrev: '—', phaseCurrent: '—', phaseNext: '—', milestoneIndex: -1, milestoneCount: 0 };
  }
  const i = Math.max(0, Math.min(n - 1, index));
  return {
    phasePrev: i > 0 ? labels[i - 1] : '—',
    phaseCurrent: labels[i],
    phaseNext: i < n - 1 ? labels[i + 1] : '—',
    milestoneIndex: i,
    milestoneCount: n,
  };
}

function derivePhaseProgress({
  procedurePhase,
  mode = 'test',
  phaseHints = {},
  intraFill = 0,
}) {
  const fill = Math.max(0, Math.min(1, Number(intraFill) || 0));
  const phase = procedurePhase || '';

  if (SETUP_PHASE_SEQUENCE.includes(phase)) {
    const idx = SETUP_PHASE_SEQUENCE.indexOf(phase);
    const labels = SETUP_PHASE_LABELS.map((l, i) =>
      i === idx ? operatorPhaseLabel(phase, mode) : l
    );
    const strip = buildPhaseStrip(labels, idx);
    return {
      scope: 'setup',
      ...strip,
      phasePercent: Math.min(99, Math.round(((idx + fill) / SETUP_PHASE_SEQUENCE.length) * 100)),
      milestoneLabels: labels,
    };
  }

  if (END_PHASE_SEQUENCE.includes(phase) || phase === 'Completed') {
    const idx = phase === 'Completed' ? 1 : END_PHASE_SEQUENCE.indexOf(phase);
    const labels = [...END_PHASE_LABELS];
    const strip = buildPhaseStrip(labels, Math.max(0, idx));
    return {
      scope: 'end',
      ...strip,
      phasePercent: phase === 'Completed' ? 100 : Math.min(99, Math.round(((idx + fill) / 2) * 100)),
      milestoneLabels: labels,
    };
  }

  if (phase === 'Idle' || phase === 'Aborted' || !phase) {
    return {
      scope: 'idle',
      phasePrev: '—',
      phaseCurrent: '—',
      phaseNext: '—',
      milestoneIndex: -1,
      milestoneCount: POINT_PHASE_SEQUENCE.length,
      phasePercent: 0,
      milestoneLabels: POINT_PHASE_LABELS.slice(),
    };
  }

  let idx = resolvePointMilestoneIndex(phase, phaseHints);
  if (idx < 0) {
    // Unknown / transitional (e.g. Homing) — show operator label only.
    const label = operatorPhaseLabel(phase, mode);
    return {
      scope: 'other',
      phasePrev: '—',
      phaseCurrent: label,
      phaseNext: '—',
      milestoneIndex: -1,
      milestoneCount: POINT_PHASE_SEQUENCE.length,
      phasePercent: 0,
      milestoneLabels: POINT_PHASE_LABELS.slice(),
    };
  }

  const labels = POINT_PHASE_LABELS.map((l, i) => {
    if (i === idx && POINT_PHASE_SEQUENCE[i] === 'TestOrAge') {
      return operatorPhaseLabel('TestOrAge', mode);
    }
    return l;
  });
  const strip = buildPhaseStrip(labels, idx);
  return {
    scope: 'point',
    ...strip,
    phasePercent: Math.min(
      99,
      Math.round(((idx + fill) / POINT_PHASE_SEQUENCE.length) * 100)
    ),
    milestoneLabels: labels,
  };
}

/**
 * Estimate 0..1 fill within the current phase from live signals.
 */
function estimateIntraPhaseFill({
  procedurePhase,
  machine = {},
  targetForce = 0,
  motionState,
}) {
  const phase = procedurePhase || '';
  const fz = Number(machine.fz);
  const forceTarget = Math.max(0.1, Number(targetForce) || 0);
  const spreader = machine.spreaderState || '';

  if (phase === 'InsertInitial' || phase === 'TestOrAge') {
    if (Number.isFinite(fz) && forceTarget > 0) {
      return Math.max(0, Math.min(0.95, Math.abs(fz) / forceTarget));
    }
    return motionState === 'Moving' ? 0.35 : 0.1;
  }

  if (phase === 'Clamp') {
    if (spreader === 'Extended') return 0.95;
    if (spreader === 'Extending') return 0.45;
    return 0.15;
  }
  if (phase === 'Unclamp') {
    if (spreader === 'Retracted' || spreader === 'SafeDeenergized') return 0.95;
    if (spreader === 'Retracting') return 0.45;
    return 0.15;
  }
  if (phase === 'ForceTare') {
    return machine.tareActive ? 0.6 : motionState === 'Moving' ? 0.3 : 0.2;
  }
  if (
    phase === 'PointApproach' ||
    phase === 'PreClampXyCorrect' ||
    phase === 'ProbeApproach' ||
    phase === 'MoveSafePose' ||
    phase === 'MoveSafeEnd' ||
    phase === 'ProbeRemeasure' ||
    phase === 'ProbeCalibrate'
  ) {
    return motionState === 'Moving' ? 0.45 : 0.15;
  }
  return 0;
}

function derivePointProgress({
  totalPoints,
  index0Based,
  processState,
  procedureState,
  iterationIndex = 0,
  totalIterations = 1,
  pointsPerIteration = null,
  procedurePhase = null,
  mode = 'test',
  phaseHints = {},
  intraFill = 0,
}) {
  const total = Math.max(0, Number(totalPoints) || 0);
  const iterations = Math.max(1, Number(totalIterations) || 1);
  const perIter = pointsPerIteration === null ? total : Math.max(0, Number(pointsPerIteration) || 0);

  const phaseInfo = derivePhaseProgress({
    procedurePhase,
    mode,
    phaseHints,
    intraFill,
  });

  if (total <= 0 && perIter <= 0) {
    return {
      pointsDone: 0,
      totalPoints: 0,
      percent: 0,
      isComplete: false,
      currentPointIndex: 0,
      ...phaseInfo,
    };
  }

  const complete =
    processState === 'Completed' || procedureState === 'completed';

  if (complete) {
    const endStrip = buildPhaseStrip(END_PHASE_LABELS, 1);
    return {
      pointsDone: total,
      totalPoints: total,
      percent: 100,
      isComplete: true,
      currentPointIndex: index0Based ?? total,
      scope: 'end',
      ...endStrip,
      phasePercent: 100,
      milestoneLabels: END_PHASE_LABELS.slice(),
    };
  }

  const idx = Number.isFinite(Number(index0Based)) ? Math.max(0, Number(index0Based)) : 0;
  const pointsDone = Math.min(idx, total);

  // Overall green percent: setup as segment 0, then points with in-point phase share.
  let percent;
  const setupWeight = 0.08;
  const endWeight = 0.04;
  const pointsWeight = 1 - setupWeight - endWeight;

  if (phaseInfo.scope === 'setup') {
    percent = Math.min(
      99,
      Math.round(setupWeight * (phaseInfo.phasePercent / 100) * 100)
    );
  } else if (phaseInfo.scope === 'end') {
    percent = Math.min(
      99,
      Math.round((setupWeight + pointsWeight + endWeight * (phaseInfo.phasePercent / 100)) * 100)
    );
  } else if (total > 0) {
    const pointShare = pointsWeight / total;
    const within =
      phaseInfo.scope === 'point'
        ? (phaseInfo.milestoneIndex + Math.max(0, Math.min(1, Number(intraFill) || 0))) /
          Math.max(1, phaseInfo.milestoneCount)
        : 0;
    const base = setupWeight + pointShare * pointsDone + pointShare * within;
    percent = Math.min(99, Math.round(base * 100));
  } else if (iterations > 1 && perIter > 0) {
    const doneSteps = iterationIndex * perIter + pointsDone;
    const totalSteps = Math.max(1, iterations * perIter);
    percent = Math.min(99, Math.round((doneSteps / totalSteps) * 100));
  } else {
    percent = 0;
  }

  return {
    pointsDone,
    totalPoints: total,
    percent,
    isComplete: false,
    currentPointIndex: idx,
    ...phaseInfo,
  };
}

function trackPhaseHints(prevHints, procedurePhase) {
  const next = { ...(prevHints || {}) };
  const phase = procedurePhase || '';
  if (phase === 'ForceTare') {
    next.forceTareDone = true;
    next.afterSeatUnclampDone = true;
  }
  if (phase === 'Unclamp' && !next.forceTareDone) {
    next.unclampPass = 1;
  }
  if (phase === 'Unclamp' && next.forceTareDone) {
    next.unclampPass = 2;
    next.afterSeatUnclampDone = true;
  }
  if (phase === 'PointApproach' || phase === 'NextPoint') {
    // New point: clear seat/tare flags.
    next.forceTareDone = false;
    next.afterSeatUnclampDone = false;
    next.unclampPass = 0;
  }
  return next;
}

module.exports = {
  derivePointProgress,
  derivePhaseProgress,
  estimateIntraPhaseFill,
  operatorPhaseLabel,
  trackPhaseHints,
  POINT_PHASE_SEQUENCE,
  POINT_PHASE_LABELS,
  SETUP_PHASE_SEQUENCE,
  SETUP_PHASE_LABELS,
};
