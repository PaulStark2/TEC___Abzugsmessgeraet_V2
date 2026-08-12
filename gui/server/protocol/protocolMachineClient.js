'use strict';

/**
 * ProtocolMachineClient — TCP client for M13 simulation_protocol (M12 binary).
 *
 * Transport/mapping only. No motion, safety, or process logic in Node.js.
 */

const net = require('net');
const { FrameStreamBuffer } = require('./frameStreamBuffer');
const {
  encodeEmptyMessage,
  encodeMoveToMessage,
  encodeBoolMessage,
  decodeAckPayload,
  decodeNackPayload,
  decodeStatusReportPayload,
  statusReportToGuiStatus,
} = require('./m12Codec');
const { MessageId, AckResultCode, NackErrorCode, DecodeResult } = require('./protocolTypes');

const DEFAULT_JOG_STEP = 1;
const RECONNECT_DELAY_MS = 2000;

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
  let closed = false;
  let reconnectTimer = null;
  const streamBuffer = new FrameStreamBuffer();

  /** @type {Map<number, { resolve: Function, reject: Function, timer: NodeJS.Timeout }>} */
  const pendingByMessageId = new Map();

  function getStatus() {
    return { ...status, connected };
  }

  function applyStatusReport(payload) {
    const decoded = decodeStatusReportPayload(payload);
    if (!decoded) {
      return;
    }
    status = statusReportToGuiStatus(decoded, connected);
  }

  function rejectPending(errorMessage) {
    for (const [, pending] of pendingByMessageId) {
      clearTimeout(pending.timer);
      pending.resolve({ ok: false, error: errorMessage });
    }
    pendingByMessageId.clear();
  }

  function handleInboundFrame(frame, decodeResult) {
    if (decodeResult !== DecodeResult.Ok || !frame) {
      return;
    }

    if (frame.messageId === MessageId.MsgStatusReport) {
      applyStatusReport(frame.payload);
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
    if (!socket || !connected) {
      return Promise.resolve({ ok: false, error: 'Not connected to simulation_protocol' });
    }
    if (pendingByMessageId.has(messageId)) {
      return Promise.resolve({ ok: false, error: 'Command already pending for this message id' });
    }

    return new Promise((resolve) => {
      const timer = setTimeout(() => {
        pendingByMessageId.delete(messageId);
        resolve({ ok: false, error: 'Protocol ACK timeout', status: getStatus() });
      }, ackTimeoutMs);

      pendingByMessageId.set(messageId, { resolve, reject: resolve, timer });

      try {
        socket.write(frame);
      } catch (err) {
        clearTimeout(timer);
        pendingByMessageId.delete(messageId);
        resolve({ ok: false, error: err.message || String(err), status: getStatus() });
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

  function connect() {
    if (closed) {
      return;
    }
    if (socket) {
      socket.destroy();
      socket = null;
    }
    connected = false;
    status = { ...status, connected: false };
    streamBuffer.clear();

    const s = net.createConnection({ host, port }, () => {
      connected = true;
      status = { ...status, connected: true };
      console.log(`ProtocolMachineClient connected to ${host}:${port}`);
    });

    socket = s;

    s.on('data', (chunk) => {
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
    });

    s.on('close', () => {
      connected = false;
      status = { ...status, connected: false };
      socket = null;
      streamBuffer.clear();
      rejectPending('Disconnected from simulation_protocol');
      if (!closed) {
        console.log('ProtocolMachineClient disconnected; reconnecting…');
        scheduleReconnect();
      }
    });
  }

  async function moveTo(x, y, z) {
    const nx = Number(x);
    const ny = Number(y);
    const nz = Number(z);
    if (![nx, ny, nz].every(Number.isFinite)) {
      return { ok: false, error: 'moveTo requires finite x, y, z' };
    }
    const encoded = encodeMoveToMessage(nx, ny, nz);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode MOVE_TO' };
    }
    return sendFrameAndWait(MessageId.CmdMoveTo, encoded.frame);
  }

  async function pause() {
    const encoded = encodeEmptyMessage(MessageId.CmdPause);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode PAUSE' };
    }
    return sendFrameAndWait(MessageId.CmdPause, encoded.frame);
  }

  async function resume() {
    const encoded = encodeEmptyMessage(MessageId.CmdResume);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode RESUME' };
    }
    return sendFrameAndWait(MessageId.CmdResume, encoded.frame);
  }

  async function stop() {
    const encoded = encodeEmptyMessage(MessageId.CmdStop);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode STOP' };
    }
    return sendFrameAndWait(MessageId.CmdStop, encoded.frame);
  }

  async function jog(axis, direction, step) {
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
    const encoded = encodeBoolMessage(MessageId.CmdSetSpreader, true);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode SET_SPREADER' };
    }
    return sendFrameAndWait(MessageId.CmdSetSpreader, encoded.frame);
  }

  async function pneumaticClose() {
    const encoded = encodeBoolMessage(MessageId.CmdSetSpreader, false);
    if (!encoded.ok) {
      return { ok: false, error: 'Failed to encode SET_SPREADER' };
    }
    return sendFrameAndWait(MessageId.CmdSetSpreader, encoded.frame);
  }

  /**
   * Compatibility workaround: command move to origin. Not Core homing.
   */
  async function home() {
    return moveTo(0, 0, 0);
  }

  async function setSpeed() {
    return { ok: false, error: 'Not supported over protocol' };
  }

  function close() {
    closed = true;
    if (reconnectTimer) {
      clearTimeout(reconnectTimer);
      reconnectTimer = null;
    }
    rejectPending('ProtocolMachineClient closed');
    if (socket) {
      socket.destroy();
      socket = null;
    }
    connected = false;
    status = { ...status, connected: false };
    streamBuffer.clear();
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
    setSpeed,
    close,
  };
}

module.exports = {
  createProtocolMachineClient,
};
