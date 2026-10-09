'use strict';

/**
 * ProtocolMachineClient — TCP client for M13 simulation_protocol (M12 binary).
 *
 * Transport/mapping only. No motion, safety, or process logic in Node.js.
 * Mutating commands require a live TCP session that has received at least one
 * MSG_STATUS_REPORT after connect (sessionReady).
 */

const { FrameStreamBuffer } = require('./frameStreamBuffer');
const { createProtocolConnection } = require('./protocolTransport');
const {
  encodeEmptyMessage,
  encodeMoveToMessage,
  encodeBoolMessage,
  encodeLoadProgramMessage,
  encodeMachineConfigMessage,
  encodeTestConfigurationMessage,
  encodePlateOriginMessage,
  encodeProbeCenterMessage,
  encodeRestoreMcsMessage,
  decodeAckPayload,
  decodeNackPayload,
  decodeStatusReportPayload,
  decodeMachineConfigPayload,
  decodeForcePathChunkPayload,
  decodeRawAdcPayload,
  decodeForceCalibrationResultPayload,
  encodeSetForceCalibrationMessage,
  encodeSelectForceCurveMessage,
  statusReportToGuiStatus,
} = require('./m12Codec');
const { MessageId, AckResultCode, NackErrorCode, DecodeResult } = require('./protocolTypes');
const { latchMeasuredHome } = require('../coord/homeMarker');

const DEFAULT_JOG_STEP = 1;
const RECONNECT_DELAY_MS_TCP = 2000;
const RECONNECT_DELAY_MS_UART = 4000; // Windows needs longer after SerialPort.close
const NOP_INTERVAL_MS = 100; // CmdNop 10 Hz while session is live
const MACHINE_UNAVAILABLE = 'Machine connection unavailable';
const STATUS_STALE_MS = 1000; // UART: if status still flows, ACK timeout ≠ link dead

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
    procedurePhase: 'Idle',
    faultReason: 'None',
    spreaderState: 'Retracted',
    communicationWatchdogEnabled: false,
    forceSampleFresh: false,
    plateOriginValid: false,
    probeCenterValid: false,
    mcsValid: false,
    measuredHome: null,
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
  const transport = String(options.transport || process.env.PROTOCOL_TRANSPORT || 'tcp').toLowerCase();
  const isUart = transport === 'uart' || transport === 'serial';
  const serialPath = options.serialPath || process.env.PROTOCOL_SERIAL_PATH || '';
  const baudRate = Number(options.baudRate || process.env.PROTOCOL_SERIAL_BAUD || 115200);
  const ackTimeoutMs = Number(
    options.ackTimeoutMs ||
      process.env.PROTOCOL_ACK_TIMEOUT_MS ||
      (isUart ? 5000 : 3000)
  );
  const reconnectDelayMs = isUart ? RECONNECT_DELAY_MS_UART : RECONNECT_DELAY_MS_TCP;

  let status = createDefaultStatus(false);
  let socket = null;
  let connected = false;
  /** True only after transport connect AND at least one valid status report in this session. */
  let sessionReady = false;
  let closed = false;
  let reconnectTimer = null;
  let nopTimer = null;
  let lastStatusAtMs = 0;
  const streamBuffer = new FrameStreamBuffer();

  let lastMachineConfig = null;
  let sessionReadyHandler = null;
  let forcePathHandler = null;
  let measuredHome = null;
  let lastHomingState = null;
  /** @type {Map<number, { resolve: Function, timer: NodeJS.Timeout, waitForMachineConfig?: boolean, ackOk?: boolean, config?: object|null }>} */
  const pendingByMessageId = new Map();

  function getStatus() {
    return { ...status, connected: connected && sessionReady };
  }

  function finishPendingCommand(messageId, result) {
    const pending = pendingByMessageId.get(messageId);
    if (!pending) {
      return;
    }
    clearTimeout(pending.timer);
    pendingByMessageId.delete(messageId);
    pending.resolve(result);
  }

  function tryCompleteGetMachineConfigPending(pending) {
    if (!pending.waitForMachineConfig || !pending.ackOk || pending.config == null) {
      return;
    }
    lastMachineConfig = pending.config;
    finishPendingCommand(MessageId.CmdGetMachineConfig, {
      ok: true,
      config: pending.config,
      status: getStatus(),
    });
  }

  function rejectPending(errorMessage) {
    const pendingIds = [...pendingByMessageId.keys()];
    for (const messageId of pendingIds) {
      finishPendingCommand(messageId, {
        ok: false,
        error: errorMessage,
        status: getStatus(),
      });
    }
  }

  function stopNopHeartbeat() {
    if (nopTimer) {
      clearInterval(nopTimer);
      nopTimer = null;
    }
  }

  function startNopHeartbeat() {
    stopNopHeartbeat();
    nopTimer = setInterval(() => {
      if (!isLiveSession()) {
        return;
      }
      const encoded = encodeEmptyMessage(MessageId.CmdNop);
      if (!encoded.ok || !socket) {
        return;
      }
      try {
        socket.write(encoded.frame);
      } catch {
        // reconnect path handles faults
      }
    }, NOP_INTERVAL_MS);
  }

  function markDisconnected(reason) {
    const wasLive = connected || sessionReady;
    connected = false;
    sessionReady = false;
    stopNopHeartbeat();
    measuredHome = null;
    lastHomingState = null;
    status = { ...status, connected: false, measuredHome: null };
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
    // First valid status after transport connect arms the session for mutating commands.
    lastStatusAtMs = Date.now();
    const becameReady = connected && !sessionReady;
    if (connected) {
      sessionReady = true;
      if (becameReady) {
        startNopHeartbeat();
      }
    }
    // StatusReport has no ADC fields. Keep the last MsgRawAdc sample.
    const keptAdc = {
      adcX: status.adcX,
      adcY: status.adcY,
      adcZ: status.adcZ,
      adcValid: status.adcValid,
    };
    status = statusReportToGuiStatus(decoded, connected && sessionReady);
    measuredHome = latchMeasuredHome({
      previousHomingState: lastHomingState,
      homingState: status.homingState,
      pose: { x: status.x, y: status.y, z: status.z },
      previousMeasuredHome: measuredHome,
    });
    lastHomingState = status.homingState;
    status = {
      ...status,
      ...keptAdc,
      measuredHome: measuredHome ? { ...measuredHome } : null,
    };
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
      const decoded = decodeMachineConfigPayload(frame.payload);
      const pendingGet = pendingByMessageId.get(MessageId.CmdGetMachineConfig);
      if (pendingGet && pendingGet.waitForMachineConfig) {
        if (decoded) {
          pendingGet.config = decoded;
          tryCompleteGetMachineConfigPending(pendingGet);
        }
        return;
      }
      if (decoded) {
        lastMachineConfig = decoded;
      }
      return;
    }

    if (frame.messageId === MessageId.MsgRawAdc) {
      const raw = decodeRawAdcPayload(frame.payload);
      if (raw) {
        status = {
          ...status,
          adcX: raw.x,
          adcY: raw.y,
          adcZ: raw.z,
          adcValid: raw.valid,
        };
      }
      return;
    }

    if (frame.messageId === MessageId.MsgForceCalibration) {
      const report = decodeForceCalibrationResultPayload(frame.payload);
      for (const id of [MessageId.CmdSetForceCalibration, MessageId.CmdSelectForceCurve]) {
        const pending = pendingByMessageId.get(id);
        if (pending) {
          pending.report = report;
        }
      }
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
      const error = ackErrorMessage(ack.resultCode);
      if (pending.waitForMachineConfig) {
        if (error) {
          finishPendingCommand(ack.refMessageId, {
            ok: false,
            error,
            resultCode: ack.resultCode,
            status: getStatus(),
          });
          return;
        }
        pending.ackOk = true;
        tryCompleteGetMachineConfigPending(pending);
        return;
      }
      finishPendingCommand(ack.refMessageId, {
        ok: !error,
        error,
        resultCode: ack.resultCode,
        status: getStatus(),
        report: pending.report || null,
      });
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
      finishPendingCommand(nack.refMessageId, {
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
        const statusFresh = lastStatusAtMs > 0 && Date.now() - lastStatusAtMs < STATUS_STALE_MS;
        // UART: status still flowing ⇒ link is up; only the command ACK was lost/slow.
        // TCP: keep fail-closed reconnect (half-open sockets).
        if (isUart && statusFresh && connected) {
          finishPendingCommand(messageId, {
            ok: false,
            error: 'Command ACK timeout (link still receiving status)',
            status: getStatus(),
          });
          return;
        }
        finishPendingCommand(messageId, {
          ok: false,
          error: MACHINE_UNAVAILABLE,
          status: getStatus(),
        });
        markDisconnected(MACHINE_UNAVAILABLE);
        if (!closed) {
          scheduleReconnect();
        }
      }, ackTimeoutMs);

      pendingByMessageId.set(messageId, { resolve, timer });

      try {
        activeSocket.write(frame, (writeErr) => {
          if (!writeErr) {
            return;
          }
          const pending = pendingByMessageId.get(messageId);
          if (pending) {
            finishPendingCommand(messageId, {
              ok: false,
              error: MACHINE_UNAVAILABLE,
              status: getStatus(),
            });
            markDisconnected(MACHINE_UNAVAILABLE);
          } else {
            markDisconnected(MACHINE_UNAVAILABLE);
          }
          if (!closed) {
            scheduleReconnect();
          }
        });
      } catch (err) {
        finishPendingCommand(messageId, {
          ok: false,
          error: MACHINE_UNAVAILABLE,
          status: getStatus(),
        });
        markDisconnected(MACHINE_UNAVAILABLE);
        if (!closed) {
          scheduleReconnect();
        }
      }
    });
  }

  function sendGetMachineConfigAndWait(frame) {
    if (!isLiveSession()) {
      return Promise.resolve({
        ok: false,
        error: MACHINE_UNAVAILABLE,
        status: getStatus(),
      });
    }
    if (pendingByMessageId.has(MessageId.CmdGetMachineConfig)) {
      return Promise.resolve({
        ok: false,
        error: 'Command already pending for this message id',
        status: getStatus(),
      });
    }

    const activeSocket = socket;

    return new Promise((resolve) => {
      const timer = setTimeout(() => {
        const statusFresh = lastStatusAtMs > 0 && Date.now() - lastStatusAtMs < STATUS_STALE_MS;
        if (isUart && statusFresh && connected) {
          finishPendingCommand(MessageId.CmdGetMachineConfig, {
            ok: false,
            error: 'Command ACK timeout (link still receiving status)',
            status: getStatus(),
          });
          return;
        }
        finishPendingCommand(MessageId.CmdGetMachineConfig, {
          ok: false,
          error: MACHINE_UNAVAILABLE,
          status: getStatus(),
        });
        markDisconnected(MACHINE_UNAVAILABLE);
        if (!closed) {
          scheduleReconnect();
        }
      }, ackTimeoutMs);

      pendingByMessageId.set(MessageId.CmdGetMachineConfig, {
        resolve,
        timer,
        waitForMachineConfig: true,
        ackOk: false,
        config: null,
      });

      try {
        activeSocket.write(frame, (writeErr) => {
          if (!writeErr) {
            return;
          }
          finishPendingCommand(MessageId.CmdGetMachineConfig, {
            ok: false,
            error: MACHINE_UNAVAILABLE,
            status: getStatus(),
          });
          markDisconnected(MACHINE_UNAVAILABLE);
          if (!closed) {
            scheduleReconnect();
          }
        });
      } catch (err) {
        finishPendingCommand(MessageId.CmdGetMachineConfig, {
          ok: false,
          error: MACHINE_UNAVAILABLE,
          status: getStatus(),
        });
        markDisconnected(MACHINE_UNAVAILABLE);
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
    }, reconnectDelayMs);
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
    stopNopHeartbeat();
    status = { ...status, connected: false };
    streamBuffer.clear();

    let s;
    try {
      s = createProtocolConnection(
        { transport, host, port, serialPath, baudRate },
        () => {
          if (s !== socket) {
            return;
          }
          // Link is up; mutating commands wait until first status report.
          connected = true;
          sessionReady = false;
          status = { ...status, connected: false };
          if (transport === 'uart' || transport === 'serial') {
            console.log(
              `ProtocolMachineClient connected UART ${serialPath} @ ${baudRate}`
            );
          } else {
            console.log(`ProtocolMachineClient connected to ${host}:${port}`);
          }
        }
      );
    } catch (err) {
      console.error(`ProtocolMachineClient connect failed: ${err.message || err}`);
      scheduleReconnect();
      return;
    }

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

  /** Core homing sequence Z+ → X+ → Y+ (CmdHome) — Reference only. */
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

  /** Explicit MCS definition (CmdDefineMcs). */
  async function defineMcs() {
    if (!isLiveSession()) {
      return { ok: false, error: MACHINE_UNAVAILABLE, status: getStatus() };
    }
    const encoded = encodeEmptyMessage(MessageId.CmdDefineMcs);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode DEFINE_MCS' };
    }
    return sendFrameAndWait(MessageId.CmdDefineMcs, encoded.frame);
  }

  /** Clear Homed latch without STOP (CmdInvalidateHoming). */
  async function invalidateHoming() {
    if (!isLiveSession()) {
      return { ok: false, error: MACHINE_UNAVAILABLE, status: getStatus() };
    }
    const encoded = encodeEmptyMessage(MessageId.CmdInvalidateHoming);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode INVALIDATE_HOMING' };
    }
    return sendFrameAndWait(MessageId.CmdInvalidateHoming, encoded.frame);
  }

  /**
   * Restore persisted MCS without motion (CmdRestoreMcs).
   * @param {number} x
   * @param {number} y
   * @param {number} z
   */
  async function restoreMcs(x, y, z) {
    if (!isLiveSession()) {
      return { ok: false, error: MACHINE_UNAVAILABLE, status: getStatus() };
    }
    const encoded = encodeRestoreMcsMessage(x, y, z);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode RESTORE_MCS' };
    }
    return sendFrameAndWait(MessageId.CmdRestoreMcs, encoded.frame);
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
    return sendGetMachineConfigAndWait(encoded.frame);
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

  async function setForceCalibration(command) {
    if (!isLiveSession()) {
      return { ok: false, error: MACHINE_UNAVAILABLE, status: getStatus() };
    }
    const encoded = encodeSetForceCalibrationMessage(command);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode SET_FORCE_CALIBRATION' };
    }
    return sendFrameAndWait(MessageId.CmdSetForceCalibration, encoded.frame);
  }

  async function selectForceCurve(command) {
    if (!isLiveSession()) {
      return { ok: false, error: MACHINE_UNAVAILABLE, status: getStatus() };
    }
    const encoded = encodeSelectForceCurveMessage(command);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode SELECT_FORCE_CURVE' };
    }
    return sendFrameAndWait(MessageId.CmdSelectForceCurve, encoded.frame);
  }

  async function setProbeCenter(x, y, z) {
    if (!isLiveSession()) {
      return { ok: false, error: MACHINE_UNAVAILABLE, status: getStatus() };
    }
    const encoded = encodeProbeCenterMessage(x, y, z);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode SET_PROBE_CENTER' };
    }
    return sendFrameAndWait(MessageId.CmdSetProbeCenter, encoded.frame);
  }

  async function setCollisionGuard(enabled) {
    if (!isLiveSession()) {
      return { ok: false, error: MACHINE_UNAVAILABLE, status: getStatus() };
    }
    const encoded = encodeBoolMessage(MessageId.CmdSetCollisionGuard, enabled === true);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode SET_COLLISION_GUARD' };
    }
    return sendFrameAndWait(MessageId.CmdSetCollisionGuard, encoded.frame);
  }

  async function fixtureOpen() {
    if (!isLiveSession()) {
      return { ok: false, error: MACHINE_UNAVAILABLE, status: getStatus() };
    }
    const encoded = encodeBoolMessage(MessageId.CmdSetFixture, true);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode SET_FIXTURE' };
    }
    return sendFrameAndWait(MessageId.CmdSetFixture, encoded.frame);
  }

  async function fixtureClose() {
    if (!isLiveSession()) {
      return { ok: false, error: MACHINE_UNAVAILABLE, status: getStatus() };
    }
    const encoded = encodeBoolMessage(MessageId.CmdSetFixture, false);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode SET_FIXTURE' };
    }
    return sendFrameAndWait(MessageId.CmdSetFixture, encoded.frame);
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
    defineMcs,
    invalidateHoming,
    restoreMcs,
    clearFault,
    setSpeed,
    loadProgram,
    start,
    setMachineConfig,
    getMachineConfig,
    setTestConfiguration,
    setPlateOrigin,
    setProbeCenter,
    setForceCalibration,
    selectForceCurve,
    setCollisionGuard,
    fixtureOpen,
    fixtureClose,
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
