/**
 * M15/M16 simulation UI — display only; Core is motion authority.
 * Program points are plate-frame; machine overlay only after plate_origin is valid.
 */
import { connectStatus, fetchCalibration, fetchMachineConfig } from '/js/api.js';
import { toMachinePoints } from '/js/coord/coordinateTransform.js';
import { resolveProgramSafeZ } from '/js/coord/safeZ.js';
import { createGantryView } from '/js/sim/gantryView.js';
import { buildProcedurePlannedPath, findWorkspaceViolation } from '/js/sim/safeZPath.js';
import { collectOverlayLabels } from '/js/sim/overlayLabels.js';
import { homeMarkerPose } from '/js/sim/homeMarker.js';
import { latchWorkspaceReferenceOffset, pickWorkspaceReferenceOffset } from '/js/sim/workspaceReferenceOffset.js';
import {
  physicalGeometryFromMachineConfig,
  plantProbeDiscCenterOnly,
  probeVisualFromSources,
} from '/js/sim/physicalGeometry.js';
import {
  fetchPlantState,
  setPlantEStop,
  setPlantForceStiffness,
  setPlantInjectedForce,
  setPlantLimits,
  setPlantProbeDisc,
  triggerPlantProbe,
} from '/js/sim/plantApi.js';

const PLANT_POLL_MS = 200;
const PROGRAM_POLL_MS = 2000;
const PROBE_PULSE_MS = 100;
const INJECTED_FORCE_MAX_N = 200;
const INJECTED_HOLD_REFRESH_MS = 200;

const el = (id) => document.getElementById(id);

const dom = {
  status: el('sim-status'),
  clientMode: el('sim-client-mode'),
  machineState: el('sim-machine-state'),
  motionState: el('sim-motion-state'),
  homingState: el('sim-homing-state'),
  homedState: el('sim-homed-state'),
  warningState: el('sim-warning-state'),
  x: el('sim-pos-x'),
  y: el('sim-pos-y'),
  z: el('sim-pos-z'),
  fx: el('sim-force-fx'),
  fy: el('sim-force-fy'),
  fz: el('sim-force-fz'),
  limits: el('sim-limits'),
  estop: el('sim-estop'),
  procedurePhase: el('sim-procedure-phase'),
  faultReason: el('sim-fault-reason'),
  spreaderState: el('sim-spreader-state'),
  commWatchdog: el('sim-comm-watchdog'),
  forceFresh: el('sim-force-fresh'),
  plantFeedback: el('sim-plant-feedback'),
  probeState: el('sim-probe-state'),
  limitState: el('sim-limit-state'),
  estopPlant: el('sim-estop-plant'),
  forceStiffness: el('sim-force-stiffness'),
  injectedForce: el('sim-injected-force'),
  programFrameNote: el('sim-program-frame-note'),
  safeZNote: el('sim-safe-z-note'),
};

const gantry = createGantryView(el('gantry-canvas'), readGeometryInputs());

/** Plate-frame program points from /api/program (never treat as machine). */
let plateProgramPoints = [];
/** Machine-frame points for 3D overlay, or [] when plate_origin not valid. */
let machineProgramPoints = [];
let calibrationSnapshot = null;
let machineConfigSnapshot = null;
let lastStatus = null;
let lastPlantPose = null;
/** Latched raw−machine offset for overlays; fallback if Core offset missing. */
let latchedWorkspaceOffset = null;
/** Core MCS offset from plant getState (display field, not a sensor). */
let lastCoreReferenceOffset = null;
let previousHomingStateForOffset = null;
let lastPlantLimits = null;
let lastPlannedSegments = [];
let traveledSamples = [];
let lastSampleKey = '';
/** Frozen TCP origin for blue planned path (selection load / first valid status). */
let plannedPathStartPose = null;
let lastProcessStateForTraj = null;
let trajectoryRunId = 0;

function clearTrajectorySession(reason) {
  trajectoryRunId += 1;
  plannedPathStartPose = null;
  traveledSamples = [];
  lastSampleKey = '';
  lastPlannedSegments = [];
  gantry.setPlannedSegments([]);
  gantry.setTraveledSamples([]);
  if (reason) {
    console.info(`[sim] trajectory session cleared (${reason}) runId=${trajectoryRunId}`);
  }
}

/**
 * STOP → clear trails. New Running → new session (no leftover planned/actual).
 */
function syncTrajectorySession(status) {
  const ps = status?.processState || 'Idle';
  if (ps === 'Aborted' && lastProcessStateForTraj !== 'Aborted') {
    clearTrajectorySession('process Aborted/STOP');
  }
  if (ps === 'Running' && lastProcessStateForTraj !== 'Running') {
    clearTrajectorySession('new procedure Running');
    if (
      status &&
      Number.isFinite(status.x) &&
      Number.isFinite(status.y) &&
      Number.isFinite(status.z)
    ) {
      plannedPathStartPose = { x: status.x, y: status.y, z: status.z };
    }
  }
  lastProcessStateForTraj = ps;
}
let estopActive = false;
let limitInputsTouched = false;
let probeInputsTouched = false;
let injectedHold = null;
let injectedHoldTimer = null;

const LIMIT_LABELS = ['X-', 'X+', 'Y-', 'Y+', 'Z-', 'Z+'];

function formatNumber(value) {
  return typeof value === 'number' && Number.isFinite(value) ? value.toFixed(2) : '—';
}

function formatLimits(status) {
  if (typeof status.limits !== 'number') {
    return '—';
  }
  const active = LIMIT_LABELS.filter((_, bit) => (status.limits & (1 << bit)) !== 0);
  return active.length ? active.join(' ') : 'keine';
}

function setHeaderStatus(message, isError) {
  dom.status.textContent = message;
  dom.status.classList.toggle('gui-status--error', Boolean(isError));
}

function setPlantFeedback(message, isError) {
  dom.plantFeedback.textContent = message;
  dom.plantFeedback.classList.toggle('command-feedback--error', Boolean(isError));
}

function setProgramFrameNote(message, isWarning) {
  if (!dom.programFrameNote) {
    return;
  }
  dom.programFrameNote.textContent = message || '';
  dom.programFrameNote.hidden = !message;
  dom.programFrameNote.classList.toggle('sim-program-note--warn', Boolean(isWarning));
}

function updateReferenceMarkers(status) {
  const st = status || lastStatus;
  const showHome = el('toggle-ref-home')?.checked !== false;
  const showPlate = el('toggle-ref-plate')?.checked !== false;
  const showProbe = el('toggle-ref-probe')?.checked !== false;
  const showPoints = el('toggle-ref-points')?.checked !== false;

  if (typeof gantry.setTestPointsVisible === 'function') {
    gantry.setTestPointsVisible(showPoints);
  }

  // Core measured home (latched at Homing* → Homed). Never live TCP pose.
  // Markers are machine-frame; gantry scene is raw — apply reference offset.
  const offset = workspaceReferenceOffset(st);
  const home =
    st?.isHomed === true ? homeMarkerPose({ isHomed: true, measuredHome: st.measuredHome }) : null;

  const plate =
    st?.plateOriginValid === true && calibrationSnapshot?.plateOriginValid
      ? calibrationSnapshot.plateOrigin
      : null;

  // Core-only probe_center — never SimPlant::ProbeDisc.center.
  const probe =
    st?.probeCenterValid === true &&
    calibrationSnapshot?.probeCenterValid &&
    calibrationSnapshot?.probeCenter
      ? calibrationSnapshot.probeCenter
      : null;

  gantry.setReferenceMarkers({
    home: showHome ? toRawPose(home, offset) : null,
    plateOrigin: showPlate ? toRawPose(plate, offset) : null,
    probeCenter: showProbe ? toRawPose(probe, offset) : null,
    showHome,
    showPlate,
    showProbe,
  });
}

function plateOriginReady(calib, status) {
  const fromStatus = status && status.plateOriginValid === true;
  const fromStore = Boolean(calib && calib.plateOriginValid && calib.plateOrigin);
  // Prefer live Core flag; still need stored origin values for the transform.
  if (status && Object.prototype.hasOwnProperty.call(status, 'plateOriginValid')) {
    return fromStatus && fromStore;
  }
  return fromStore;
}

function refreshMachineOverlay(status) {
  const st = status || lastStatus;
  if (!plateProgramPoints.length) {
    machineProgramPoints = [];
    traveledSamples = [];
    lastSampleKey = '';
    lastPlannedSegments = [];
    gantry.setTestPoints([]);
    gantry.setPlannedSegments([]);
    gantry.setTraveledSamples([]);
    updateOverlayLabels();
    setProgramFrameNote('');
    return;
  }

  if (!plateOriginReady(calibrationSnapshot, st)) {
    machineProgramPoints = [];
    lastPlannedSegments = [];
    gantry.setTestPoints([]);
    gantry.setPlannedSegments([]);
    gantry.setTraveledSamples([]);
    updateOverlayLabels();
    setProgramFrameNote(
      'Prüfprogramm geladen (Bauplatten-Koordinaten). Zur Darstellung der Maschinenpositionen ist eine Bauplatten-Kalibrierung erforderlich.',
      true
    );
    return;
  }

  const transformed = toMachinePoints(calibrationSnapshot, plateProgramPoints);
  if (!transformed) {
    machineProgramPoints = [];
    lastPlannedSegments = [];
    gantry.setTestPoints([]);
    gantry.setPlannedSegments([]);
    gantry.setTraveledSamples([]);
    updateOverlayLabels();
    setProgramFrameNote(
      'Prüfprogramm geladen. Bauplatten-Kalibrierung erforderlich — keine Maschinenkoordinaten berechenbar.',
      true
    );
    return;
  }

  machineProgramPoints = transformed;
  gantry.setTestPoints(toRawPoses(machineProgramPoints, workspaceReferenceOffset(st)));
  setProgramFrameNote(
    `Prüfpunkte als Maschinenkoordinaten (plate_origin + Platepunkt + tool_offset), ${machineProgramPoints.length} Punkt(e).`,
    false
  );
  if (st) {
    updateTrajectory(st);
  } else {
    updateOverlayLabels();
  }
}

function readNumberInput(id, fallback) {
  const value = Number(el(id).value);
  return Number.isFinite(value) ? value : fallback;
}

function readGeometryInputs() {
  return {
    sizeX: readNumberInput('input-size-x', 200),
    sizeY: readNumberInput('input-size-y', 150),
    sizeZ: readNumberInput('input-size-z', 120),
    profile: readNumberInput('input-profile', 12),
  };
}

function readLimitInputs() {
  return {
    xMin: readNumberInput('input-limit-x-min', 0),
    xMax: readNumberInput('input-limit-x-max', 100),
    yMin: readNumberInput('input-limit-y-min', 0),
    yMax: readNumberInput('input-limit-y-max', 100),
    zMin: readNumberInput('input-limit-z-min', 0),
    zMax: readNumberInput('input-limit-z-max', 50),
  };
}

function normalizePlantLimits(limits) {
  if (!limits) {
    return null;
  }
  return {
    xMin: limits.xMin,
    xMax: limits.xMax,
    yMin: limits.yMin,
    yMax: limits.yMax,
    zMin: limits.zMin,
    zMax: limits.zMax,
  };
}

function updateOverlayLabels() {
  const showPointLabels = el('toggle-point-labels')?.checked === true;
  const showSegmentLabels =
    el('toggle-trajectory')?.checked !== false && el('toggle-segment-labels')?.checked === true;
  const offset = workspaceReferenceOffset(lastStatus);
  const labels = collectOverlayLabels({
    points: machineProgramPoints,
    segments: lastPlannedSegments,
    machineConfig: machineConfigSnapshot,
    showPointLabels,
    showSegmentLabels,
  });
  gantry.setOverlayLabels(toRawPoses(labels, offset));
}

function workspaceReferenceOffset(status) {
  const st = status || lastStatus;
  const homingState = st && st.homingState ? st.homingState : 'None';
  latchedWorkspaceOffset = latchWorkspaceReferenceOffset({
    plantPose: lastPlantPose,
    statusPose: st,
    previousOffset: latchedWorkspaceOffset,
    motionState: st && st.motionState,
    processState: st && st.processState,
    homingState,
    previousHomingState: previousHomingStateForOffset,
  });
  if (st) {
    previousHomingStateForOffset = homingState;
  }
  return pickWorkspaceReferenceOffset(lastCoreReferenceOffset, latchedWorkspaceOffset);
}

/** Machine-frame pose → raw/plant for the gantry scene (raw = machine + offset). */
function toRawPose(pose, offset) {
  if (!pose || ![pose.x, pose.y, pose.z].every(Number.isFinite)) {
    return pose;
  }
  const ox = offset && Number.isFinite(offset.x) ? offset.x : 0;
  const oy = offset && Number.isFinite(offset.y) ? offset.y : 0;
  const oz = offset && Number.isFinite(offset.z) ? offset.z : 0;
  return { ...pose, x: pose.x + ox, y: pose.y + oy, z: pose.z + oz };
}

function toRawPoses(list, offset) {
  if (!Array.isArray(list)) {
    return list;
  }
  return list.map((pose) => toRawPose(pose, offset));
}

function toRawSegments(segments, offset) {
  if (!Array.isArray(segments)) {
    return segments;
  }
  return segments.map((seg) => ({
    ...seg,
    from: toRawPose(seg.from, offset),
    to: toRawPose(seg.to, offset),
  }));
}

function updateSafeZNote(plannedVertices) {
  const note = dom.safeZNote;
  if (!note) {
    return;
  }
  const resolved = resolveProgramSafeZ(
    plateProgramPoints,
    machineConfigSnapshot?.safe_z_buffer,
    calibrationSnapshot
  );
  if (!resolved) {
    note.hidden = false;
    note.classList.toggle('sim-program-note--warn', true);
    note.textContent =
      plateProgramPoints.length === 0
        ? 'Kein Prüfprogramm geladen. Die Safe-Z-Höhe kann nicht berechnet werden.'
        : 'Safe-Z nicht berechenbar (Programm, Puffer oder Bauplatten-Kalibrierung prüfen).';
    return;
  }
  const probePart =
    resolved.probeZPlate !== null && Number.isFinite(resolved.probeZPlate)
      ? `Messtaster (Bauplatte): ${resolved.probeZPlate.toFixed(2)} mm · `
      : '';
  let text =
    `Höchster Prüfpunkt: ${resolved.maxPointZ.toFixed(2)} mm · ` +
    probePart +
    `Safe-Z-Puffer: ${resolved.safeZBuffer.toFixed(2)} mm · ` +
    `Safe-Z (Bauplatte): ${resolved.safeZPlate.toFixed(2)} mm · ` +
    `Safe-Z (Maschine): ${resolved.safeZMachine.toFixed(2)} mm`;
  const limits = lastPlantLimits || normalizePlantLimits(readLimitInputs());
  const check = findWorkspaceViolation(
    plannedVertices,
    limits,
    workspaceReferenceOffset(lastStatus)
  );
  note.hidden = false;
  if (!check.ok) {
    note.classList.toggle('sim-program-note--warn', true);
    note.textContent = `${check.error} ${text}`;
    return;
  }
  note.classList.toggle('sim-program-note--warn', false);
  note.textContent = text;
}

function updateTrajectory(status) {
  if (!machineProgramPoints.length) {
    lastPlannedSegments = [];
    plannedPathStartPose = null;
    gantry.setPlannedSegments([]);
    gantry.setTraveledSamples([]);
    updateOverlayLabels();
    updateSafeZNote();
    return;
  }
  const resolved = resolveProgramSafeZ(
    plateProgramPoints,
    machineConfigSnapshot?.safe_z_buffer,
    calibrationSnapshot
  );
  const safeZ = resolved ? resolved.safeZMachine : NaN;

  if (
    status &&
    Number.isFinite(status.x) &&
    Number.isFinite(status.y) &&
    Number.isFinite(status.z) &&
    !plannedPathStartPose
  ) {
    plannedPathStartPose = { x: status.x, y: status.y, z: status.z };
  }
  const startPose =
    plannedPathStartPose ||
    (status && Number.isFinite(status.x)
      ? { x: status.x, y: status.y, z: status.z }
      : null);

  const probeCenter =
    status?.probeCenterValid === true &&
    calibrationSnapshot?.probeCenterValid &&
    calibrationSnapshot?.probeCenter
      ? calibrationSnapshot.probeCenter
      : null;

  const standoff = Number(machineConfigSnapshot?.approach_distance);
  const insertSearch = Number(machineConfigSnapshot?.insert_search_distance);
  const insertionBuffer = Number(machineConfigSnapshot?.insertion_buffer);

  let plannedVertices = [];
  if (Number.isFinite(safeZ) && startPose) {
    const planned = buildProcedurePlannedPath(
      startPose,
      machineProgramPoints,
      safeZ,
      probeCenter,
      {
        approachStandoffMm: Number.isFinite(standoff) ? standoff : 15,
        insertSearchDistance: Number.isFinite(insertSearch) ? insertSearch : 10,
        insertionBuffer: Number.isFinite(insertionBuffer) ? insertionBuffer : 1,
        includeProbe: Boolean(probeCenter),
        measuredHome: status?.measuredHome || null,
      }
    );
    lastPlannedSegments = planned.segments;
    plannedVertices = planned.vertices;
    gantry.setPlannedSegments(toRawSegments(planned.segments, workspaceReferenceOffset(status)));
  } else {
    // Do not invent a direct polyline when Safe-Z/start are unknown.
    lastPlannedSegments = [];
    gantry.setPlannedSegments([]);
  }

  // Traveled: real TCP samples from StatusReport (machine frame) during a run.
  if (
    status &&
    status.processState === 'Running' &&
    Number.isFinite(status.x) &&
    Number.isFinite(status.y) &&
    Number.isFinite(status.z)
  ) {
    const key = `${status.x.toFixed(3)},${status.y.toFixed(3)},${status.z.toFixed(3)}`;
    if (key !== lastSampleKey) {
      lastSampleKey = key;
      traveledSamples.push({ x: status.x, y: status.y, z: status.z });
      if (traveledSamples.length > 4000) {
        traveledSamples = traveledSamples.slice(-3000);
      }
    }
  }
  gantry.setTraveledSamples(toRawPoses(traveledSamples, workspaceReferenceOffset(status)));
  updateOverlayLabels();
  updateSafeZNote(plannedVertices);
}

function renderStatus(status) {
  lastStatus = status;
  syncTrajectorySession(status);
  dom.clientMode.textContent = status.machineClientMode || '—';
  dom.machineState.textContent = status.machineState || '—';
  dom.motionState.textContent = status.motionState || '—';
  dom.homingState.textContent = status.homingState || 'None';
  {
    // Core isHomed: true only while still at the last successful home pose.
    dom.homedState.textContent =
      status.isHomed === true ? 'ja (Homed)' : 'nein (nicht referenziert)';
  }
  dom.warningState.textContent = status.limitWarning ? 'Limit aktiv' : '—';
  dom.x.textContent = formatNumber(status.x);
  dom.y.textContent = formatNumber(status.y);
  dom.z.textContent = formatNumber(status.z);
  dom.fx.textContent = formatNumber(status.fx);
  dom.fy.textContent = formatNumber(status.fy);
  dom.fz.textContent = formatNumber(status.fz);
  dom.limits.textContent = formatLimits(status);
  dom.estop.textContent = status.eStop ? 'AKTIV' : 'inaktiv';
  if (dom.procedurePhase) {
    dom.procedurePhase.textContent = status.procedurePhase || '—';
  }
  if (dom.faultReason) {
    dom.faultReason.textContent =
      status.faultReason && status.faultReason !== 'None' ? status.faultReason : '—';
  }
  if (dom.spreaderState) {
    dom.spreaderState.textContent = status.spreaderState || '—';
  }
  if (dom.commWatchdog) {
    dom.commWatchdog.textContent = status.communicationWatchdogEnabled ? 'aktiv' : 'aus';
  }
  if (dom.forceFresh) {
    dom.forceFresh.textContent = status.forceSampleFresh ? 'frisch' : 'alt';
  }

  if (status.connected === false) {
    setHeaderStatus('Offline — Pose eingefroren, keine lokale Bewegung', true);
    return;
  }

  // Gantry follows continuous plant/raw TCP (no jump on machine re-reference).
  // Status x/y/z remain machine-frame coordinates for the numeric readout.
  const visual =
    lastPlantPose &&
    Number.isFinite(lastPlantPose.x) &&
    Number.isFinite(lastPlantPose.y) &&
    Number.isFinite(lastPlantPose.z)
      ? lastPlantPose
      : { x: status.x, y: status.y, z: status.z };
  gantry.setPose(visual);
  refreshMachineOverlay(status);
  updateReferenceMarkers(status);
  setHeaderStatus('Controller verbunden — Pose aus Core-Status', false);
}

function renderPlantState(state) {
  const activeLimits = [];
  if (state.limitXMin) activeLimits.push('X-');
  if (state.limitXMax) activeLimits.push('X+');
  if (state.limitYMin) activeLimits.push('Y-');
  if (state.limitYMax) activeLimits.push('Y+');
  if (state.limitZMin) activeLimits.push('Z-');
  if (state.limitZMax) activeLimits.push('Z+');

  dom.limitState.textContent = activeLimits.length ? `${activeLimits.join(' ')} HIGH` : 'alle LOW';
  dom.probeState.textContent = state.probeTriggered ? 'HIGH' : 'LOW';
  dom.estopPlant.textContent = state.eStop ? 'AKTIV' : 'inaktiv';
  estopActive = Boolean(state.eStop);
  el('btn-estop-toggle').classList.toggle('sim-estop-btn--active', estopActive);
  if (dom.forceStiffness && Number.isFinite(Number(state.forceStiffnessNPerMm))) {
    dom.forceStiffness.textContent = Number(state.forceStiffnessNPerMm).toFixed(2);
    const input = el('input-force-stiffness');
    if (input && document.activeElement !== input) {
      input.value = String(state.forceStiffnessNPerMm);
    }
  }
  if (dom.injectedForce) {
    const fx = Number(state.injectedFx);
    const fy = Number(state.injectedFy);
    const fz = Number(state.injectedFz);
    const fmt = (v) => (Number.isFinite(v) ? v.toFixed(1) : '0.0');
    dom.injectedForce.textContent = `${fmt(fx)} / ${fmt(fy)} / ${fmt(fz)}`;
  }
  if (state.probe) {
    gantry.setProbeDisc(
      probeVisualFromSources({ plantProbe: state.probe, machineConfig: machineConfigSnapshot })
    );
  }
}

function syncLimitInputs(limits) {
  if (limitInputsTouched || !limits) {
    return;
  }
  el('input-limit-x-min').value = String(limits.xMin);
  el('input-limit-x-max').value = String(limits.xMax);
  el('input-limit-y-min').value = String(limits.yMin);
  el('input-limit-y-max').value = String(limits.yMax);
  el('input-limit-z-min').value = String(limits.zMin);
  el('input-limit-z-max').value = String(limits.zMax);
}

function syncPhysicalGeometryReadouts(geo) {
  if (!geo) {
    return;
  }
  el('input-probe-diameter').value = String(geo.diameter);
  el('input-probe-thickness').value = String(geo.thickness);
  el('input-probe-tool').value = String(geo.toolDiameter);
  el('input-probe-tool-height').value = String(geo.toolCylindricalHeight);
  if (typeof gantry.setToolGeometry === 'function') {
    gantry.setToolGeometry({
      toolDiameter: geo.toolDiameter,
      toolCylindricalHeight: geo.toolCylindricalHeight,
    });
  }
}

function syncProbeDiscInputs(probe) {
  if (probeInputsTouched || !probe) {
    return;
  }
  if (Number.isFinite(probe.x)) el('input-probe-x').value = String(probe.x);
  if (Number.isFinite(probe.y)) el('input-probe-y').value = String(probe.y);
  if (Number.isFinite(probe.z)) el('input-probe-z').value = String(probe.z);
}

function readProbeDiscInputs() {
  return plantProbeDiscCenterOnly({
    x: readNumberInput('input-probe-x', 40),
    y: readNumberInput('input-probe-y', 40),
    z: readNumberInput('input-probe-z', 10),
  });
}

async function pollPlantState() {
  try {
    const result = await fetchPlantState();
    renderPlantState(result.state);
    syncLimitInputs(result.state.limits);
    syncProbeDiscInputs(result.state.probe);
    lastPlantLimits = normalizePlantLimits(result.state.limits);
    gantry.setLimits(lastPlantLimits);
    if (
      result.pose &&
      Number.isFinite(result.pose.x) &&
      Number.isFinite(result.pose.y) &&
      Number.isFinite(result.pose.z)
    ) {
      lastPlantPose = { x: result.pose.x, y: result.pose.y, z: result.pose.z };
      const coreOff = result.referenceOffset;
      if (
        coreOff &&
        [coreOff.x, coreOff.y, coreOff.z].every((v) => typeof v === 'number' && Number.isFinite(v))
      ) {
        lastCoreReferenceOffset = { x: coreOff.x, y: coreOff.y, z: coreOff.z };
      }
      if (lastStatus && lastStatus.connected !== false) {
        gantry.setPose(lastPlantPose);
        refreshMachineOverlay(lastStatus);
        updateReferenceMarkers(lastStatus);
      }
    }
  } catch (err) {
    dom.probeState.textContent = '—';
    dom.limitState.textContent = '—';
    setPlantFeedback(err.message, true);
  }
}

async function refreshCalibration() {
  try {
    const result = await fetchCalibration();
    if (result && result.ok && result.calibration) {
      calibrationSnapshot = result.calibration;
    }
  } catch {
    // keep last snapshot
  }
  try {
    const cfg = await fetchMachineConfig();
    if (cfg && cfg.ok && cfg.config) {
      machineConfigSnapshot = cfg.config;
      syncPhysicalGeometryReadouts(physicalGeometryFromMachineConfig(machineConfigSnapshot));
    }
  } catch {
    // keep last
  }
  refreshMachineOverlay(lastStatus);
}

async function fetchProgramPoints() {
  try {
    const res = await fetch('/api/program/selection');
    const data = await res.json();
    if (data.ok && data.selection && data.selection.confirmed && data.selection.points?.length) {
      const next = data.selection.points
        .slice()
        .sort((a, b) => a.sequenceNumber - b.sequenceNumber);
      const changed =
        next.length !== plateProgramPoints.length ||
        next.some(
          (p, i) =>
            !plateProgramPoints[i] ||
            p.x !== plateProgramPoints[i].x ||
            p.y !== plateProgramPoints[i].y ||
            p.z !== plateProgramPoints[i].z
        );
      plateProgramPoints = next;
      if (changed) {
        plannedPathStartPose = null;
        traveledSamples = [];
        lastSampleKey = '';
      }
      if (dom.programFrameNote) {
        dom.programFrameNote.hidden = false;
        dom.programFrameNote.textContent = `${next.length} ausgewählte Prüfpunkte (bestätigte Auswahl).`;
      }
      await refreshCalibration();
      return;
    }
    plateProgramPoints = [];
    plannedPathStartPose = null;
    if (dom.programFrameNote) {
      dom.programFrameNote.hidden = false;
      dom.programFrameNote.textContent =
        'Keine bestätigte Prüfpunkt-Auswahl — keine Punkte in der Simulation.';
    }
    refreshMachineOverlay(lastStatus);
  } catch {
    // program API optional for simulation view
  }
}

async function runPlantAction(action, successMessage) {
  try {
    const result = await action();
    if (result && result.state) {
      renderPlantState(result.state);
      lastPlantLimits = normalizePlantLimits(result.state.limits);
      gantry.setLimits(lastPlantLimits);
      if (lastStatus) {
        updateTrajectory(lastStatus);
      }
    }
    setPlantFeedback(successMessage, false);
  } catch (err) {
    setPlantFeedback(err.message, true);
  }
}

function readInjectedMagnitude() {
  const value = Number(el('input-injected-force-n').value);
  if (!Number.isFinite(value) || value < 0) {
    return 0;
  }
  return Math.min(value, INJECTED_FORCE_MAX_N);
}

function injectedVectorFor(axis, sign, magnitude) {
  const vector = { fx: 0, fy: 0, fz: 0 };
  if (axis === 'x') {
    vector.fx = sign * magnitude;
  } else if (axis === 'y') {
    vector.fy = sign * magnitude;
  } else if (axis === 'z') {
    vector.fz = sign * magnitude;
  }
  return vector;
}

function clearInjectedHoldTimer() {
  if (injectedHoldTimer != null) {
    window.clearInterval(injectedHoldTimer);
    injectedHoldTimer = null;
  }
}

async function sendInjectedForce(vector, successMessage) {
  try {
    const result = await setPlantInjectedForce(vector);
    if (result && result.state) {
      renderPlantState(result.state);
    }
    if (successMessage) {
      setPlantFeedback(successMessage, false);
    }
  } catch (err) {
    setPlantFeedback(err.message, true);
  }
}

async function stopInjectedHold() {
  const active = injectedHold;
  if (!active) {
    clearInjectedHoldTimer();
    return;
  }
  injectedHold = null;
  clearInjectedHoldTimer();
  active.btn.classList.remove('sim-force-btn--active');
  try {
    if (active.pointerId != null) {
      active.btn.releasePointerCapture(active.pointerId);
    }
  } catch {
    // already released
  }
  await sendInjectedForce({ fx: 0, fy: 0, fz: 0 }, 'Externe Kraft gelöst (0 N)');
}

async function startInjectedHold(btn, event) {
  const axis = btn.dataset.axis;
  const sign = Number(btn.dataset.sign);
  if (!axis || !Number.isFinite(sign)) {
    return;
  }
  if (injectedHold) {
    await stopInjectedHold();
  }
  const magnitude = readInjectedMagnitude();
  const vector = injectedVectorFor(axis, sign, magnitude);
  injectedHold = { axis, sign, pointerId: event.pointerId, btn };
  btn.classList.add('sim-force-btn--active');
  try {
    btn.setPointerCapture(event.pointerId);
  } catch {
    // capture optional
  }
  await sendInjectedForce(
    vector,
    `Externe Kraft ${sign > 0 ? '+' : '−'}${axis.toUpperCase()} ${magnitude} N`
  );
  clearInjectedHoldTimer();
  injectedHoldTimer = window.setInterval(() => {
    if (!injectedHold) {
      return;
    }
    const refresh = injectedVectorFor(injectedHold.axis, injectedHold.sign, readInjectedMagnitude());
    void sendInjectedForce(refresh);
  }, INJECTED_HOLD_REFRESH_MS);
}

function bindInjectedForcePad() {
  const buttons = document.querySelectorAll('.sim-force-btn');
  const onPointerDown = (event) => {
    if (event.button != null && event.button !== 0) {
      return;
    }
    event.preventDefault();
    void startInjectedHold(event.currentTarget, event);
  };
  const onPointerUp = (event) => {
    if (!injectedHold) {
      return;
    }
    if (injectedHold.pointerId != null && event.pointerId !== injectedHold.pointerId) {
      return;
    }
    event.preventDefault();
    void stopInjectedHold();
  };
  const onClick = (event) => {
    event.preventDefault();
  };
  buttons.forEach((btn) => {
    btn.addEventListener('pointerdown', onPointerDown);
    btn.addEventListener('pointerup', onPointerUp);
    btn.addEventListener('pointercancel', onPointerUp);
    btn.addEventListener('lostpointercapture', onPointerUp);
    btn.addEventListener('click', onClick);
  });
  window.addEventListener('blur', () => {
    if (injectedHold) {
      void stopInjectedHold();
    }
  });
  window.addEventListener('pagehide', () => {
    if (injectedHold) {
      void stopInjectedHold();
    }
  });
}

function bindPanel() {
  for (const id of [
    'input-limit-x-min', 'input-limit-x-max',
    'input-limit-y-min', 'input-limit-y-max',
    'input-limit-z-min', 'input-limit-z-max',
  ]) {
    el(id).addEventListener('input', () => {
      limitInputsTouched = true;
    });
  }

  for (const id of ['input-probe-x', 'input-probe-y', 'input-probe-z']) {
    el(id).addEventListener('input', () => {
      probeInputsTouched = true;
    });
  }

  el('btn-probe-trigger').addEventListener('click', () => {
    runPlantAction(
      () => triggerPlantProbe(PROBE_PULSE_MS),
      `Messtaster-Impuls gesendet (${PROBE_PULSE_MS} ms)`
    );
  });

  el('btn-apply-probe-disc').addEventListener('click', () => {
    const disc = readProbeDiscInputs();
    runPlantAction(() => setPlantProbeDisc(disc), 'Scheibenposition übernommen');
  });

  el('btn-apply-limits').addEventListener('click', () => {
    const limits = readLimitInputs();
    runPlantAction(() => setPlantLimits(limits), 'Endanschläge übernommen');
  });

  el('btn-estop-toggle').addEventListener('click', () => {
    const next = !estopActive;
    runPlantAction(
      () => setPlantEStop(next),
      next ? 'E-Stop-Eingang aktiviert' : 'E-Stop-Eingang freigegeben'
    );
  });

  el('btn-apply-force-stiffness').addEventListener('click', () => {
    const nPerMm = Number(el('input-force-stiffness').value);
    runPlantAction(
      () => setPlantForceStiffness(nPerMm),
      `Kraftsteifigkeit gesetzt: ${nPerMm} N/mm`
    );
  });

  bindInjectedForcePad();

  el('toggle-trajectory').addEventListener('change', (event) => {
    gantry.setShowTrajectory(event.target.checked);
    if (lastStatus) {
      updateTrajectory(lastStatus);
    } else {
      updateOverlayLabels();
    }
  });

  el('toggle-limit-markers').addEventListener('change', (event) => {
    gantry.setShowLimitMarkers(event.target.checked);
  });

  for (const id of ['toggle-ref-home', 'toggle-ref-plate', 'toggle-ref-probe', 'toggle-ref-points']) {
    const node = el(id);
    if (node) {
      node.addEventListener('change', () => updateReferenceMarkers(lastStatus));
    }
  }

  for (const id of ['toggle-point-labels', 'toggle-segment-labels']) {
    const node = el(id);
    if (node) {
      node.addEventListener('change', () => updateOverlayLabels());
    }
  }

  el('btn-apply-geometry').addEventListener('click', () => {
    const geo = readGeometryInputs();
    if (machineConfigSnapshot) {
      geo.toolDiameter = Number(machineConfigSnapshot.tool_diameter) || 2.5;
      geo.toolCylindricalHeight = Number(machineConfigSnapshot.tool_cylindrical_height) || 15;
    }
    gantry.setGeometry(geo);
    setPlantFeedback('Darstellungsgeometrie aktualisiert', false);
  });

  el('btn-sim-reset-camera').addEventListener('click', () => gantry.resetCamera());
}

function init() {
  bindPanel();
  gantry.setShowLimitMarkers(el('toggle-limit-markers').checked);
  gantry.setShowTrajectory(el('toggle-trajectory').checked);
  gantry.setLimits(readLimitInputs());

  connectStatus(renderStatus, (connected, message) => {
    setHeaderStatus(message || (connected ? 'verbunden' : 'getrennt'), !connected);
  });

  pollPlantState();
  refreshCalibration();
  fetchProgramPoints();
  window.setInterval(pollPlantState, PLANT_POLL_MS);
  window.setInterval(fetchProgramPoints, PROGRAM_POLL_MS);
  window.setInterval(refreshCalibration, PROGRAM_POLL_MS);
}

init();
