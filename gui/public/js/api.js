'use strict';

/**
 * HTTP command + WebSocket status (G2/G3). Browser does not simulate motion.
 */

let statusCallback = null;
let connectionCallback = null;
let reconnectTimer = null;
/** @type {Set<(status: object) => void>} */
const statusListeners = new Set();

function notifyStatusListeners(status) {
  if (statusCallback) {
    statusCallback(status);
  }
  for (const listener of statusListeners) {
    try {
      listener(status);
    } catch {
      // page listeners must not break the status bus
    }
  }
}

/**
 * Subscribe to live machine status (WebSocket). Returns unsubscribe fn.
 * @param {(status: object) => void} listener
 */
export function subscribeLiveStatus(listener) {
  if (typeof listener !== 'function') {
    return () => {};
  }
  statusListeners.add(listener);
  return () => {
    statusListeners.delete(listener);
  };
}

/**
 * @param {object} body
 */
export async function sendCommand(body) {
  const response = await fetch('/api/command', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  let result;
  try {
    result = await response.json();
  } catch {
    throw new Error('Ungültige Serverantwort');
  }
  if (!response.ok || !result.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  if (result.status) {
    notifyStatusListeners(result.status);
  }
  return result;
}

/**
 * Upload and validate a TestProgram JSON (G5).
 * Browser must not perform motion logic; server is validation authority.
 * @param {object} program
 * @returns {Promise<{ok:boolean, summary?:object, errors?:Array}>}
 */
export async function uploadProgram(program) {
  const response = await fetch('/api/program/upload', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(program),
  });

  let result;
  try {
    result = await response.json();
  } catch {
    throw new Error('Ungültige Serverantwort');
  }

  // Wichtig: Validierungsfehler sollen im UI als Liste darstellbar sein.
  // Deshalb nicht werfen, sondern das result-Objekt zurückgeben.
  if (!response.ok) {
    return result;
  }

  return result;
}

/**
 * Fetch the currently loaded program (G6-prep).
 * @returns {Promise<{ok:boolean, current?:object, error?:string}>}
 */
export async function fetchCurrentProgram() {
  const response = await fetch('/api/program/current', { method: 'GET' });
  let result;
  try {
    result = await response.json();
  } catch {
    throw new Error('Ungültige Serverantwort');
  }

  if (!response.ok) {
    return result;
  }

  return result;
}

/**
 * Fetch current point selection draft/confirmed state (G6).
 * @returns {Promise<{ok:boolean, selection?:object, error?:string}>}
 */
export async function fetchProgramSelection() {
  const response = await fetch('/api/program/selection', { method: 'GET' });
  let result;
  try {
    result = await response.json();
  } catch {
    throw new Error('Ungültige Serverantwort');
  }

  if (!response.ok) {
    return result;
  }

  return result;
}

/**
 * Update selection draft (G6).
 * @param {{ selectedSequenceNumbers?: number[], selectAll?: boolean }} body
 */
export async function updateProgramSelection(body) {
  const response = await fetch('/api/program/selection', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });

  let result;
  try {
    result = await response.json();
  } catch {
    throw new Error('Ungültige Serverantwort');
  }

  if (!response.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }

  return result;
}

/**
 * Confirm current selection (G6).
 */
export async function confirmProgramSelection() {
  const response = await fetch('/api/program/selection/confirm', { method: 'POST' });
  let result;
  try {
    result = await response.json();
  } catch {
    throw new Error('Ungültige Serverantwort');
  }

  if (!response.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }

  return result;
}

/**
 * Clear selection draft (G6).
 */
export async function clearProgramSelection() {
  const response = await fetch('/api/program/selection', { method: 'DELETE' });
  let result;
  try {
    result = await response.json();
  } catch {
    throw new Error('Ungültige Serverantwort');
  }

  if (!response.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }

  return result;
}

/**
 * Delete the currently loaded program (clears selection, keeps results).
 */
/**
 * List taught/saved programs on the device (gui/server/data/programs).
 */
export async function fetchProgramFiles() {
  const response = await fetch('/api/program/files', { method: 'GET' });
  let result;
  try {
    result = await response.json();
  } catch {
    throw new Error('Ungültige Serverantwort');
  }
  if (!response.ok || !result.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}

/**
 * Teach current TCP as a plate-frame Prüfpunkt into a new or existing program JSON.
 * @param {{ mode: 'create'|'append', pointName: string, programName?: string, fileName?: string }} body
 */
export async function teachProgramPoint(body) {
  const response = await fetch('/api/program/teach', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body || {}),
  });
  let result;
  try {
    result = await response.json();
  } catch {
    throw new Error('Ungültige Serverantwort');
  }
  if (!response.ok || !result.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}

/**
 * Load a device-stored program JSON into RAM (same as upload).
 * @param {string} fileName
 */
export async function loadProgramFile(fileName) {
  const response = await fetch(`/api/program/files/${encodeURIComponent(fileName)}/load`, {
    method: 'POST',
  });
  let result;
  try {
    result = await response.json();
  } catch {
    throw new Error('Ungültige Serverantwort');
  }
  if (!response.ok || !result.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}

export async function deleteCurrentProgram() {
  const response = await fetch('/api/program/current', { method: 'DELETE' });
  let result;
  try {
    result = await response.json();
  } catch {
    throw new Error('Ungültige Serverantwort');
  }

  if (!response.ok) {
    return result;
  }

  return result;
}

/**
 * Procedure status (G8).
 */
export async function fetchProcedureStatus() {
  const response = await fetch('/api/procedure/status', { method: 'GET' });
  let result;
  try {
    result = await response.json();
  } catch {
    throw new Error('Ungültige Serverantwort');
  }
  if (!response.ok) {
    return result;
  }
  return result;
}

/**
 * @param {{ mode: string, insertionForce: number, iterations: number }} body
 */
export async function startProcedure(body) {
  const response = await fetch('/api/procedure/start', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  let result;
  try {
    result = await response.json();
  } catch {
    throw new Error('Ungültige Serverantwort');
  }
  if (!response.ok || !result.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}

export async function pauseProcedure() {
  const response = await fetch('/api/procedure/pause', { method: 'POST' });
  let result;
  try {
    result = await response.json();
  } catch {
    throw new Error('Ungültige Serverantwort');
  }
  if (!response.ok || !result.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}

export async function resumeProcedure() {
  const response = await fetch('/api/procedure/resume', { method: 'POST' });
  let result;
  try {
    result = await response.json();
  } catch {
    throw new Error('Ungültige Serverantwort');
  }
  if (!response.ok || !result.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}


export async function sendProcedureDecision(action) {
  const response = await fetch('/api/procedure/decision', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ action }),
  });
  let result;
  try {
    result = await response.json();
  } catch {
    throw new Error('Ungültige Serverantwort');
  }
  if (!response.ok || !result.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}

export async function stopProcedure() {
  const response = await fetch('/api/procedure/stop', { method: 'POST' });
  let result;
  try {
    result = await response.json();
  } catch {
    throw new Error('Ungültige Serverantwort');
  }
  if (!response.ok || !result.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}

export async function resetProcedureUi() {
  const response = await fetch('/api/procedure/reset-ui', { method: 'POST' });
  let result;
  try {
    result = await response.json();
  } catch {
    throw new Error('Ungültige Serverantwort');
  }
  if (!response.ok || !result.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}

/**
 * Latest procedure results (G9).
 * @param {number} [index] optional history index (0 = newest)
 */
export async function fetchCurrentResults(index) {
  const q =
    index !== undefined && index !== null && Number.isFinite(Number(index))
      ? `?index=${encodeURIComponent(String(index))}`
      : '';
  const response = await fetch(`/api/results/current${q}`, { method: 'GET' });
  let result;
  try {
    result = await response.json();
  } catch {
    throw new Error('Ungültige Serverantwort');
  }
  if (!response.ok) {
    return result;
  }
  return result;
}

export async function fetchResultsHistory() {
  const response = await fetch('/api/results/history', { method: 'GET' });
  let result;
  try {
    result = await response.json();
  } catch {
    throw new Error('Ungültige Serverantwort');
  }
  if (!response.ok) {
    return result;
  }
  return result;
}

/**
 * Seed realistic Kraft-Weg demo results (offline GUI validation, no test stand).
 */
export async function loadDemoResults() {
  const response = await fetch('/api/results/demo', { method: 'POST' });
  let result;
  try {
    result = await response.json();
  } catch {
    throw new Error('Ungültige Serverantwort');
  }
  if (!response.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}

/** Clear in-memory results (demo or last run). Real runs also clear on procedure start. */
export async function clearCurrentResults() {
  const response = await fetch('/api/results/current', { method: 'DELETE' });
  let result;
  try {
    result = await response.json();
  } catch {
    throw new Error('Ungültige Serverantwort');
  }
  if (!response.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}

export async function fetchParameterCatalog() {
  const response = await fetch('/api/parameters/catalog', { method: 'GET' });
  return response.json();
}

export async function fetchMachineConfig() {
  const response = await fetch('/api/machine-config', { method: 'GET' });
  return response.json();
}

/**
 * Latest controller status snapshot (M12 StatusReport via GUI server).
 */
export async function fetchMachineStatus() {
  const response = await fetch('/api/machine', { method: 'GET' });
  let result;
  try {
    result = await response.json();
  } catch {
    throw new Error('Ungültige Serverantwort');
  }
  if (!response.ok || !result.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result.status || {};
}

export async function unlockParameters(password) {
  const response = await fetch('/api/parameters/unlock', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ password }),
  });
  const result = await response.json();
  if (!response.ok || !result.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}

export async function saveMachineConfig(config) {
  const response = await fetch('/api/machine-config', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ config }),
  });
  const result = await response.json();
  if (!response.ok || !result.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}

export async function invalidateHoming() {
  const response = await fetch('/api/machine/invalidate-homing', { method: 'POST' });
  const result = await response.json();
  if (!response.ok || !result.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}

export async function setCollisionGuard(enabled) {
  return sendCommand({ command: 'setCollisionGuard', enabled: enabled === true });
}

export async function pneumaticOpen() {
  return sendCommand({ command: 'pneumaticOpen' });
}

export async function pneumaticClose() {
  return sendCommand({ command: 'pneumaticClose' });
}

export async function fixtureOpen() {
  return sendCommand({ command: 'fixtureOpen' });
}

export async function fixtureClose() {
  return sendCommand({ command: 'fixtureClose' });
}

export async function submitForceCalibration(body) {
  const response = await fetch('/api/calibration/force', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  const result = await response.json();
  if (!response.ok && !result.report) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}

export async function selectForceCurve(body) {
  const response = await fetch('/api/calibration/force-select', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  const result = await response.json();
  if (!response.ok && !result.report) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}

export async function fetchCalibration() {
  const response = await fetch('/api/calibration', { method: 'GET' });
  return response.json();
}

export async function setPlateOrigin(pose) {
  const response = await fetch('/api/calibration/plate-origin', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(pose || {}),
  });
  const result = await response.json();
  if (!response.ok || !result.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}

export async function startPlateBoreSearch(pointId) {
  const response = await fetch('/api/calibration/plate-bore-start', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ pointId }),
  });
  const result = await response.json();
  if (!response.ok || !result.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}

export async function approachCalibPoint(pointId) {
  const response = await fetch('/api/calibration/approach-point', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ pointId }),
  });
  const result = await response.json();
  if (!response.ok || !result.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}

export async function setPlateYaw(body) {
  const response = await fetch('/api/calibration/plate-yaw', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body || {}),
  });
  const result = await response.json();
  if (!response.ok || !result.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}

export async function defineMcs() {
  const response = await fetch('/api/calibration/define-mcs', { method: 'POST' });
  const result = await response.json();
  if (!response.ok || !result.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}

export async function persistMcs(pose) {
  const response = await fetch('/api/calibration/mcs', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(pose || {}),
  });
  const result = await response.json();
  if (!response.ok || !result.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}

/** Abort Full Calibration before commit: re-push committed store into Core. */
export async function rollbackFullCalibration() {
  const response = await fetch('/api/calibration/rollback-full', { method: 'POST' });
  const result = await response.json();
  if (!response.ok || !result.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}

export async function startProbeCalibration() {
  const response = await fetch('/api/calibration/probe-start', { method: 'POST' });
  const result = await response.json();
  if (!response.ok || !result.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}

export async function syncCalibrationFromController(options = {}) {
  const persistProbeCenter = options.persistProbeCenter === true;
  const response = await fetch('/api/calibration/sync', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ persistProbeCenter }),
  });
  const result = await response.json();
  if (!response.ok || !result.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}

/**
 * @param {(status: object) => void} onStatus
 * @param {(connected: boolean, message?: string) => void} [onConnection]
 */
/**
 * @param {(status: object) => void} onStatus
 * @param {(connected: boolean, message?: string) => void} [onConnection]
 * @param {(evt: object) => void} [onMessageEvent]
 */
export function connectStatus(onStatus, onConnection, onMessageEvent) {
  statusCallback = onStatus;
  connectionCallback = onConnection;

  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  const url = `${protocol}//${window.location.host}/ws/status`;
  const socket = new WebSocket(url);

  socket.addEventListener('open', () => {
    if (connectionCallback) {
      connectionCallback(true, 'GUI läuft — Live-Status verbunden');
    }
  });

  socket.addEventListener('message', (event) => {
    let message;
    try {
      message = JSON.parse(event.data);
    } catch {
      return;
    }
    if (message.type === 'message_event') {
      if (typeof onMessageEvent === 'function') {
        onMessageEvent(message);
      }
      return;
    }
    if (message.type !== 'status') {
      return;
    }
    notifyStatusListeners(message);
  });

  socket.addEventListener('close', () => {
    if (connectionCallback) {
      connectionCallback(false, 'Live-Status getrennt — reconnect in 2s…');
    }
    if (reconnectTimer) {
      clearTimeout(reconnectTimer);
    }
    reconnectTimer = window.setTimeout(() => {
      reconnectTimer = null;
      connectStatus(onStatus, onConnection, onMessageEvent);
    }, 2000);
  });

  socket.addEventListener('error', () => {
    socket.close();
  });
}
