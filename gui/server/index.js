'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');
const { WebSocketServer } = require('ws');
const { createMachineClient } = require('./machineClient');
const { validateTestProgram } = require('./program/validateTestProgram');
const programStore = require('./program/programStore');
const selectionStore = require('./program/selectionStore');
const resultsStore = require('./results/resultsStore');
const procedureStore = require('./procedure/procedureStore');
const { createProcedureService } = require('./procedure/procedureService');
const { createPlantClient } = require('./simulation/plantClient');
const machineConfigStore = require('./config/machineConfigStore');
const { GROUPS: parameterGroups, validateMachineConfigPayload } = require('./config/parameterCatalog');
const calibrationStore = require('./config/calibrationStore');
const forcePathStore = require('./results/forcePathStore');
const { plantProbeDiscCenterOnly } = require('./coord/physicalGeometry');

const PORT = Number(process.env.PORT) || 3000;
const PUBLIC_DIR = path.join(__dirname, '..', 'public');
const THREE_DIR = path.join(__dirname, '..', 'node_modules', 'three');
const STATUS_HZ = 5;
const STATUS_INTERVAL_MS = 1000 / STATUS_HZ;
const TICK_HZ = 20;
const TICK_INTERVAL_MS = 1000 / TICK_HZ;

const MIME_TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'application/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.ico': 'image/x-icon',
};

function contentTypeFor(filePath) {
  const ext = path.extname(filePath).toLowerCase();
  return MIME_TYPES[ext] || 'application/octet-stream';
}

function isPathInside(rootDir, candidate) {
  const rel = path.relative(rootDir, candidate);
  return Boolean(rel) && !rel.startsWith('..') && !path.isAbsolute(rel);
}

function resolvePublicPath(urlPath) {
  const decoded = decodeURIComponent(urlPath.split('?')[0]);

  // Serve local npm three package offline (no CDN) under /vendor/three/
  if (decoded === '/vendor/three' || decoded.startsWith('/vendor/three/')) {
    const relative = decoded === '/vendor/three' ? '/' : decoded.slice('/vendor/three'.length);
    const absolute = path.normalize(path.join(THREE_DIR, relative));
    if (!isPathInside(THREE_DIR, absolute) && absolute !== THREE_DIR) {
      return null;
    }
    return absolute;
  }

  // M15: /simulation is a standalone page, not a hash route of the machine HMI.
  const rewritten =
    decoded === '/simulation' || decoded === '/simulation/' ? '/simulation.html' : decoded;

  const relative = rewritten === '/' ? '/index.html' : rewritten;
  const absolute = path.normalize(path.join(PUBLIC_DIR, relative));

  if (!isPathInside(PUBLIC_DIR, absolute) && absolute !== PUBLIC_DIR) {
    return null;
  }

  return absolute;
}

function readJsonBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    req.on('data', (chunk) => chunks.push(chunk));
    req.on('end', () => {
      const raw = Buffer.concat(chunks).toString('utf8');
      if (!raw) {
        resolve({});
        return;
      }
      try {
        resolve(JSON.parse(raw));
      } catch (err) {
        reject(err);
      }
    });
    req.on('error', reject);
  });
}

function sendJson(res, statusCode, body) {
  const payload = JSON.stringify(body);
  res.writeHead(statusCode, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(payload),
  });
  res.end(payload);
}

/**
 * Dispatch a command object to the MachineClient.
 * Supports sync clients (mock/sim) and async clients (protocol ACK wait).
 * @returns {Promise<{ ok: boolean, status?: object, error?: string, machineClientMode?: string, implementation?: string }>}
 */
async function handleCommand(body) {
  const command = body && body.command;
  if (typeof command !== 'string') {
    return { ok: false, error: 'Missing command' };
  }

  const implementation =
    machineClient.implementationName ||
    (typeof machineClient.tick === 'function' ? 'MockMachineClient?' : 'Unknown');
  const mode = machineClient.machineClientMode || process.env.MACHINE_CLIENT || 'mock';
  console.log(`[MACHINE] implementation = ${implementation} (mode=${mode}) command=${command}`);

  let result;
  switch (command) {
    case 'moveTo':
      if (typeof machineClient.moveTo !== 'function') {
        return { ok: false, error: 'moveTo not supported by client' };
      }
      result = await machineClient.moveTo(body.x, body.y, body.z);
      break;
    case 'pause':
      if (typeof machineClient.pause !== 'function') {
        return { ok: false, error: 'pause not supported by client' };
      }
      result = await machineClient.pause();
      break;
    case 'resume':
      if (typeof machineClient.resume !== 'function') {
        return { ok: false, error: 'resume not supported by client' };
      }
      result = await machineClient.resume();
      break;
    case 'stop':
      if (typeof machineClient.stop !== 'function') {
        return { ok: false, error: 'stop not supported by client' };
      }
      // Soft-STOP also aborts an active G8 procedure (mock or protocol adapter).
      if (procedureService) {
        try {
          await procedureService.stop();
        } catch {
          // ignore procedure stop errors; machine stop still proceeds
        }
      }
      result = await machineClient.stop();
      break;
    case 'setSpeed':
      if (typeof machineClient.setSpeed !== 'function') {
        return { ok: false, error: 'setSpeed not supported by client' };
      }
      result = await machineClient.setSpeed(body.speed);
      break;
    case 'jog':
      if (typeof machineClient.jog !== 'function') {
        return { ok: false, error: 'jog not supported by client' };
      }
      result = await machineClient.jog(body.axis, body.direction, body.step);
      break;
    case 'pneumaticOpen':
      if (typeof machineClient.pneumaticOpen !== 'function') {
        return { ok: false, error: 'pneumaticOpen not supported by client' };
      }
      result = await machineClient.pneumaticOpen();
      break;
    case 'pneumaticClose':
      if (typeof machineClient.pneumaticClose !== 'function') {
        return { ok: false, error: 'pneumaticClose not supported by client' };
      }
      result = await machineClient.pneumaticClose();
      break;
    case 'fixtureOpen':
      if (typeof machineClient.fixtureOpen !== 'function') {
        return { ok: false, error: 'fixtureOpen not supported by client' };
      }
      result = await machineClient.fixtureOpen();
      break;
    case 'fixtureClose':
      if (typeof machineClient.fixtureClose !== 'function') {
        return { ok: false, error: 'fixtureClose not supported by client' };
      }
      result = await machineClient.fixtureClose();
      break;
    case 'home':
      if (typeof machineClient.home !== 'function') {
        return { ok: false, error: 'home not supported by client' };
      }
      result = await machineClient.home();
      break;
    case 'clearFault':
    case 'resetFault':
      if (typeof machineClient.clearFault !== 'function') {
        return { ok: false, error: 'clearFault not supported by client' };
      }
      result = await machineClient.clearFault();
      break;
    default:
      return { ok: false, error: `Unknown command: ${command}` };
  }

  // Always stamp identity so the browser/tests can see which backend answered.
  if (result && typeof result === 'object') {
    result.machineClientMode = mode;
    result.implementation = implementation;
    if (result.status && typeof result.status === 'object') {
      result.status = {
        ...result.status,
        machineClientMode: mode,
        implementation,
      };
    }
    console.log(
      `[MACHINE] command=${command} ok=${Boolean(result.ok)}` +
        (result.error ? ` error=${result.error}` : '') +
        (result.status ? ` pose=(${result.status.x},${result.status.y},${result.status.z})` : '')
    );
  }
  return result;
}

const machineClient = createMachineClient();
const procedureService = createProcedureService(machineClient);
const plantClient = createPlantClient();
// GUI-only parameter-menu unlock. Not part of the STM32 safety circuit.
// Default 'tec-admin' is for development. Production password MUST be changed
// (environment VARIABLE PARAMETER_MENU_PASSWORD).
const PARAMETER_MENU_PASSWORD = process.env.PARAMETER_MENU_PASSWORD || 'tec-admin';
let parametersUnlocked = false;

if (typeof machineClient.onForcePathChunk === 'function') {
  machineClient.onForcePathChunk((chunk) => forcePathStore.appendChunk(chunk));
}

if (typeof machineClient.onSessionReady === 'function') {
  machineClient.onSessionReady(async () => {
    const cfg = machineConfigStore.load();
    if (typeof machineClient.setMachineConfig === 'function') {
      const pushed = await machineClient.setMachineConfig(cfg);
      if (!pushed.ok) {
        console.error(`[CONFIG] push on connect failed: ${pushed.error || 'unknown'}`);
      }
    }
    const calib = calibrationStore.load();
    if (calib.plateOriginValid && calib.plateOrigin && typeof machineClient.setPlateOrigin === 'function') {
      await machineClient.setPlateOrigin(calib.plateOrigin.x, calib.plateOrigin.y, calib.plateOrigin.z);
    }
    if (calib.probeCenterValid && calib.probeCenter && typeof machineClient.setProbeCenter === 'function') {
      const pushed = await machineClient.setProbeCenter(calib.probeCenter.x, calib.probeCenter.y, calib.probeCenter.z);
      if (!pushed.ok) {
        console.error(`[CALIB] probe_center push on connect failed: ${pushed.error || 'unknown'}`);
      }
    }
  });
}

/**
 * Forward a simulation plant result. Connection problems answer with 503 so the
 * simulation UI can report a plain connection error without breaking.
 */
function sendPlantResult(res, result) {
  if (result && result.ok) {
    sendJson(res, 200, result);
    return;
  }
  sendJson(res, result && result.offline ? 503 : 400, result || { ok: false, error: 'Unknown error' });
}

function readFiniteNumber(value) {
  const numeric = Number(value);
  return Number.isFinite(numeric) ? numeric : null;
}

function buildSummaryFromProgram(normalizedProgram) {
  const testPoints = normalizedProgram.testPoints || [];
  const xs = testPoints.map((p) => p.x);
  const ys = testPoints.map((p) => p.y);
  const zs = testPoints.map((p) => p.z);

  const pointRows = testPoints.map((p) => ({
    name: p.name,
    sequenceNumber: p.sequenceNumber,
    x: p.x,
    y: p.y,
    z: p.z,
  }));

  return {
    programName: normalizedProgram.programName ?? null,
    version: normalizedProgram.version,
    count: testPoints.length,
    pointsPreview: pointRows,
    testPoints: pointRows,
    xRange: { min: Math.min(...xs), max: Math.max(...xs) },
    yRange: { min: Math.min(...ys), max: Math.max(...ys) },
    zRange: { min: Math.min(...zs), max: Math.max(...zs) },
  };
}

const server = http.createServer(async (req, res) => {
  const urlPath = (req.url || '/').split('?')[0];

  if (req.method === 'POST' && urlPath === '/api/command') {
    try {
      const body = await readJsonBody(req);
      const result = await handleCommand(body);
      broadcastStatus();
      sendJson(res, result.ok ? 200 : 400, result);
    } catch {
      sendJson(res, 400, { ok: false, error: 'Invalid JSON body' });
    }
    return;
  }

  if (req.method === 'POST' && urlPath === '/api/program/upload') {
    try {
      const body = await readJsonBody(req);
      const validation = validateTestProgram(body);

      if (!validation.ok) {
        // Invalid upload must not overwrite store.
        sendJson(res, 400, { ok: false, errors: validation.errors });
        return;
      }

      programStore.setProgram(validation.value);
      selectionStore.clear();
      sendJson(res, 200, { ok: true, summary: buildSummaryFromProgram(validation.value) });
      return;
    } catch {
      sendJson(res, 400, { ok: false, errors: [{ path: '', message: 'Invalid JSON body' }] });
      return;
    }
  }

  if (req.method === 'GET' && urlPath === '/api/program/current') {
    const current = programStore.getProgram();
    if (!current) {
      sendJson(res, 404, { ok: false, error: 'No program loaded' });
      return;
    }

    sendJson(res, 200, {
      ok: true,
      current: {
        meta: {
          programName: current.programName ?? null,
          version: current.version,
        },
        testPoints: current.testPoints,
      },
    });
    return;
  }

  if (req.method === 'DELETE' && urlPath === '/api/program/current') {
    const current = programStore.getProgram();
    if (!current) {
      sendJson(res, 404, { ok: false, error: 'No program loaded' });
      return;
    }

    const proc = procedureService.getStatus();
    if (proc.state === 'running' || proc.state === 'paused' || proc.state === 'unavailable') {
      sendJson(res, 409, {
        ok: false,
        error: 'Cannot delete program while procedure is running, paused, or unavailable',
      });
      return;
    }

    programStore.clearProgram();
    selectionStore.clear();
    if (['idle', 'completed', 'stopped', 'error'].includes(proc.state)) {
      procedureStore.reset();
    }

    sendJson(res, 200, { ok: true });
    return;
  }

  if (req.method === 'GET' && urlPath === '/api/program/selection') {
    const selection = selectionStore.getSelection();
    if (!selection) {
      sendJson(res, 404, { ok: false, error: 'No program loaded' });
      return;
    }
    sendJson(res, 200, { ok: true, selection });
    return;
  }

  if (req.method === 'PUT' && urlPath === '/api/program/selection') {
    try {
      const body = await readJsonBody(req);
      const result = selectionStore.setDraft(body);
      sendJson(res, result.ok ? 200 : 400, result);
    } catch {
      sendJson(res, 400, { ok: false, error: 'Invalid JSON body' });
    }
    return;
  }

  if (req.method === 'POST' && urlPath === '/api/program/selection/confirm') {
    const result = selectionStore.confirmSelection();
    sendJson(res, result.ok ? 200 : 400, result);
    return;
  }

  if (req.method === 'DELETE' && urlPath === '/api/program/selection') {
    selectionStore.clear();
    const selection = selectionStore.getSelection();
    if (!selection) {
      sendJson(res, 404, { ok: false, error: 'No program loaded' });
      return;
    }
    sendJson(res, 200, { ok: true, selection });
    return;
  }

  if (req.method === 'GET' && urlPath === '/api/machine') {
    const status = machineClient.getStatus();
    sendJson(res, 200, {
      ok: true,
      machineClientMode: machineClient.machineClientMode || process.env.MACHINE_CLIENT || 'mock',
      implementation: machineClient.implementationName || 'Unknown',
      hasTick: typeof machineClient.tick === 'function',
      pid: process.pid,
      port: PORT,
      envMachineClient: process.env.MACHINE_CLIENT || null,
      envProtocolPort: process.env.PROTOCOL_PORT || null,
      status,
    });
    return;
  }

  if (req.method === 'GET' && urlPath === '/api/procedure/status') {
    sendJson(res, 200, { ok: true, status: procedureService.getStatus() });
    return;
  }

  if (req.method === 'POST' && urlPath === '/api/procedure/start') {
    try {
      const body = await readJsonBody(req);
      const result = await procedureService.start(body);
      broadcastStatus();
      sendJson(res, result.ok ? 200 : 400, result);
    } catch {
      sendJson(res, 400, { ok: false, error: 'Invalid JSON body' });
    }
    return;
  }

  if (req.method === 'POST' && urlPath === '/api/procedure/pause') {
    const result = await procedureService.pause();
    broadcastStatus();
    sendJson(res, result.ok ? 200 : 400, result);
    return;
  }

  if (req.method === 'POST' && urlPath === '/api/procedure/resume') {
    const result = await procedureService.resume();
    broadcastStatus();
    sendJson(res, result.ok ? 200 : 400, result);
    return;
  }

  if (req.method === 'POST' && urlPath === '/api/procedure/stop') {
    const result = await procedureService.stop();
    broadcastStatus();
    sendJson(res, result.ok ? 200 : 400, result);
    return;
  }

  if (req.method === 'GET' && urlPath === '/api/results/current') {
    const current = resultsStore.getResults();
    if (!current) {
      sendJson(res, 404, { ok: false, error: 'No results available' });
      return;
    }
    sendJson(res, 200, { ok: true, results: current });
    return;
  }

  if (req.method === 'POST' && urlPath === '/api/parameters/unlock') {
    try {
      const body = await readJsonBody(req);
      if (String(body.password || '') !== PARAMETER_MENU_PASSWORD) {
        sendJson(res, 401, { ok: false, error: 'Ungültiges Passwort' });
        return;
      }
      parametersUnlocked = true;
      sendJson(res, 200, { ok: true, unlocked: true });
    } catch {
      sendJson(res, 400, { ok: false, error: 'Invalid JSON body' });
    }
    return;
  }

  if (req.method === 'GET' && urlPath === '/api/parameters/catalog') {
    sendJson(res, 200, { ok: true, unlocked: parametersUnlocked, groups: parameterGroups });
    return;
  }

  if (req.method === 'GET' && urlPath === '/api/machine-config') {
    sendJson(res, 200, { ok: true, unlocked: parametersUnlocked, config: machineConfigStore.load() });
    return;
  }

  if (req.method === 'PUT' && urlPath === '/api/machine-config') {
    if (!parametersUnlocked) {
      sendJson(res, 403, { ok: false, error: 'Parametereinstellungen sind gesperrt' });
      return;
    }
    try {
      const body = await readJsonBody(req);
      const incoming = { ...machineConfigStore.load(), ...(body.config || body) };
      const check = validateMachineConfigPayload(incoming);
      if (!check.ok) {
        sendJson(res, 400, { ok: false, error: check.error });
        return;
      }
      if (typeof machineClient.setMachineConfig === 'function' && machineClient.getStatus().connected) {
        const result = await machineClient.setMachineConfig(incoming);
        if (!result.ok) {
          sendJson(res, 400, {
            ok: false,
            error: result.error || 'Controller hat die Konfiguration abgelehnt (bisheriger Wert bleibt).',
          });
          return;
        }
      }
      const saved = machineConfigStore.save(incoming);
      sendJson(res, 200, { ok: true, config: saved });
    } catch {
      sendJson(res, 400, { ok: false, error: 'Invalid JSON body' });
    }
    return;
  }

  if (req.method === 'GET' && urlPath === '/api/calibration') {
    sendJson(res, 200, { ok: true, calibration: calibrationStore.load() });
    return;
  }

  if (req.method === 'POST' && urlPath === '/api/calibration/plate-origin') {
    try {
      const body = await readJsonBody(req);
      const status = machineClient.getStatus();
      const x = Number(body.x !== undefined ? body.x : status.x);
      const y = Number(body.y !== undefined ? body.y : status.y);
      const z = Number(body.z !== undefined ? body.z : status.z);
      if (![x, y, z].every(Number.isFinite)) {
        sendJson(res, 400, { ok: false, error: 'x, y, z must be finite' });
        return;
      }
      if (typeof machineClient.setPlateOrigin === 'function' && status.connected) {
        const result = await machineClient.setPlateOrigin(x, y, z);
        if (!result.ok) {
          sendJson(res, 400, { ok: false, error: result.error || 'CmdSetPlateOrigin failed' });
          return;
        }
      }
      sendJson(res, 200, { ok: true, calibration: calibrationStore.setPlateOrigin({ x, y, z }) });
    } catch {
      sendJson(res, 400, { ok: false, error: 'Invalid JSON body' });
    }
    return;
  }

  if (req.method === 'POST' && urlPath === '/api/calibration/probe-start') {
    if (typeof machineClient.startProbeCalibration !== 'function') {
      sendJson(res, 400, { ok: false, error: 'Probe-Kalibrierung ist auf diesem Backend nicht verfügbar' });
      return;
    }
    const result = await machineClient.startProbeCalibration();
    sendJson(res, result.ok ? 200 : 400, result);
    return;
  }

  if (req.method === 'POST' && urlPath === '/api/calibration/sync') {
    if (typeof machineClient.getMachineConfig !== 'function') {
      sendJson(res, 400, { ok: false, error: 'Kalibrierungssync ist auf diesem Backend nicht verfügbar' });
      return;
    }
    const status = machineClient.getStatus();
    if (!status.connected) {
      sendJson(res, 400, { ok: false, error: 'Controller nicht verbunden' });
      return;
    }
    const cfgResult = await machineClient.getMachineConfig();
    if (!cfgResult.ok || !cfgResult.config) {
      sendJson(res, 400, { ok: false, error: cfgResult.error || 'CmdGetMachineConfig failed' });
      return;
    }
    const c = cfgResult.config;
    const storedCfg = machineConfigStore.load();
    machineConfigStore.save({
      ...storedCfg,
      tool_offset_x: c.tool_offset_x,
      tool_offset_y: c.tool_offset_y,
      tool_offset_z: c.tool_offset_z,
    });
    let calib = calibrationStore.load();
    if (status.probeCenterValid) {
      calib = calibrationStore.setProbeCenter(
        {
          x: c.probe_offset_reference_x - c.tool_offset_x,
          y: c.probe_offset_reference_y - c.tool_offset_y,
          z: c.probe_offset_reference_z - c.tool_offset_z,
        },
        { x: c.tool_offset_x, y: c.tool_offset_y, z: c.tool_offset_z }
      );
    }
    sendJson(res, 200, { ok: true, calibration: calib, config: machineConfigStore.load() });
    return;
  }

  if (req.method === 'GET' && urlPath === '/api/results/export') {
    const current = resultsStore.getResults();
    if (!current) {
      sendJson(res, 404, { ok: false, error: 'No results available' });
      return;
    }

    const header = [
      'sequenceNumber',
      'name',
      'iteration',
      'x',
      'y',
      'z',
      'fx',
      'fy',
      'fz',
      'pass',
      'source',
    ];
    const lines = [header.join(',')];
    for (const row of current.rows || []) {
      lines.push(
        [
          row.sequenceNumber,
          JSON.stringify(row.name ?? ''),
          row.iteration ?? '',
          row.x ?? '',
          row.y ?? '',
          row.z ?? '',
          row.fx ?? '',
          row.fy ?? '',
          row.fz ?? '',
          row.pass === null || row.pass === undefined ? '' : row.pass,
          row.source ?? '',
        ].join(',')
      );
    }
    const csv = `${lines.join('\n')}\n`;
    res.writeHead(200, {
      'Content-Type': 'text/csv; charset=utf-8',
      'Content-Disposition': 'attachment; filename="results-stub.csv"',
      'Content-Length': Buffer.byteLength(csv),
    });
    res.end(csv);
    return;
  }

  // M15 simulation plant proxy: virtual sensors only, no machine commands.
  if (req.method === 'GET' && urlPath === '/api/simulation/state') {
    sendPlantResult(res, await plantClient.getState());
    return;
  }

  if (req.method === 'POST' && urlPath === '/api/simulation/limits') {
    try {
      const body = await readJsonBody(req);
      const limits = {};
      for (const key of ['xMin', 'xMax', 'yMin', 'yMax', 'zMin', 'zMax']) {
        if (body[key] !== undefined) {
          const value = readFiniteNumber(body[key]);
          if (value === null) {
            sendJson(res, 400, { ok: false, error: `Invalid ${key}` });
            return;
          }
          limits[key] = value;
        }
      }
      if (Object.keys(limits).length === 0) {
        sendJson(res, 400, { ok: false, error: 'No limit values provided' });
        return;
      }
      sendPlantResult(res, await plantClient.setLimits(limits));
    } catch {
      sendJson(res, 400, { ok: false, error: 'Invalid JSON body' });
    }
    return;
  }

  if (req.method === 'POST' && urlPath === '/api/simulation/probe') {
    try {
      const body = await readJsonBody(req);
      let pulseMs;
      if (body.pulseMs !== undefined) {
        pulseMs = readFiniteNumber(body.pulseMs);
        if (pulseMs === null || pulseMs <= 0) {
          sendJson(res, 400, { ok: false, error: 'Invalid pulseMs' });
          return;
        }
      }
      sendPlantResult(res, await plantClient.triggerProbe(pulseMs));
    } catch {
      sendJson(res, 400, { ok: false, error: 'Invalid JSON body' });
    }
    return;
  }

  if (req.method === 'POST' && urlPath === '/api/simulation/probe-disc') {
    try {
      const body = await readJsonBody(req);
      const disc = plantProbeDiscCenterOnly(body);
      if (Object.keys(disc).length === 0) {
        sendJson(res, 400, {
          ok: false,
          error: 'No probe disc position provided (x/y/z). Physical geometry is MachineConfig-only.',
        });
        return;
      }
      sendPlantResult(res, await plantClient.setProbeDisc(disc));
    } catch {
      sendJson(res, 400, { ok: false, error: 'Invalid JSON body' });
    }
    return;
  }

  if (req.method === 'POST' && urlPath === '/api/simulation/estop') {
    try {
      const body = await readJsonBody(req);
      if (typeof body.active !== 'boolean') {
        sendJson(res, 400, { ok: false, error: 'Missing boolean "active"' });
        return;
      }
      sendPlantResult(res, await plantClient.setEStop(body.active));
    } catch {
      sendJson(res, 400, { ok: false, error: 'Invalid JSON body' });
    }
    return;
  }

  if (req.method === 'POST' && urlPath === '/api/simulation/force-stiffness') {
    try {
      const body = await readJsonBody(req);
      const nPerMm = Number(body.nPerMm);
      if (!Number.isFinite(nPerMm) || nPerMm < 0) {
        sendJson(res, 400, { ok: false, error: 'nPerMm must be a non-negative number' });
        return;
      }
      sendPlantResult(res, await plantClient.setForceStiffness(nPerMm));
    } catch {
      sendJson(res, 400, { ok: false, error: 'Invalid JSON body' });
    }
    return;
  }

  if (req.method !== 'GET' && req.method !== 'HEAD') {
    res.writeHead(405, { 'Content-Type': 'text/plain; charset=utf-8' });
    res.end('Method Not Allowed');
    return;
  }

  const filePath = resolvePublicPath(req.url || '/');
  if (!filePath) {
    res.writeHead(403, { 'Content-Type': 'text/plain; charset=utf-8' });
    res.end('Forbidden');
    return;
  }

  fs.readFile(filePath, (err, data) => {
    if (err) {
      res.writeHead(err.code === 'ENOENT' ? 404 : 500, {
        'Content-Type': 'text/plain; charset=utf-8',
      });
      res.end(err.code === 'ENOENT' ? 'Not Found' : 'Internal Server Error');
      return;
    }

    res.writeHead(200, { 'Content-Type': contentTypeFor(filePath) });
    if (req.method === 'HEAD') {
      res.end();
      return;
    }
    res.end(data);
  });
});

const wss = new WebSocketServer({ server, path: '/ws/status' });

function buildStatusMessage() {
  const status = machineClient.getStatus();
  const mode = machineClient.machineClientMode || process.env.MACHINE_CLIENT || 'mock';
  const implementation = machineClient.implementationName || 'Unknown';
  return JSON.stringify({
    type: 'status',
    ...status,
    machineClientMode: mode,
    implementation,
    hasTick: typeof machineClient.tick === 'function',
  });
}

function broadcastStatus() {
  const payload = buildStatusMessage();
  for (const client of wss.clients) {
    if (client.readyState === 1) {
      client.send(payload);
    }
  }
}

wss.on('connection', (socket) => {
  socket.send(buildStatusMessage());
});

setInterval(() => {
  if (typeof machineClient.tick === 'function') {
    machineClient.tick(TICK_INTERVAL_MS / 1000);
  }
}, TICK_INTERVAL_MS);

setInterval(broadcastStatus, STATUS_INTERVAL_MS);

server.on('error', (err) => {
  if (err && err.code === 'EADDRINUSE') {
    console.error(`[GUI] ERROR: Port ${PORT} is already in use.`);
    console.error('Another GUI instance may already be running.');
    console.error('Consider:');
    console.error('    npm run dev:stop');
    process.exit(1);
  }
  throw err;
});

server.listen(PORT, () => {
  const clientKind = machineClient.machineClientMode || (process.env.MACHINE_CLIENT || 'mock').toLowerCase();
  const implementation = machineClient.implementationName || 'Unknown';
  console.log(`[MACHINE] client mode = ${clientKind}`);
  console.log(`[MACHINE] implementation = ${implementation}`);
  console.log(`[MACHINE] pid = ${process.pid}`);
  console.log(`[MACHINE] hasTick = ${typeof machineClient.tick === 'function'}`);
  console.log(`GUI server listening on http://localhost:${PORT}`);
  console.log(`Status WebSocket: ws://localhost:${PORT}/ws/status (~${STATUS_HZ} Hz)`);
  console.log('Command API: POST /api/command');
  console.log('Machine debug: GET /api/machine');
  console.log(`Simulation UI: http://localhost:${PORT}/simulation`);
  console.log(`Simulation plant channel: ${plantClient.host}:${plantClient.port} (not M12)`);
});
