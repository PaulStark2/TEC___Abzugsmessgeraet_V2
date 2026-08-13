'use strict';

/**
 * M12 frame/payload codec — mirrors communication/ Encoder, Decoder, PayloadCodec.
 */

const { crc16CcittFalse } = require('./crc16');
const {
  MAGIC0,
  MAGIC1,
  PROTOCOL_VERSION,
  MAX_PAYLOAD_SIZE,
  MIN_FRAME_SIZE,
  MOVE_TO_PAYLOAD_WIRE_SIZE,
  BOOL_PAYLOAD_WIRE_SIZE,
  ACK_PAYLOAD_WIRE_SIZE,
  NACK_PAYLOAD_WIRE_SIZE,
  STATUS_REPORT_PAYLOAD_WIRE_SIZE,
  LOAD_PROGRAM_MAX_POINT_COUNT,
  MessageId,
  DecodeResult,
  NO_CURRENT_POINT_INDEX,
  isKnownMessageId,
  machineStateName,
  motionStateName,
  processStateName,
} = require('./protocolTypes');

function isFiniteNumber(value) {
  return typeof value === 'number' && Number.isFinite(value);
}

function writeF32LE(buffer, offset, value) {
  if (!isFiniteNumber(value)) {
    return false;
  }
  buffer.writeFloatLE(value, offset);
  return true;
}

function readF32LE(buffer, offset) {
  const value = buffer.readFloatLE(offset);
  if (!Number.isFinite(value)) {
    return { ok: false };
  }
  return { ok: true, value };
}

function encodeMoveToPayload({ x, y, z }) {
  const out = Buffer.alloc(MOVE_TO_PAYLOAD_WIRE_SIZE);
  if (!writeF32LE(out, 0, x) || !writeF32LE(out, 4, y) || !writeF32LE(out, 8, z)) {
    return null;
  }
  return out;
}

function decodeMoveToPayload(payload) {
  if (!Buffer.isBuffer(payload) || payload.length !== MOVE_TO_PAYLOAD_WIRE_SIZE) {
    return null;
  }
  const x = readF32LE(payload, 0);
  const y = readF32LE(payload, 4);
  const z = readF32LE(payload, 8);
  if (!x.ok || !y.ok || !z.ok) {
    return null;
  }
  return { x: x.value, y: y.value, z: z.value };
}

function encodeBoolPayload(value) {
  const bit = value ? 1 : 0;
  return Buffer.from([bit]);
}

function decodeBoolPayload(payload) {
  if (!Buffer.isBuffer(payload) || payload.length !== BOOL_PAYLOAD_WIRE_SIZE) {
    return null;
  }
  const value = payload[0];
  if (value !== 0 && value !== 1) {
    return null;
  }
  return { value };
}

function encodeAckPayload({ refMessageId, resultCode }) {
  return Buffer.from([refMessageId & 0xff, resultCode & 0xff]);
}

function decodeAckPayload(payload) {
  if (!Buffer.isBuffer(payload) || payload.length !== ACK_PAYLOAD_WIRE_SIZE) {
    return null;
  }
  return {
    refMessageId: payload[0],
    resultCode: payload[1],
  };
}

function encodeNackPayload({ refMessageId, errorCode, detail = 0 }) {
  return Buffer.from([refMessageId & 0xff, errorCode & 0xff, detail & 0xff]);
}

function decodeNackPayload(payload) {
  if (!Buffer.isBuffer(payload) || payload.length !== NACK_PAYLOAD_WIRE_SIZE) {
    return null;
  }
  return {
    refMessageId: payload[0],
    errorCode: payload[1],
    detail: payload[2],
  };
}

function decodeStatusReportPayload(payload) {
  if (!Buffer.isBuffer(payload) || payload.length !== STATUS_REPORT_PAYLOAD_WIRE_SIZE) {
    return null;
  }

  let offset = 0;
  const machineState = payload[offset++];
  const motionState = payload[offset++];
  const processState = payload[offset++];

  const x = readF32LE(payload, offset);
  offset += 4;
  const y = readF32LE(payload, offset);
  offset += 4;
  const z = readF32LE(payload, offset);
  offset += 4;
  const fx = readF32LE(payload, offset);
  offset += 4;
  const fy = readF32LE(payload, offset);
  offset += 4;
  const fz = readF32LE(payload, offset);
  offset += 4;

  if (!x.ok || !y.ok || !z.ok || !fx.ok || !fy.ok || !fz.ok) {
    return null;
  }

  const limits = payload[offset++];
  const eStop = payload[offset++];
  const spreaderExtend = payload[offset++];
  const spreaderExtended = payload[offset++];
  const fixtureExtend = payload[offset++];
  const fixtureExtended = payload[offset++];
  const currentPointIndex = payload[offset++];
  const programPointCount = payload[offset++];

  const boolFields = [eStop, spreaderExtend, spreaderExtended, fixtureExtend, fixtureExtended];
  if (boolFields.some((v) => v !== 0 && v !== 1)) {
    return null;
  }

  return {
    machineState,
    motionState,
    processState,
    x: x.value,
    y: y.value,
    z: z.value,
    fx: fx.value,
    fy: fy.value,
    fz: fz.value,
    limits,
    eStop,
    spreaderExtend,
    spreaderExtended,
    fixtureExtend,
    fixtureExtended,
    currentPointIndex,
    programPointCount,
  };
}

function encodeStatusReportPayload(status) {
  const out = Buffer.alloc(STATUS_REPORT_PAYLOAD_WIRE_SIZE);
  let offset = 0;
  out[offset++] = status.machineState & 0xff;
  out[offset++] = status.motionState & 0xff;
  out[offset++] = status.processState & 0xff;
  if (
    !writeF32LE(out, offset, status.x) ||
    !writeF32LE(out, offset + 4, status.y) ||
    !writeF32LE(out, offset + 8, status.z) ||
    !writeF32LE(out, offset + 12, status.fx) ||
    !writeF32LE(out, offset + 16, status.fy) ||
    !writeF32LE(out, offset + 20, status.fz)
  ) {
    return null;
  }
  offset += 24;
  out[offset++] = status.limits & 0xff;
  out[offset++] = status.eStop ? 1 : 0;
  out[offset++] = status.spreaderExtend ? 1 : 0;
  out[offset++] = status.spreaderExtended ? 1 : 0;
  out[offset++] = status.fixtureExtend ? 1 : 0;
  out[offset++] = status.fixtureExtended ? 1 : 0;
  out[offset++] = status.currentPointIndex & 0xff;
  out[offset++] = status.programPointCount & 0xff;
  return out;
}

function statusReportToGuiStatus(decoded, connected = true) {
  return {
    machineState: machineStateName(decoded.machineState),
    motionState: motionStateName(decoded.motionState),
    processState: processStateName(decoded.processState),
    x: decoded.x,
    y: decoded.y,
    z: decoded.z,
    fx: decoded.fx,
    fy: decoded.fy,
    fz: decoded.fz,
    limits: decoded.limits,
    eStop: decoded.eStop !== 0,
    spreaderExtend: decoded.spreaderExtend !== 0,
    spreaderExtended: decoded.spreaderExtended !== 0,
    fixtureExtend: decoded.fixtureExtend !== 0,
    fixtureExtended: decoded.fixtureExtended !== 0,
    pneumaticExtended: decoded.spreaderExtended !== 0,
    currentPointIndex:
      decoded.currentPointIndex === NO_CURRENT_POINT_INDEX ? null : decoded.currentPointIndex,
    programPointCount: decoded.programPointCount,
    connected: Boolean(connected),
  };
}

function isEmptyCommandId(messageId) {
  switch (messageId) {
    case MessageId.CmdNop:
    case MessageId.CmdStop:
    case MessageId.CmdPause:
    case MessageId.CmdResume:
    case MessageId.CmdStart:
    case MessageId.CmdClearFault:
    case MessageId.CmdRequestStatus:
      return true;
    default:
      return false;
  }
}

function validatePayloadForMessage(messageId, payload) {
  const length = payload ? payload.length : 0;
  if (isEmptyCommandId(messageId)) {
    return length === 0;
  }
  switch (messageId) {
    case MessageId.CmdMoveTo:
      return length === MOVE_TO_PAYLOAD_WIRE_SIZE && decodeMoveToPayload(payload) !== null;
    case MessageId.CmdSetSpreader:
    case MessageId.CmdSetFixture:
      return length === BOOL_PAYLOAD_WIRE_SIZE && decodeBoolPayload(payload) !== null;
    case MessageId.RspAck:
      return length === ACK_PAYLOAD_WIRE_SIZE && decodeAckPayload(payload) !== null;
    case MessageId.RspNack:
      return length === NACK_PAYLOAD_WIRE_SIZE && decodeNackPayload(payload) !== null;
    case MessageId.MsgStatusReport:
      return length === STATUS_REPORT_PAYLOAD_WIRE_SIZE && decodeStatusReportPayload(payload) !== null;
    case MessageId.CmdLoadProgram:
      if (length < 1) {
        return false;
      }
      {
        const count = payload[0];
        if (count > 32) {
          return false;
        }
        return length === 1 + count * MOVE_TO_PAYLOAD_WIRE_SIZE;
      }
    case MessageId.MsgProgramSummary:
      return length === 26;
    default:
      return false;
  }
}

/**
 * Encode one complete M12 frame.
 * @returns {{ ok: boolean, frame?: Buffer, error?: string }}
 */
function encodeFrame(messageId, payload) {
  const payloadBuf = payload ? Buffer.from(payload) : Buffer.alloc(0);
  if (payloadBuf.length > MAX_PAYLOAD_SIZE) {
    return { ok: false, error: 'PayloadTooLarge' };
  }

  const frame = Buffer.alloc(6 + payloadBuf.length + 2);
  frame[0] = MAGIC0;
  frame[1] = MAGIC1;
  frame[2] = PROTOCOL_VERSION;
  frame[3] = messageId & 0xff;
  frame[4] = payloadBuf.length & 0xff;
  frame[5] = (payloadBuf.length >> 8) & 0xff;
  if (payloadBuf.length > 0) {
    payloadBuf.copy(frame, 6);
  }
  const crc = crc16CcittFalse(frame.subarray(0, 6 + payloadBuf.length));
  frame[6 + payloadBuf.length] = crc & 0xff;
  frame[6 + payloadBuf.length + 1] = (crc >> 8) & 0xff;
  return { ok: true, frame };
}

function encodeEmptyMessage(messageId) {
  return encodeFrame(messageId, null);
}

function encodeMoveToMessage(x, y, z) {
  const payload = encodeMoveToPayload({ x, y, z });
  if (!payload) {
    return { ok: false, error: 'InvalidPayload' };
  }
  return encodeFrame(MessageId.CmdMoveTo, payload);
}

function encodeBoolMessage(messageId, value) {
  const payload = encodeBoolPayload(value);
  return encodeFrame(messageId, payload);
}

/**
 * Encode CMD_LOAD_PROGRAM payload (mirrors tecomm::encodeLoadProgramPayload).
 * @param {Array<{x:number,y:number,z:number}>} points
 * @returns {Buffer|null}
 */
function encodeLoadProgramPayload(points) {
  if (!Array.isArray(points)) {
    return null;
  }
  if (points.length > LOAD_PROGRAM_MAX_POINT_COUNT) {
    return null;
  }
  const out = Buffer.alloc(1 + points.length * MOVE_TO_PAYLOAD_WIRE_SIZE);
  out[0] = points.length & 0xff;
  let offset = 1;
  for (const point of points) {
    if (
      !writeF32LE(out, offset, Number(point.x)) ||
      !writeF32LE(out, offset + 4, Number(point.y)) ||
      !writeF32LE(out, offset + 8, Number(point.z))
    ) {
      return null;
    }
    offset += MOVE_TO_PAYLOAD_WIRE_SIZE;
  }
  return out;
}

function encodeLoadProgramMessage(points) {
  const payload = encodeLoadProgramPayload(points);
  if (!payload) {
    return { ok: false, error: 'InvalidPayload' };
  }
  return encodeFrame(MessageId.CmdLoadProgram, payload);
}

/**
 * Decode one complete frame from a buffer prefix.
 * Mirrors tecomm::decodeFrame.
 * @returns {{ result: string, frame?: object, bytesConsumed: number }}
 */
function decodeFrame(data) {
  const buf = Buffer.isBuffer(data) ? data : Buffer.from(data);
  if (buf.length < MIN_FRAME_SIZE) {
    return { result: DecodeResult.NeedMoreData, bytesConsumed: 0 };
  }
  if (buf[0] !== MAGIC0 || buf[1] !== MAGIC1) {
    return { result: DecodeResult.InvalidFrame, bytesConsumed: 0 };
  }
  if (buf[2] !== PROTOCOL_VERSION) {
    return { result: DecodeResult.UnsupportedVersion, bytesConsumed: 0 };
  }

  const messageId = buf[3];
  const payloadLength = buf[4] | (buf[5] << 8);
  if (payloadLength > MAX_PAYLOAD_SIZE) {
    return { result: DecodeResult.InvalidLength, bytesConsumed: 0 };
  }

  const frameSize = 6 + payloadLength + 2;
  if (buf.length < frameSize) {
    return { result: DecodeResult.NeedMoreData, bytesConsumed: 0 };
  }

  const expectedCrc = crc16CcittFalse(buf.subarray(0, 6 + payloadLength));
  const receivedCrc = buf[6 + payloadLength] | (buf[6 + payloadLength + 1] << 8);
  if (expectedCrc !== receivedCrc) {
    return { result: DecodeResult.CrcError, bytesConsumed: frameSize };
  }

  const payload = payloadLength > 0 ? Buffer.from(buf.subarray(6, 6 + payloadLength)) : Buffer.alloc(0);
  const frame = {
    protocolVersion: PROTOCOL_VERSION,
    messageId,
    payload,
    payloadLength,
  };

  if (!isKnownMessageId(messageId)) {
    return { result: DecodeResult.UnknownMessageId, frame, bytesConsumed: frameSize };
  }
  if (!validatePayloadForMessage(messageId, payload)) {
    return { result: DecodeResult.InvalidPayload, frame, bytesConsumed: frameSize };
  }

  return { result: DecodeResult.Ok, frame, bytesConsumed: frameSize };
}

module.exports = {
  encodeFrame,
  encodeEmptyMessage,
  encodeMoveToMessage,
  encodeBoolMessage,
  encodeLoadProgramPayload,
  encodeLoadProgramMessage,
  encodeMoveToPayload,
  decodeMoveToPayload,
  encodeBoolPayload,
  decodeBoolPayload,
  encodeAckPayload,
  decodeAckPayload,
  encodeNackPayload,
  decodeNackPayload,
  encodeStatusReportPayload,
  decodeStatusReportPayload,
  statusReportToGuiStatus,
  decodeFrame,
  validatePayloadForMessage,
};
