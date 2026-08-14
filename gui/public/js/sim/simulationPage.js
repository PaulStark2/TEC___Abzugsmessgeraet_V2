/**
 * M15/M16 simulation UI — display only; Core is motion authority.
 */
import { connectStatus } from '/js/api.js';
import { createGantryView } from '/js/sim/gantryView.js';
import {
  fetchPlantState,
  setPlantEStop,
  setPlantLimits,
  triggerPlantProbe,
} from '/js/sim/plantApi.js';

const PLANT_POLL_MS = 200;
const PROGRAM_POLL_MS = 2000;
const PROBE_PULSE_MS = 100;

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
  plantFeedback: el('sim-plant-feedback'),
  probeState: el('sim-probe-state'),
  limitState: el('sim-limit-state'),
  estopPlant: el('sim-estop-plant'),
};

const gantry = createGantryView(el('gantry-canvas'), readGeometryInputs());

let programPoints = [];
let lastStatus = null;
let estopActive = false;
let limitInputsTouched = false;

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

function updateTrajectory(status) {
  if (!programPoints.length) {
    gantry.setPlannedTrajectory([]);
    gantry.setTraveledTrajectory([], null);
    return;
  }
  gantry.setPlannedTrajectory(programPoints);
  const idx = typeof status?.currentPointIndex === 'number' ? status.currentPointIndex : 0;
  const completed = programPoints.slice(0, Math.max(0, idx));
  gantry.setTraveledTrajectory(completed, { x: status.x, y: status.y, z: status.z });
}

function renderStatus(status) {
  lastStatus = status;
  dom.clientMode.textContent = status.machineClientMode || '—';
  dom.machineState.textContent = status.machineState || '—';
  dom.motionState.textContent = status.motionState || '—';
  dom.homingState.textContent = status.homingState || 'None';
  dom.homedState.textContent = status.isHomed ? 'ja' : 'nein';
  dom.warningState.textContent = status.limitWarning ? 'Limit aktiv' : '—';
  dom.x.textContent = formatNumber(status.x);
  dom.y.textContent = formatNumber(status.y);
  dom.z.textContent = formatNumber(status.z);
  dom.fx.textContent = formatNumber(status.fx);
  dom.fy.textContent = formatNumber(status.fy);
  dom.fz.textContent = formatNumber(status.fz);
  dom.limits.textContent = formatLimits(status);
  dom.estop.textContent = status.eStop ? 'AKTIV' : 'inaktiv';

  if (status.connected === false) {
    setHeaderStatus('Offline — Pose eingefroren, keine lokale Bewegung', true);
    return;
  }

  gantry.setPose({ x: status.x, y: status.y, z: status.z });
  updateTrajectory(status);
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

async function pollPlantState() {
  try {
    const result = await fetchPlantState();
    renderPlantState(result.state);
    syncLimitInputs(result.state.limits);
    gantry.setLimits(normalizePlantLimits(result.state.limits));
  } catch (err) {
    dom.probeState.textContent = '—';
    dom.limitState.textContent = '—';
    setPlantFeedback(err.message, true);
  }
}

async function fetchProgramPoints() {
  try {
    let res = await fetch('/api/program/selection');
    let data = await res.json();
    if (data.ok && data.selection && data.selection.confirmed && data.selection.points?.length) {
      programPoints = data.selection.points
        .slice()
        .sort((a, b) => a.sequenceNumber - b.sequenceNumber);
      gantry.setTestPoints(programPoints);
      if (lastStatus) {
        updateTrajectory(lastStatus);
      }
      return;
    }
    res = await fetch('/api/program/current');
    data = await res.json();
    if (data.ok && data.current?.testPoints?.length) {
      programPoints = data.current.testPoints
        .slice()
        .sort((a, b) => a.sequenceNumber - b.sequenceNumber);
      gantry.setTestPoints(programPoints);
      if (lastStatus) {
        updateTrajectory(lastStatus);
      }
    }
  } catch {
    // program API optional for simulation view
  }
}

async function runPlantAction(action, successMessage) {
  try {
    const result = await action();
    if (result && result.state) {
      renderPlantState(result.state);
      gantry.setLimits(normalizePlantLimits(result.state.limits));
    }
    setPlantFeedback(successMessage, false);
  } catch (err) {
    setPlantFeedback(err.message, true);
  }
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

  el('btn-probe-trigger').addEventListener('click', () => {
    runPlantAction(
      () => triggerPlantProbe(PROBE_PULSE_MS),
      `Messtaster-Impuls gesendet (${PROBE_PULSE_MS} ms)`
    );
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

  el('toggle-trajectory').addEventListener('change', (event) => {
    gantry.setShowTrajectory(event.target.checked);
    if (lastStatus) {
      updateTrajectory(lastStatus);
    }
  });

  el('toggle-limit-markers').addEventListener('change', (event) => {
    gantry.setShowLimitMarkers(event.target.checked);
  });

  el('btn-apply-geometry').addEventListener('click', () => {
    gantry.setGeometry(readGeometryInputs());
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
  fetchProgramPoints();
  window.setInterval(pollPlantState, PLANT_POLL_MS);
  window.setInterval(fetchProgramPoints, PROGRAM_POLL_MS);
}

init();
