/**
 * M15 browser client for the simulation plant channel (via Node proxy).
 * Sensor injection only — machine commands stay on the M12 path.
 */

async function post(url, body) {
  const response = await fetch(url, {
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
  if (!result.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}

export async function fetchPlantState() {
  const response = await fetch('/api/simulation/state');
  let result;
  try {
    result = await response.json();
  } catch {
    throw new Error('Ungültige Serverantwort');
  }
  if (!result.ok) {
    throw new Error(result.error || `HTTP ${response.status}`);
  }
  return result;
}

export function setPlantLimits(limits) {
  return post('/api/simulation/limits', limits);
}

export function setPlantProbeDisc(disc) {
  return post('/api/simulation/probe-disc', disc);
}

export function triggerPlantProbe(pulseMs) {
  return post('/api/simulation/probe', { pulseMs });
}

export function setPlantEStop(active) {
  return post('/api/simulation/estop', { active });
}

export function setPlantForceStiffness(nPerMm) {
  return post('/api/simulation/force-stiffness', { nPerMm });
}

export function setPlantInjectedForce(force) {
  return post('/api/simulation/injected-force', {
    fx: Number(force && force.fx),
    fy: Number(force && force.fy),
    fz: Number(force && force.fz),
  });
}
