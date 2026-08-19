'use strict';

/**
 * HTTP command + WebSocket status (G2/G3). Browser does not simulate motion.
 */

let statusCallback = null;
let connectionCallback = null;
let reconnectTimer = null;

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
  if (result.status && statusCallback) {
    statusCallback(result.status);
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

/**
 * Latest procedure results (G9).
 */
export async function fetchCurrentResults() {
  const response = await fetch('/api/results/current', { method: 'GET' });
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

export async function fetchParameterCatalog() {
  const response = await fetch('/api/parameters/catalog', { method: 'GET' });
  return response.json();
}

export async function fetchMachineConfig() {
  const response = await fetch('/api/machine-config', { method: 'GET' });
  return response.json();
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

export async function startProbeCalibration() {
  const response = await fetch('/api/calibration/probe-start', { method: 'POST' });
  const result = await response.json();
  if (!response.ok || !result.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}

export async function syncCalibrationFromController() {
  const response = await fetch('/api/calibration/sync', { method: 'POST' });
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
export function connectStatus(onStatus, onConnection) {
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
    if (message.type !== 'status' || !statusCallback) {
      return;
    }
    statusCallback(message);
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
      connectStatus(onStatus, onConnection);
    }, 2000);
  });

  socket.addEventListener('error', () => {
    socket.close();
  });
}
