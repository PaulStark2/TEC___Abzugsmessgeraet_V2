/**
 * M15 simulation UI bootstrap (standalone /simulation page).
 *
 * Responsibilities:
 *  - render the controller status and drive the 3D gantry from the Core pose
 *  - collect operator input for the virtual outside world (sensors)
 *
 * Explicit non-responsibilities: no motion logic, no kinematics, no safety
 * reaction. The Core decides how the machine moves.
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
const PROBE_PULSE_MS = 100;

const el = (id) => document.getElementById(id);

const dom = {
  status: el('sim-status'),
  clientMode: el('sim-client-mode'),
  machineState: el('sim-machine-state'),
  motionState: el('sim-motion-state'),
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
};

const gantry = createGantryView(el('gantry-canvas'), readGeometryInputs());

function formatNumber(value) {
  return typeof value === 'number' && Number.isFinite(value) ? value.toFixed(2) : '—';
}

// Limit bit order matches the M12 StatusReport bitmask (XMin..ZMax).
const LIMIT_LABELS = ['X-', 'X+', 'Y-', 'Y+', 'Z-', 'Z+'];

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
    xMax: readNumberInput('input-limit-x', 100),
    yMax: readNumberInput('input-limit-y', 100),
    zMax: readNumberInput('input-limit-z', 50),
  };
}

/**
 * Render controller status. Display only — never derive a pose locally.
 * @param {object} status flat status message from /ws/status
 */
function renderStatus(status) {
  dom.clientMode.textContent = status.machineClientMode || '—';
  dom.machineState.textContent = status.machineState || '—';
  dom.motionState.textContent = status.motionState || '—';
  dom.x.textContent = formatNumber(status.x);
  dom.y.textContent = formatNumber(status.y);
  dom.z.textContent = formatNumber(status.z);
  dom.fx.textContent = formatNumber(status.fx);
  dom.fy.textContent = formatNumber(status.fy);
  dom.fz.textContent = formatNumber(status.fz);
  dom.limits.textContent = formatLimits(status);
  dom.estop.textContent = status.eStop ? 'AKTIV' : 'inaktiv';

  // Offline: keep the last known pose (no local movement, no interpolation).
  if (status.connected === false) {
    setHeaderStatus('Controller offline — Pose eingefroren, keine lokale Bewegung', true);
    return;
  }

  gantry.setPose({ x: status.x, y: status.y, z: status.z });

  if (status.connected === true) {
    setHeaderStatus('Controller verbunden — Pose aus Core-Status', false);
  }
}

function renderPlantState(state) {
  const activeLimits = [];
  if (state.limitXMax) activeLimits.push('X+');
  if (state.limitYMax) activeLimits.push('Y+');
  if (state.limitZMax) activeLimits.push('Z+');

  dom.limitState.textContent = activeLimits.length ? `${activeLimits.join(' ')} HIGH` : 'alle LOW';
  dom.probeState.textContent = state.probeTriggered ? 'HIGH' : 'LOW';
  dom.probeState.parentElement.classList.toggle('sim-readout--active', state.probeTriggered);
  dom.limitState.parentElement.classList.toggle('sim-readout--active', activeLimits.length > 0);
}

// The plant owns the limit values; mirror them until the operator edits a field.
let limitInputsTouched = false;

function syncLimitInputs(limits) {
  if (limitInputsTouched || !limits) {
    return;
  }
  el('input-limit-x').value = String(limits.xMax);
  el('input-limit-y').value = String(limits.yMax);
  el('input-limit-z').value = String(limits.zMax);
}

async function pollPlantState() {
  try {
    const result = await fetchPlantState();
    renderPlantState(result.state);
    syncLimitInputs(result.state.limits);
    gantry.setLimits(result.state.limits);
    el('input-estop').checked = Boolean(result.state.eStop);
  } catch (err) {
    dom.probeState.textContent = '—';
    dom.limitState.textContent = '—';
    setPlantFeedback(err.message, true);
  }
}

async function runPlantAction(action, successMessage) {
  try {
    const result = await action();
    if (result && result.state) {
      renderPlantState(result.state);
      gantry.setLimits(result.state.limits);
    }
    setPlantFeedback(successMessage, false);
  } catch (err) {
    setPlantFeedback(err.message, true);
  }
}

function bindPanel() {
  for (const id of ['input-limit-x', 'input-limit-y', 'input-limit-z']) {
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
    runPlantAction(
      () => setPlantLimits(limits),
      `Endschalter gesetzt: X+${limits.xMax} Y+${limits.yMax} Z+${limits.zMax}`
    );
  });

  el('input-estop').addEventListener('change', (event) => {
    const active = event.target.checked;
    runPlantAction(
      () => setPlantEStop(active),
      active ? 'E-Stop-Eingang aktiviert' : 'E-Stop-Eingang freigegeben'
    );
  });

  el('btn-apply-geometry').addEventListener('click', () => {
    gantry.setGeometry(readGeometryInputs());
    gantry.setLimits(readLimitInputs());
    setPlantFeedback('Darstellungsgeometrie aktualisiert (kein Core-Einfluss)', false);
  });

  el('btn-sim-reset-camera').addEventListener('click', () => gantry.resetCamera());
}

function init() {
  bindPanel();
  gantry.setLimits(readLimitInputs());

  connectStatus(renderStatus, (connected, message) => {
    setHeaderStatus(message || (connected ? 'verbunden' : 'getrennt'), !connected);
  });

  pollPlantState();
  window.setInterval(pollPlantState, PLANT_POLL_MS);
}

init();
