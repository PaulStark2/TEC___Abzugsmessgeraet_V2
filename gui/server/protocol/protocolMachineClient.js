'use strict';

/**
 * ProtocolMachineClient — TCP client for M13 simulation_protocol (M12 binary).
 *
 * Transport/mapping only. No motion, safety, or process logic in Node.js.
 * Mutating commands require a live TCP session that has received at least one
 * MSG_STATUS_REPORT after connect (sessionReady).
 */

const net = require('net');
const { FrameStreamBuffer } = require('./frameStreamBuffer');
const {
  encodeEmptyMessage,
  encodeMoveToMessage,
  encodeBoolMessage,
  encodeLoadProgramMessage,
  encodeMachineConfigMessage,
  encodeTestConfigurationMessage,
  encodePlateOriginMessage,
  decodeAckPayload,
  decodeNackPayload,
  decodeStatusReportPayload,
  decodeMachineConfigPayload,
  decodeForcePathChunkPayload,
  statusReportToGuiStatus,
} = require('./m12Codec');
const { MessageId, AckResultCode, NackErrorCode, DecodeResult } = require('./protocolTypes');

const DEFAULT_JOG_STEP = 1;
const RECONNECT_DELAY_MS = 2000;
const MACHINE_UNAVAILABLE = 'Machine connection unavailable';

function createDefaultStatus(connected = false) {
  return {
    machineState: 'Idle',
    motionState: 'Idle',
    processState: 'Idle',
    x: 0,
    y: 0,
    z: 0,
    fx: 0,
    fy: 0,
    fz: 0,
    limits: 0,
    eStop: false,
    spreaderExtend: false,
    spreaderExtended: false,
    fixtureExtend: false,
    fixtureExtended: false,
    pneumaticExtended: false,
    currentPointIndex: null,
    programPointCount: 0,
    connected,
  };
}

function ackErrorMessage(resultCode) {
  switch (resultCode) {
    case AckResultCode.Ok:
      return null;
    case AckResultCode.Rejected:
      return 'Command rejected by Core';
    case AckResultCode.InvalidState:
      return 'Invalid state for command';
    case AckResultCode.ProgramEmpty:
      return 'Program empty';
    case AckResultCode.FaultLatched:
      return 'Machine in fault; clear fault first';
    default:
      return `Protocol ACK error (code ${resultCode})`;
  }
}

function nackErrorMessage(errorCode) {
  switch (errorCode) {
    case NackErrorCode.InvalidPayload:
      return 'Invalid protocol payload';
    case NackErrorCode.Unsupported:
      return 'Unsupported command';
    case NackErrorCode.InvalidState:
      return 'Invalid state for command';
    case NackErrorCode.Unknown:
      return 'Unknown protocol error';
    default:
      return `Protocol error (code ${errorCode})`;
  }
}

function createProtocolMachineClient(options = {}) {
  const host = options.host || process.env.PROTOCOL_HOST || '127.0.0.1';
  const port = Number(options.port || process.env.PROTOCOL_PORT || 8766);
  const ackTimeoutMs = Number(options.ackTimeoutMs || process.env.PROTOCOL_ACK_TIMEOUT_MS || 3000);

  let status = createDefaultStatus(false);
  let socket = null;
  let connected = false;
  /** True only after TCP connect AND at least one valid status report in this session. */
  let sessionReady = false;
  let closed = false;
  let reconnectTimer = null;
  const streamBuffer = new FrameStreamBuffer();

  let lastMachineConfig = null;
  let sessionReadyHandler = null;
  let forcePathHandler = null;
  /** @type {Map<number, { resolve: Function, timer: NodeJS.Timeout }>} */
  const pendingByMessageId = new Map();

  function getStatus() {
    return { ...status, connected: connected && sessionReady };
  }

  function rejectPending(errorMessage) {
    for (const [, pending] of pendingByMessageId) {
      clearTimeout(pending.timer);
      pending.resolve({ ok: false, error: errorMessage, status: getStatus() });
    }
    pendingByMessageId.clear();
  }

  function markDisconnected(reason) {
    const wasLive = connected || sessionReady;
    connected = false;
    sessionReady = false;
    status = { ...status, connected: false };
    streamBuffer.clear();
    rejectPending(reason || MACHINE_UNAVAILABLE);
    return wasLive;
  }

  function isLiveSession() {
    return (
      Boolean(socket) &&
      !socket.destroyed &&
      connected &&
      sessionReady &&
      socket.writable
    );
  }

  function applyStatusReport(payload) {
    const decoded = decodeStatusReportPayload(payload);
    if (!decoded) {
      return;
    }
    // First valid status after TCP connect arms the session for mutating commands.
    const becameReady = connected && !sessionReady;
    if (connected) {
      sessionReady = true;
    }
    status = statusReportToGuiStatus(decoded, connected && sessionReady);
    if (becameReady && typeof sessionReadyHandler === 'function') {
      Promise.resolve()
        .then(() => sessionReadyHandler())
        .catch((err) => console.error(`onSessionReady: ${err.message || err}`));
    }
  }

  function handleInboundFrame(frame, decodeResult) {
    if (decodeResult !== DecodeResult.Ok || !frame) {
      return;
    }

    if (frame.messageId === MessageId.MsgStatusReport) {
      applyStatusReport(frame.payload);
      return;
    }

    if (frame.messageId === MessageId.MsgMachineConfig) {
      lastMachineConfig = decodeMachineConfigPayload(frame.payload);
      return;
    }

    if (frame.messageId === MessageId.MsgForcePathChunk) {
      const chunk = decodeForcePathChunkPayload(frame.payload);
      if (chunk && typeof forcePathHandler === 'function') {
        forcePathHandler(chunk);
      }
      return;
    }

    if (frame.messageId === MessageId.RspAck) {
      const ack = decodeAckPayload(frame.payload);
      if (!ack) {
        return;
      }
      const pending = pendingByMessageId.get(ack.refMessageId);
      if (!pending) {
        return;
      }
      clearTimeout(pending.timer);
      pendingByMessageId.delete(ack.refMessageId);
      const error = ackErrorMessage(ack.resultCode);
      if (error) {
        pending.resolve({ ok: false, error, status: getStatus() });
      } else {
        pending.resolve({ ok: true, status: getStatus() });
      }
      return;
    }

    if (frame.messageId === MessageId.RspNack) {
      const nack = decodeNackPayload(frame.payload);
      if (!nack) {
        return;
      }
      const pending = pendingByMessageId.get(nack.refMessageId);
      if (!pending) {
        return;
      }
      clearTimeout(pending.timer);
      pendingByMessageId.delete(nack.refMessageId);
      pending.resolve({
        ok: false,
        error: nackErrorMessage(nack.errorCode),
        status: getStatus(),
      });
    }
  }

  function drainStreamBuffer() {
    for (;;) {
      const popped = streamBuffer.tryPopFrame();
      if (!popped.ok) {
        break;
      }
      handleInboundFrame(popped.frame, popped.decodeResult);
    }
  }

  function sendFrameAndWait(messageId, frame) {
    if (!isLiveSession()) {
      return Promise.resolve({
        ok: false,
        error: MACHINE_UNAVAILABLE,
        status: getStatus(),
      });
    }
    if (pendingByMessageId.has(messageId)) {
      return Promise.resolve({
        ok: false,
        error: 'Command already pending for this message id',
        status: getStatus(),
      });
    }

    const activeSocket = socket;

    return new Promise((resolve) => {
      const timer = setTimeout(() => {
        pendingByMessageId.delete(messageId);
        // Half-open TCP (host killed, no close yet): fail closed and force reconnect.
        markDisconnected(MACHINE_UNAVAILABLE);
        if (!closed) {
          scheduleReconnect();
        }
        resolve({ ok: false, error: MACHINE_UNAVAILABLE, status: getStatus() });
      }, ackTimeoutMs);

      pendingByMessageId.set(messageId, { resolve, timer });

      try {
        activeSocket.write(frame, (writeErr) => {
          if (!writeErr) {
            return;
          }
          const pending = pendingByMessageId.get(messageId);
          if (pending) {
            clearTimeout(pending.timer);
            pendingByMessageId.delete(messageId);
            markDisconnected(MACHINE_UNAVAILABLE);
            pending.resolve({
              ok: false,
              error: MACHINE_UNAVAILABLE,
              status: getStatus(),
            });
          } else {
            markDisconnected(MACHINE_UNAVAILABLE);
          }
          if (!closed) {
            scheduleReconnect();
          }
        });
      } catch (err) {
        const pending = pendingByMessageId.get(messageId);
        if (pending) {
          clearTimeout(pending.timer);
          pendingByMessageId.delete(messageId);
        }
        markDisconnected(MACHINE_UNAVAILABLE);
        resolve({
          ok: false,
          error: MACHINE_UNAVAILABLE,
          status: getStatus(),
        });
        if (!closed) {
          scheduleReconnect();
        }
      }
    });
  }

  function scheduleReconnect() {
    if (closed || reconnectTimer) {
      return;
    }
    reconnectTimer = setTimeout(() => {
      reconnectTimer = null;
      connect();
    }, RECONNECT_DELAY_MS);
  }

  function attachSocketHandlers(s) {
    s.on('data', (chunk) => {
      if (s !== socket) {
        return;
      }
      streamBuffer.append(chunk);
      if (streamBuffer.overflowed()) {
        console.error('ProtocolMachineClient: stream buffer overflow; clearing');
        streamBuffer.clear();
        return;
      }
      drainStreamBuffer();
    });

    s.on('error', (err) => {
      console.error(`ProtocolMachineClient error: ${err.message}`);
      if (s === socket) {
        markDisconnected(MACHINE_UNAVAILABLE);
      }
    });

    s.on('end', () => {
      if (s === socket) {
        markDisconnected(MACHINE_UNAVAILABLE);
      }
    });

    s.on('close', () => {
      if (s !== socket && socket !== null) {
        return;
      }
      socket = null;
      const wasLive = markDisconnected(MACHINE_UNAVAILABLE);
      if (!closed) {
        if (wasLive) {
          console.log('ProtocolMachineClient disconnected; reconnecting…');
        }
        scheduleReconnect();
      }
    });
  }

  function connect() {
    if (closed) {
      return;
    }
    if (socket) {
      const old = socket;
      socket = null;
      try {
        old.destroy();
      } catch {
        // ignore
      }
    }

    connected = false;
    sessionReady = false;
    status = { ...status, connected: false };
    streamBuffer.clear();

    const s = net.createConnection({ host, port }, () => {
      if (s !== socket) {
        return;
      }
      // TCP is up, but mutating commands wait until first status report.
      connected = true;
      sessionReady = false;
      status = { ...status, connected: false };
      console.log(`ProtocolMachineClient connected to ${host}:${port}`);
    });

    socket = s;
    attachSocketHandlers(s);
  }

  async function moveTo(x, y, z) {
    const nx = Number(x);
    const ny = Number(y);
    const nz = Number(z);
    if (![nx, ny, nz].every(Number.isFinite)) {
      return { ok: false, error: 'moveTo requires finite x, y, z' };
    }
    if (!isLiveSession()) {
      return { ok: false, error: MACHINE_UNAVAILABLE, status: getStatus() };
    }
    const encoded = encodeMoveToMessage(nx, ny, nz);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode MOVE_TO' };
    }
    return sendFrameAndWait(MessageId.CmdMoveTo, encoded.frame);
  }

  async function pause() {
    if (!isLiveSession()) {
      return { ok: false, error: MACHINE_UNAVAILABLE, status: getStatus() };
    }
    const encoded = encodeEmptyMessage(MessageId.CmdPause);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode PAUSE' };
    }
    return sendFrameAndWait(MessageId.CmdPause, encoded.frame);
  }

  async function resume() {
    if (!isLiveSession()) {
      return { ok: false, error: MACHINE_UNAVAILABLE, status: getStatus() };
    }
    const encoded = encodeEmptyMessage(MessageId.CmdResume);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode RESUME' };
    }
    return sendFrameAndWait(MessageId.CmdResume, encoded.frame);
  }

  async function stop() {
    if (!isLiveSession()) {
      return { ok: false, error: MACHINE_UNAVAILABLE, status: getStatus() };
    }
    const encoded = encodeEmptyMessage(MessageId.CmdStop);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode STOP' };
    }
    return sendFrameAndWait(MessageId.CmdStop, encoded.frame);
  }

  async function jog(axis, direction, step) {
    if (!isLiveSession()) {
      return { ok: false, error: MACHINE_UNAVAILABLE, status: getStatus() };
    }
    const axisKey = String(axis || '').toUpperCase();
    if (!['X', 'Y', 'Z'].includes(axisKey)) {
      return { ok: false, error: 'jog axis must be X, Y, or Z' };
    }
    const dir = Number(direction);
    if (dir !== 1 && dir !== -1) {
      return { ok: false, error: 'jog direction must be 1 or -1' };
    }
    const stepSize = step === undefined ? DEFAULT_JOG_STEP : Number(step);
    if (!Number.isFinite(stepSize) || stepSize <= 0) {
      return { ok: false, error: 'jog step must be a positive finite number' };
    }

    const current = getStatus();
    const delta = dir * stepSize;
    const target = { x: current.x, y: current.y, z: current.z };
    if (axisKey === 'X') target.x += delta;
    if (axisKey === 'Y') target.y += delta;
    if (axisKey === 'Z') target.z += delta;
    return moveTo(target.x, target.y, target.z);
  }

  async function pneumaticOpen() {
    if (!isLiveSession()) {
      return { ok: false, error: MACHINE_UNAVAILABLE, status: getStatus() };
    }
    const encoded = encodeBoolMessage(MessageId.CmdSetSpreader, true);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode SET_SPREADER' };
    }
    return sendFrameAndWait(MessageId.CmdSetSpreader, encoded.frame);
  }

  async function pneumaticClose() {
    if (!isLiveSession()) {
      return { ok: false, error: MACHINE_UNAVAILABLE, status: getStatus() };
    }
    const encoded = encodeBoolMessage(MessageId.CmdSetSpreader, false);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode SET_SPREADER' };
    }
    return sendFrameAndWait(MessageId.CmdSetSpreader, encoded.frame);
  }

  async function clearFault() {
    if (!isLiveSession()) {
      return { ok: false, error: MACHINE_UNAVAILABLE, status: getStatus() };
    }
    const encoded = encodeEmptyMessage(MessageId.CmdClearFault);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode CLEAR_FAULT' };
    }
    return sendFrameAndWait(MessageId.CmdClearFault, encoded.frame);
  }

  /** Core homing sequence Z+ → X+ → Y+ (CmdHome). */
  async function home() {
    if (!isLiveSession()) {
      return { ok: false, error: MACHINE_UNAVAILABLE, status: getStatus() };
    }
    const encoded = encodeEmptyMessage(MessageId.CmdHome);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode HOME' };
    }
    return sendFrameAndWait(MessageId.CmdHome, encoded.frame);
  }

  async function setSpeed() {
    return { ok: false, error: 'Not supported over protocol' };
  }

  /**
   * Load a point program into Core TestExecutor via CMD_LOAD_PROGRAM.
   * Transport only — no GUI sequencing of points.
   * @param {Array<{x:number,y:number,z:number}>} points
   */
  async function loadProgram(points) {
    if (!isLiveSession()) {
      return { ok: false, error: MACHINE_UNAVAILABLE, status: getStatus() };
    }
    if (!Array.isArray(points) || points.length === 0) {
      return { ok: false, error: 'loadProgram requires a non-empty points array' };
    }
    const encoded = encodeLoadProgramMessage(points);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode LOAD_PROGRAM' };
    }
    return sendFrameAndWait(MessageId.CmdLoadProgram, encoded.frame);
  }

  /**
   * Start Core TestExecutor via CMD_START.
   */
  async function start() {
    if (!isLiveSession()) {
      return { ok: false, error: MACHINE_UNAVAILABLE, status: getStatus() };
    }
    const encoded = encodeEmptyMessage(MessageId.CmdStart);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode START' };
    }
    return sendFrameAndWait(MessageId.CmdStart, encoded.frame);
  }

  async function setMachineConfig(cfg) {
    if (!isLiveSession()) {
      return { ok: false, error: MACHINE_UNAVAILABLE, status: getStatus() };
    }
    const encoded = encodeMachineConfigMessage(cfg);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode SET_MACHINE_CONFIG' };
    }
    return sendFrameAndWait(MessageId.CmdSetMachineConfig, encoded.frame);
  }

  async function getMachineConfig() {
    if (!isLiveSession()) {
      return { ok: false, error: MACHINE_UNAVAILABLE, status: getStatus() };
    }
    const encoded = encodeEmptyMessage(MessageId.CmdGetMachineConfig);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode GET_MACHINE_CONFIG' };
    }
    const result = await sendFrameAndWait(MessageId.CmdGetMachineConfig, encoded.frame);
    if (!result.ok) {
      return result;
    }
    return { ok: true, config: lastMachineConfig, status: getStatus() };
  }

  async function setTestConfiguration(cfg) {
    if (!isLiveSession()) {
      return { ok: false, error: MACHINE_UNAVAILABLE, status: getStatus() };
    }
    const encoded = encodeTestConfigurationMessage(cfg);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode SET_TEST_CONFIGURATION' };
    }
    return sendFrameAndWait(MessageId.CmdSetTestConfiguration, encoded.frame);
  }

  async function setPlateOrigin(x, y, z) {
    if (!isLiveSession()) {
      return { ok: false, error: MACHINE_UNAVAILABLE, status: getStatus() };
    }
    const encoded = encodePlateOriginMessage(x, y, z);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode SET_PLATE_ORIGIN' };
    }
    return sendFrameAndWait(MessageId.CmdSetPlateOrigin, encoded.frame);
  }

  async function startProbeCalibration() {
    if (!isLiveSession()) {
      return { ok: false, error: MACHINE_UNAVAILABLE, status: getStatus() };
    }
    const encoded = encodeEmptyMessage(MessageId.CmdStartProbeCalib);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode START_PROBE_CALIB' };
    }
    return sendFrameAndWait(MessageId.CmdStartProbeCalib, encoded.frame);
  }

  function onSessionReady(handler) {
    sessionReadyHandler = handler;
  }

  function onForcePathChunk(handler) {
    forcePathHandler = handler;
  }

  function close() {
    closed = true;
    if (reconnectTimer) {
      clearTimeout(reconnectTimer);
      reconnectTimer = null;
    }
    markDisconnected(MACHINE_UNAVAILABLE);
    if (socket) {
      const s = socket;
      socket = null;
      try {
        s.destroy();
      } catch {
        // ignore
      }
    }
  }

  connect();

  return {
    getStatus,
    moveTo,
    pause,
    resume,
    stop,
    jog,
    pneumaticOpen,
    pneumaticClose,
    home,
    clearFault,
    setSpeed,
    loadProgram,
    start,
    setMachineConfig,
    getMachineConfig,
    setTestConfiguration,
    setPlateOrigin,
    startProbeCalibration,
    onSessionReady,
    onForcePathChunk,
    close,
  };
}

module.exports = {
  createProtocolMachineClient,
  MACHINE_UNAVAILABLE,
};
