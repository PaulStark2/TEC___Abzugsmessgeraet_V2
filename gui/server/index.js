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
const { compareCriticalMachineConfig } = require('./config/machineConfigSync');
const {
  GROUPS: parameterGroups,
  READ_ONLY_KEYS,
  validateMachineConfigPayload,
} = require('./config/parameterCatalog');
const { isJogStepAllowed } = require('./config/jogStep');
const calibrationStore = require('./config/calibrationStore');
const { pushCommittedCalibration } = require('./config/pushCommittedCalibration');
const { MCS_REQUIRED_MESSAGE } = require('./config/mcsMessages');
const forcePathStore = require('./results/forcePathStore');
const { plantProbeDiscCenterOnly } = require('./coord/physicalGeometry');
const simPlantStore = require('./config/simPlantStore');

const PORT = Number(process.env.PORT) || 3000;
const PUBLIC_DIR = path.join(__dirname, '..', 'public');
const THREE_DIR = path.join(__dirname, '..', 'node_modules', 'three');
const STATUS_HZ = 5;
const STATUS_INTERVAL_MS = 1000 / STATUS_HZ;
const TICK_HZ = 20;
const TICK_INTERVAL_MS = 1000 / TICK_HZ;

/** Last failed connect-time MachineConfig push (cleared on success). */
let lastMachineConfigPushError = null;

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

function forceDirectionName(direction) {
  if (direction === 0 || direction === 'tension') return 'tension';
  if (direction === 1 || direction === 'compression') return 'compression';
  return 'zero';
}

function forceZFromReport(body, report) {
  const sourceNames = ['auto', 'linear', 'quadratic', 'external'];
  const source = sourceNames[Number(body.source)] || 'auto';
  return {
    valid: true,
    source,
    points: (body.points || []).map((point, index) => ({
      adcNet: Number(point.adcNet),
      massG: Number(point.massG),
      nominalG: Number(point.nominalG),
      direction: forceDirectionName(point.direction),
      referenceForceN: report.referenceForceN ? report.referenceForceN[index] : null,
    })),
    active: {
      degree: report.degree,
      beta0: report.beta0,
      beta1: report.beta1,
      beta2: report.beta2,
      resolved: report.resolvedSource,
      pearsonR: report.pearsonR,
      rSquared: report.rSquared,
      maxAbsResidualN: report.maxAbsResidualN,
      meanResidualTensionN: report.meanResidualTensionN,
      meanResidualCompressionN: report.meanResidualCompressionN,
      worstIndex: report.worstIndex,
      linear: {
        valid: report.linearValid === 1,
        beta0: report.linearBeta0,
        beta1: report.linearBeta1,
        beta2: report.linearBeta2,
        rSquared: report.linearR2,
        maxAbsResidualN: report.linearMaxAbsN,
      },
      quadratic: {
        valid: report.quadraticValid === 1,
        beta0: report.quadraticBeta0,
        beta1: report.quadraticBeta1,
        beta2: report.quadraticBeta2,
        rSquared: report.quadraticR2,
        maxAbsResidualN: report.quadraticMaxAbsN,
      },
    },
    external: body.external || null,
  };
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
      {
        const diskCfg = machineConfigStore.load();
        const minStep = Number(diskCfg && diskCfg.jog_min_step_mm);
        const step =
          body.step === undefined || body.step === null || body.step === ''
            ? minStep
            : Number(body.step);
        if (!isJogStepAllowed(step, minStep > 0 ? minStep : 0.1)) {
          return {
            ok: false,
            error:
              `Ungültige Jog-Schrittweite ${step} mm ` +
              `(Mindestschritt / Raster: ${minStep > 0 ? minStep : 0.1} mm)`,
          };
        }
        result = await machineClient.jog(body.axis, body.direction, step);
      }
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
    case 'setCollisionGuard':
      if (typeof machineClient.setCollisionGuard !== 'function') {
        return { ok: false, error: 'setCollisionGuard not supported by client' };
      }
      result = await machineClient.setCollisionGuard(body.enabled === true);
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
    case 'forceTare':
      if (typeof machineClient.forceTare !== 'function') {
        return { ok: false, error: 'forceTare not supported by client' };
      }
      result = await machineClient.forceTare();
      break;
    case 'home':
      if (typeof machineClient.home !== 'function') {
        return { ok: false, error: 'home not supported by client' };
      }
      {
        const st = typeof machineClient.getStatus === 'function' ? machineClient.getStatus() : {};
        if (st.mcsValid !== true) {
          return {
            ok: false,
            error: MCS_REQUIRED_MESSAGE,
            code: 'mcs_required',
            status: st,
          };
        }
      }
      result = await machineClient.home();
      break;
    case 'defineMcs':
      if (typeof machineClient.defineMcs !== 'function') {
        return { ok: false, error: 'defineMcs not supported by client' };
      }
      // Do not touch committed calibration.json here — commit is persistMcs only.
      result = await machineClient.defineMcs();
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
const plantClient = createPlantClient();
const procedureService = createProcedureService(machineClient, { plantClient });
// GUI-only parameter-menu unlock. Not part of the STM32 safety circuit.
// Default 'tec-admin' is for development. Production password MUST be changed
// (environment VARIABLE PARAMETER_MENU_PASSWORD).
const PARAMETER_MENU_PASSWORD = process.env.PARAMETER_MENU_PASSWORD || 'tec-admin';
let parametersUnlocked = false;
/** After plant reconnect, push persisted SimUI settings once. */
let plantSettingsRestored = false;

if (typeof machineClient.onForcePathChunk === 'function') {
  machineClient.onForcePathChunk((chunk) => forcePathStore.appendChunk(chunk));
}

if (typeof machineClient.onSessionReady === 'function') {
  machineClient.onSessionReady(async () => {
    const cfg = machineConfigStore.load();
    if (typeof machineClient.setMachineConfig === 'function') {
      const pushed = await machineClient.setMachineConfig(cfg);
      if (!pushed.ok) {
        lastMachineConfigPushError = pushed.error || 'CmdSetMachineConfig failed';
        console.error(`[CONFIG] push on connect failed: ${lastMachineConfigPushError}`);
      } else {
        lastMachineConfigPushError = null;
      }
    }
    const pushedCalib = await pushCommittedCalibration(machineClient);
    if (!pushedCalib.ok) {
      console.error(`[CALIB] commit push on connect failed: ${pushedCalib.error || 'unknown'}`);
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

/**
 * Re-apply last SimUI plant settings after simulation host restart.
 * @returns {Promise<boolean>} true if plant accepted the restore
 */
async function restorePersistedPlantSettings() {
  const saved = simPlantStore.load();
  const limitsResult = await plantClient.setLimits(saved.limits);
  if (!limitsResult || !limitsResult.ok) {
    return false;
  }
  const discResult = await plantClient.setProbeDisc(saved.probeDisc);
  if (!discResult || !discResult.ok) {
    return false;
  }
  const stiffnessResult = await plantClient.setForceStiffness(saved.forceStiffnessNPerMm);
  return Boolean(stiffnessResult && stiffnessResult.ok);
}

function persistPlantSettingsFromState(state) {
  if (!state || typeof state !== 'object') {
    return;
  }
  const patch = {};
  if (state.limits) {
    patch.limits = state.limits;
  }
  if (state.probe) {
    patch.probeDisc = state.probe;
  }
  if (state.forceStiffnessNPerMm !== undefined) {
    patch.forceStiffnessNPerMm = state.forceStiffnessNPerMm;
  }
  if (Object.keys(patch).length) {
    simPlantStore.save(patch);
  }
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
    let body;
    try {
      body = await readJsonBody(req);
    } catch {
      sendJson(res, 400, { ok: false, error: 'Invalid JSON body' });
      return;
    }
    try {
      const result = await handleCommand(body);
      broadcastStatus();
      sendJson(res, result.ok ? 200 : 400, result);
    } catch (err) {
      console.error('[api/command]', err);
      sendJson(res, 500, {
        ok: false,
        error: (err && err.message) || 'Command handler failed',
      });
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
    const diskConfig = machineConfigStore.load();
    const status =
      typeof machineClient.getStatus === 'function' ? machineClient.getStatus() : { connected: false };
    const connected = status.connected === true;
    let coreConfig = null;
    let coreError = null;
    let drift = false;
    let mismatchedKeys = [];
    if (typeof machineClient.getMachineConfig === 'function' && connected) {
      try {
        const coreResult = await machineClient.getMachineConfig();
        if (coreResult && coreResult.ok && coreResult.config) {
          coreConfig = coreResult.config;
          const cmp = compareCriticalMachineConfig(diskConfig, coreConfig);
          drift = cmp.drift;
          mismatchedKeys = cmp.mismatchedKeys;
        } else {
          coreError = (coreResult && coreResult.error) || 'CmdGetMachineConfig failed';
        }
      } catch (err) {
        coreError = err.message || String(err);
      }
    }
    sendJson(res, 200, {
      ok: true,
      unlocked: parametersUnlocked,
      config: diskConfig,
      coreConfig,
      coreConnected: connected,
      coreError,
      configDrift: drift,
      mismatchedKeys,
      lastConnectPushError: lastMachineConfigPushError,
      machineClientMode: machineClient.machineClientMode || process.env.MACHINE_CLIENT || 'mock',
    });
    return;
  }

  if (req.method === 'PUT' && urlPath === '/api/machine-config') {
    if (!parametersUnlocked) {
      sendJson(res, 403, { ok: false, error: 'Parametereinstellungen sind gesperrt' });
      return;
    }
    try {
      const body = await readJsonBody(req);
      const stored = machineConfigStore.load();
      const incoming = { ...stored, ...(body.config || body) };
      // tool_offset_* only via probe calibration sync — ignore menu edits.
      for (const key of READ_ONLY_KEYS) {
        if (Object.prototype.hasOwnProperty.call(stored, key)) {
          incoming[key] = stored[key];
        }
      }
      const check = validateMachineConfigPayload(incoming);
      if (!check.ok) {
        sendJson(res, 400, { ok: false, error: check.error });
        return;
      }

      const canPush = typeof machineClient.setMachineConfig === 'function';
      const connected =
        typeof machineClient.getStatus === 'function' && machineClient.getStatus().connected === true;

      if (canPush && !connected) {
        sendJson(res, 503, {
          ok: false,
          error:
            'Controller nicht verbunden — Konfiguration wurde nicht gespeichert. Bitte verbinden und erneut speichern.',
        });
        return;
      }

      let coreConfig = null;
      let mismatchedKeys = [];
      if (canPush && connected) {
        const result = await machineClient.setMachineConfig(incoming);
        if (!result.ok) {
          sendJson(res, 400, {
            ok: false,
            error: result.error || 'Controller hat die Konfiguration abgelehnt (bisheriger Wert bleibt).',
          });
          return;
        }
        if (typeof machineClient.getMachineConfig === 'function') {
          const roundtrip = await machineClient.getMachineConfig();
          if (roundtrip && roundtrip.ok && roundtrip.config) {
            coreConfig = roundtrip.config;
            const cmp = compareCriticalMachineConfig(incoming, coreConfig);
            mismatchedKeys = cmp.mismatchedKeys;
            if (cmp.drift) {
              sendJson(res, 400, {
                ok: false,
                error: `Controller-Roundtrip weicht ab (${mismatchedKeys.join(', ')}). Disk nicht geschrieben.`,
                coreConfig,
                mismatchedKeys,
              });
              return;
            }
          } else {
            sendJson(res, 400, {
              ok: false,
              error:
                (roundtrip && roundtrip.error) ||
                'Konfiguration gesendet, aber Roundtrip-Lesen fehlgeschlagen. Disk nicht geschrieben.',
            });
            return;
          }
        }
        lastMachineConfigPushError = null;
      }

      const saved = machineConfigStore.save(incoming);
      const curve = calibrationStore.invalidateForceZBeyondRange(saved.force_range_N);
      sendJson(res, 200, {
        ok: true,
        config: saved,
        coreConfig,
        mismatchedKeys,
        pushedToController: Boolean(canPush && connected),
        forceCurveCleared: curve.invalidated,
      });
    } catch {
      sendJson(res, 400, { ok: false, error: 'Invalid JSON body' });
    }
    return;
  }

  if (req.method === 'POST' && urlPath === '/api/machine/invalidate-homing') {
    if (typeof machineClient.invalidateHoming !== 'function') {
      sendJson(res, 400, { ok: false, error: 'Homing-Reset ist auf diesem Backend nicht verfügbar' });
      return;
    }
    const result = await machineClient.invalidateHoming();
    sendJson(res, result.ok ? 200 : 400, {
      ok: Boolean(result.ok),
      error: result.error,
      status: result.status || (typeof machineClient.getStatus === 'function' ? machineClient.getStatus() : null),
    });
    return;
  }

  if (req.method === 'GET' && urlPath === '/api/calibration') {
    sendJson(res, 200, { ok: true, calibration: calibrationStore.load() });
    return;
  }

  if (req.method === 'POST' && urlPath === '/api/calibration/define-mcs') {
    if (typeof machineClient.defineMcs !== 'function') {
      sendJson(res, 400, { ok: false, error: 'MCS-Definition ist auf diesem Backend nicht verfügbar' });
      return;
    }
    // Core Define only — committed calibration.json unchanged until persistMcs.
    const result = await machineClient.defineMcs();
    sendJson(res, result.ok ? 200 : 400, {
      ...result,
      calibration: calibrationStore.load(),
    });
    return;
  }

  if (req.method === 'POST' && urlPath === '/api/calibration/rollback-full') {
    // Abort before commit: re-apply committed store into Core (Disk unchanged).
    const result = await pushCommittedCalibration(machineClient);
    sendJson(res, result.ok ? 200 : 400, {
      ok: result.ok,
      restored: Boolean(result.restored),
      reason: result.reason,
      error: result.error,
      calibration: result.calibration || calibrationStore.load(),
      message: result.restored
        ? 'Committed Kalibrierung wieder in den Core geladen.'
        : result.reason === 'no_committed_mcs'
          ? MCS_REQUIRED_MESSAGE
          : undefined,
    });
    return;
  }

  if (req.method === 'POST' && urlPath === '/api/calibration/mcs') {
    try {
      const body = await readJsonBody(req);
      const status = machineClient.getStatus();
      const x = Number(body.x !== undefined ? body.x : status.measuredHome?.x ?? status.x);
      const y = Number(body.y !== undefined ? body.y : status.measuredHome?.y ?? status.y);
      const z = Number(body.z !== undefined ? body.z : status.measuredHome?.z ?? status.z);
      if (![x, y, z].every(Number.isFinite)) {
        sendJson(res, 400, { ok: false, error: 'x, y, z must be finite' });
        return;
      }
      sendJson(res, 200, {
        ok: true,
        calibration: calibrationStore.setMachineCoordinateSystem({ x, y, z }),
      });
    } catch (err) {
      sendJson(res, 400, { ok: false, error: err.message || 'Invalid JSON body' });
    }
    return;
  }

  if (req.method === 'POST' && urlPath === '/api/calibration/plate-origin') {
    try {
      const body = await readJsonBody(req);
      const calib = calibrationStore.load();
      if (!calib.machineCoordinateSystem || !calib.machineCoordinateSystem.valid) {
        sendJson(res, 400, {
          ok: false,
          error:
            'Kein gültiges Maschinenkoordinatensystem. Bitte zuerst MCS und Bauplatte/Messtaster definieren und kalibrieren.',
        });
        return;
      }
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

  if (req.method === 'POST' && urlPath === '/api/calibration/force') {
    try {
      const body = await readJsonBody(req);
      if (typeof machineClient.setForceCalibration !== 'function') {
        sendJson(res, 400, { ok: false, error: 'Kraftkalibrierung ist auf diesem Backend nicht verfügbar' });
        return;
      }
      const result = await machineClient.setForceCalibration({
        source: Number(body.source) || 0,
        confirm: Boolean(body.confirm),
        commit: Boolean(body.commit),
        points: body.points || [],
      });
      let calibration = calibrationStore.load();
      if (result.report && result.report.committed && body.commit) {
        calibration = calibrationStore.setForceZ(forceZFromReport(body, result.report));
      }
      sendJson(res, result.report ? 200 : 400, {
        ok: Boolean(result.ok),
        error: result.error,
        report: result.report,
        calibration,
      });
    } catch {
      sendJson(res, 400, { ok: false, error: 'Invalid JSON body' });
    }
    return;
  }

  if (req.method === 'POST' && urlPath === '/api/calibration/force-select') {
    try {
      const body = await readJsonBody(req);
      if (typeof machineClient.selectForceCurve !== 'function') {
        sendJson(res, 400, { ok: false, error: 'Kraftkalibrierung ist auf diesem Backend nicht verfügbar' });
        return;
      }
      const result = await machineClient.selectForceCurve({
        source: Number(body.source),
        confirm: Boolean(body.confirm),
        unit: body.unit === 'volt' || body.unit === 1 ? 1 : 0,
        commit: Boolean(body.commit),
        beta0: Number(body.beta0) || 0,
        beta1: Number(body.beta1) || 0,
        beta2: Number(body.beta2) || 0,
      });
      let calibration = calibrationStore.load();
      if (result.report && result.report.committed && body.commit) {
        const previous = calibration.forceZ || {};
        const next = forceZFromReport(
          {
            source: Number(body.source),
            points: previous.points || [],
            external:
              Number(body.source) === 3
                ? {
                    unit: body.unit === 'volt' || body.unit === 1 ? 'volt' : 'adc',
                    beta0: Number(body.beta0) || 0,
                    beta1: Number(body.beta1) || 0,
                    beta2: Number(body.beta2) || 0,
                  }
                : previous.external,
          },
          result.report
        );
        calibration = calibrationStore.setForceZ(next);
      }
      sendJson(res, result.report ? 200 : 400, {
        ok: Boolean(result.ok),
        error: result.error,
        report: result.report,
        calibration,
      });
    } catch {
      sendJson(res, 400, { ok: false, error: 'Invalid JSON body' });
    }
    return;
  }

  if (req.method === 'POST' && urlPath === '/api/calibration/sync') {
    let persistProbeCenter = false;
    try {
      const body = await readJsonBody(req);
      persistProbeCenter = Boolean(body && body.persistProbeCenter === true);
    } catch {
      // empty body → tool-only sync
    }
    const { syncCalibrationFromController } = require('./config/syncCalibrationFromController');
    const result = await syncCalibrationFromController(machineClient, { persistProbeCenter });
    sendJson(res, result.ok ? 200 : 400, result);
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
    let result = await plantClient.getState();
    if (!result || !result.ok) {
      if (result && result.offline) {
        plantSettingsRestored = false;
      }
      sendPlantResult(res, result);
      return;
    }
    if (!plantSettingsRestored) {
      const restored = await restorePersistedPlantSettings();
      if (restored) {
        plantSettingsRestored = true;
        result = await plantClient.getState();
      }
    }
    sendPlantResult(res, result);
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
      const result = await plantClient.setLimits(limits);
      if (result && result.ok) {
        simPlantStore.save({ limits });
        plantSettingsRestored = true;
        persistPlantSettingsFromState(result.state);
      }
      sendPlantResult(res, result);
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
      const result = await plantClient.setProbeDisc(disc);
      if (result && result.ok) {
        simPlantStore.save({ probeDisc: disc });
        plantSettingsRestored = true;
        persistPlantSettingsFromState(result.state);
      }
      sendPlantResult(res, result);
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
      const result = await plantClient.setForceStiffness(nPerMm);
      if (result && result.ok) {
        simPlantStore.save({ forceStiffnessNPerMm: nPerMm });
        plantSettingsRestored = true;
        persistPlantSettingsFromState(result.state);
      }
      sendPlantResult(res, result);
    } catch {
      sendJson(res, 400, { ok: false, error: 'Invalid JSON body' });
    }
    return;
  }

  if (req.method === 'POST' && urlPath === '/api/simulation/injected-force') {
    try {
      const body = await readJsonBody(req);
      const fx = body.fx === undefined ? 0 : Number(body.fx);
      const fy = body.fy === undefined ? 0 : Number(body.fy);
      const fz = body.fz === undefined ? 0 : Number(body.fz);
      const maxAbs = 200;
      if (![fx, fy, fz].every(Number.isFinite)) {
        sendJson(res, 400, { ok: false, error: 'fx/fy/fz must be finite numbers' });
        return;
      }
      if (Math.abs(fx) > maxAbs || Math.abs(fy) > maxAbs || Math.abs(fz) > maxAbs) {
        sendJson(res, 400, { ok: false, error: 'each |F| must be <= 200 N' });
        return;
      }
      sendPlantResult(res, await plantClient.setInjectedForce({ fx, fy, fz }));
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
