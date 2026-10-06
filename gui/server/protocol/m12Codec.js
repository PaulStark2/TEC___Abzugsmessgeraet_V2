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
  MACHINE_CONFIG_PAYLOAD_WIRE_SIZE,
  TEST_CONFIGURATION_PAYLOAD_WIRE_SIZE,
  FORCE_PATH_CHUNK_MAX_SAMPLES,
  FORCE_PATH_CHUNK_HEADER_WIRE_SIZE,
  FORCE_CALIB_MAX_POINTS,
  FORCE_CALIB_POINT_WIRE_SIZE,
  SET_FORCE_CALIBRATION_HEADER_WIRE_SIZE,
  SELECT_FORCE_CURVE_WIRE_SIZE,
  RAW_ADC_WIRE_SIZE,
  FORCE_CALIB_RESULT_HEADER_WIRE_SIZE,
  MessageId,
  DecodeResult,
  NO_CURRENT_POINT_INDEX,
  isKnownMessageId,
  machineStateName,
  motionStateName,
  homingStateName,
  processStateName,
  procedurePhaseName,
  faultReasonName,
  spreaderStateName,
  StatusFlag,
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

/** Field order must match communication/PayloadCodec.cpp encodeMachineConfigPayload. */
const MACHINE_CONFIG_FIELDS = [
  { key: 'v_approach_testpoints', type: 'f32' },
  { key: 'v_fine_testpoints', type: 'f32' },
  { key: 'v_unclamp', type: 'f32' },
  { key: 'v_homing', type: 'f32' },
  { key: 'a_testpoints', type: 'f32' },
  { key: 'corner_testpoints', type: 'f32' },
  { key: 'a_homing', type: 'f32' },
  { key: 'force_filter', type: 'f32' },
  { key: 'xy_tolerance_N', type: 'f32' },
  { key: 'xy_correction_step', type: 'f32' },
  { key: 'xy_max_correction', type: 'f32' },
  { key: 'insertion_buffer', type: 'f32' },
  { key: 'collision_approach_N', type: 'f32' },
  { key: 'collision_retract_N', type: 'f32' },
  { key: 'collision_clamp_N', type: 'f32' },
  { key: 'collision_unclamp_N', type: 'f32' },
  { key: 'collision_contact_N', type: 'f32' },
  { key: 'collision_measure_N', type: 'f32' },
  { key: 'spreader_extend_time_s', type: 'f32' },
  { key: 'spreader_retract_time_s', type: 'f32' },
  { key: 'force_tolerance_N', type: 'f32' },
  { key: 'z_correction_gain', type: 'f32' },
  { key: 'z_correction_max', type: 'f32' },
  { key: 'minimum_correction', type: 'f32' },
  { key: 'max_optimization_iterations', type: 'u16' },
  { key: 'v_search', type: 'f32' },
  { key: 'probe_search_distance', type: 'f32' },
  { key: 'probe_backoff', type: 'f32' },
  { key: 'home_offset_x', type: 'f32' },
  { key: 'home_offset_y', type: 'f32' },
  { key: 'home_offset_z', type: 'f32' },
  { key: 'tool_offset_x', type: 'f32' },
  { key: 'tool_offset_y', type: 'f32' },
  { key: 'tool_offset_z', type: 'f32' },
  { key: 'safe_z_buffer', type: 'f32' },
  { key: 'sample_timeout_s', type: 'f32' },
  { key: 'heartbeat_timeout_s', type: 'f32' },
  { key: 'xy_correct_timeout_s', type: 'f32' },
  { key: 'head_diameter', type: 'f32' },
  { key: 'measuring_head_diameter', type: 'f32' },
  { key: 'measuring_head_thickness', type: 'f32' },
  { key: 'tool_diameter', type: 'f32' },
  { key: 'search_depth', type: 'f32' },
  { key: 'approach_distance', type: 'f32' },
  { key: 'v_search_fine', type: 'f32' },
  { key: 'probe_offset_reference_x', type: 'f32' },
  { key: 'probe_offset_reference_y', type: 'f32' },
  { key: 'probe_offset_reference_z', type: 'f32' },
  { key: 'tool_cylindrical_height', type: 'f32' },
  { key: 'probe_xy_search_margin', type: 'f32' },
  { key: 'insert_search_distance', type: 'f32' },
  { key: 'num_fine_searches', type: 'u16' },
  { key: 'phase1_v_extract', type: 'f32' },
  { key: 'phase1_v_insert', type: 'f32' },
  { key: 'phase1_a', type: 'f32' },
  { key: 'phase1_corner', type: 'f32' },
  { key: 'phase2_v_extract', type: 'f32' },
  { key: 'phase2_v_insert', type: 'f32' },
  { key: 'phase2_a', type: 'f32' },
  { key: 'phase2_corner', type: 'f32' },
  { key: 'phase3_v_extract', type: 'f32' },
  { key: 'phase3_v_insert', type: 'f32' },
  { key: 'phase3_a', type: 'f32' },
  { key: 'phase3_corner', type: 'f32' },
  { key: 'v_approach_probe', type: 'f32' },
  { key: 'v_transfer_probe', type: 'f32' },
  { key: 'a_probe_transfer', type: 'f32' },
  { key: 'corner_probe_transfer', type: 'f32' },
  { key: 'a_probe_search', type: 'f32' },
  { key: 'a_probe_brake', type: 'f32' },
  { key: 'probe_center_x', type: 'f32', optionalZero: true },
  { key: 'probe_center_y', type: 'f32', optionalZero: true },
  { key: 'probe_center_z', type: 'f32', optionalZero: true },
  { key: 'force_range_N', type: 'f32', defaultValue: 30 },
  { key: 'force_hard_limit_N', type: 'f32', defaultValue: 30 },
];

function encodeMachineConfigPayload(cfg) {
  const out = Buffer.alloc(MACHINE_CONFIG_PAYLOAD_WIRE_SIZE);
  let offset = 0;
  for (const field of MACHINE_CONFIG_FIELDS) {
    if (field.type === 'u16') {
      const value = cfg[field.key];
      if (!Number.isInteger(value) || value < 0) {
        return null;
      }
      out.writeUInt16LE(value & 0xffff, offset);
      offset += 2;
      continue;
    }
    let value = cfg[field.key];
    if (field.optionalZero && !Number.isFinite(value)) {
      value = 0;
    }
    if (field.defaultValue !== undefined && !Number.isFinite(value)) {
      value = field.defaultValue;
    }
    if (!writeF32LE(out, offset, value)) {
      return null;
    }
    offset += 4;
  }
  return offset === MACHINE_CONFIG_PAYLOAD_WIRE_SIZE ? out : null;
}

function decodeMachineConfigPayload(payload) {
  if (!Buffer.isBuffer(payload) || payload.length !== MACHINE_CONFIG_PAYLOAD_WIRE_SIZE) {
    return null;
  }
  const out = {};
  let offset = 0;
  for (const field of MACHINE_CONFIG_FIELDS) {
    if (field.type === 'u16') {
      out[field.key] = payload.readUInt16LE(offset);
      offset += 2;
      continue;
    }
    const value = readF32LE(payload, offset);
    if (!value.ok) {
      return null;
    }
    out[field.key] = value.value;
    offset += 4;
  }
  return offset === MACHINE_CONFIG_PAYLOAD_WIRE_SIZE ? out : null;
}

function encodeTestConfigurationPayload({ mode, target_force_N, iterations, active_point_index }) {
  if (mode !== 0 && mode !== 1) {
    return null;
  }
  if (!isFiniteNumber(target_force_N) || !Number.isInteger(iterations) || iterations < 0) {
    return null;
  }
  const out = Buffer.alloc(TEST_CONFIGURATION_PAYLOAD_WIRE_SIZE);
  out[0] = mode;
  if (!writeF32LE(out, 1, target_force_N)) {
    return null;
  }
  out.writeUInt16LE(iterations & 0xffff, 5);
  out[7] = active_point_index & 0xff;
  return out;
}

function decodeTestConfigurationPayload(payload) {
  if (!Buffer.isBuffer(payload) || payload.length !== TEST_CONFIGURATION_PAYLOAD_WIRE_SIZE) {
    return null;
  }
  const mode = payload[0];
  if (mode !== 0 && mode !== 1) {
    return null;
  }
  const force = readF32LE(payload, 1);
  if (!force.ok) {
    return null;
  }
  return {
    mode,
    target_force_N: force.value,
    iterations: payload.readUInt16LE(5),
    active_point_index: payload[7],
  };
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
  const homingState = payload[offset++];
  const blockedDirections = payload[offset++];
  const flags = payload[offset++];
  const procedurePhase = payload[offset++];
  const faultReason = payload[offset++];
  const spreaderState = payload[offset++];

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
    homingState,
    blockedDirections,
    flags,
    procedurePhase,
    faultReason,
    spreaderState,
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
  out[offset++] = status.homingState & 0xff;
  out[offset++] = status.blockedDirections & 0xff;
  out[offset++] = status.flags & 0xff;
  out[offset++] = (status.procedurePhase || 0) & 0xff;
  out[offset++] = (status.faultReason || 0) & 0xff;
  out[offset++] = (status.spreaderState || 0) & 0xff;
  return out;
}

function statusReportToGuiStatus(decoded, connected = true) {
  const flagBit = (bit) => (decoded.flags & (1 << bit)) !== 0;
  const limitWarning =
    flagBit(StatusFlag.LimitWarning) ||
    decoded.limits !== 0 ||
    decoded.blockedDirections !== 0;
  return {
    machineState: machineStateName(decoded.machineState),
    motionState: motionStateName(decoded.motionState),
    processState: processStateName(decoded.processState),
    homingState: homingStateName(decoded.homingState),
    isHomed: flagBit(StatusFlag.IsHomed),
    communicationWatchdogEnabled: flagBit(StatusFlag.CommWatchdog),
    forceSampleFresh: flagBit(StatusFlag.ForceFresh),
    plateOriginValid: flagBit(StatusFlag.PlateOriginValid),
    probeCenterValid: flagBit(StatusFlag.ProbeCenterValid),
    mcsValid: flagBit(StatusFlag.McsValid),
    limitWarning,
    x: decoded.x,
    y: decoded.y,
    z: decoded.z,
    fx: decoded.fx,
    fy: decoded.fy,
    fz: decoded.fz,
    limits: decoded.limits,
    blockedDirections: decoded.blockedDirections,
    eStop: decoded.eStop !== 0,
    spreaderExtend: decoded.spreaderExtend !== 0,
    spreaderExtended: decoded.spreaderExtended !== 0,
    fixtureExtend: decoded.fixtureExtend !== 0,
    fixtureExtended: decoded.fixtureExtended !== 0,
    pneumaticExtended: decoded.spreaderExtended !== 0,
    currentPointIndex:
      decoded.currentPointIndex === NO_CURRENT_POINT_INDEX ? null : decoded.currentPointIndex,
    programPointCount: decoded.programPointCount,
    procedurePhase: procedurePhaseName(decoded.procedurePhase),
    procedurePhaseOrdinal: decoded.procedurePhase,
    faultReason: faultReasonName(decoded.faultReason || 0),
    faultReasonOrdinal: decoded.faultReason || 0,
    spreaderState: spreaderStateName(decoded.spreaderState || 0),
    spreaderStateOrdinal: decoded.spreaderState || 0,
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
    case MessageId.CmdHome:
    case MessageId.CmdGetMachineConfig:
    case MessageId.CmdStartProbeCalib:
    case MessageId.CmdDefineMcs:
    case MessageId.CmdInvalidateHoming:
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
    case MessageId.CmdSetCollisionGuard:
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
    case MessageId.CmdSetMachineConfig:
    case MessageId.MsgMachineConfig:
      return length === MACHINE_CONFIG_PAYLOAD_WIRE_SIZE && decodeMachineConfigPayload(payload) !== null;
    case MessageId.CmdSetTestConfiguration:
      return length === TEST_CONFIGURATION_PAYLOAD_WIRE_SIZE && decodeTestConfigurationPayload(payload) !== null;
    case MessageId.CmdSetPlateOrigin:
      return length === MOVE_TO_PAYLOAD_WIRE_SIZE && decodeMoveToPayload(payload) !== null;
    case MessageId.CmdSetProbeCenter:
      return length === MOVE_TO_PAYLOAD_WIRE_SIZE && decodeMoveToPayload(payload) !== null;
    case MessageId.CmdRestoreMcs:
      return length === MOVE_TO_PAYLOAD_WIRE_SIZE && decodeMoveToPayload(payload) !== null;
    case MessageId.MsgForcePathChunk:
      return decodeForcePathChunkPayload(payload) !== null;
    case MessageId.CmdSetForceCalibration:
      return decodeSetForceCalibrationPayload(payload) !== null;
    case MessageId.CmdSelectForceCurve:
      return decodeSelectForceCurvePayload(payload) !== null;
    case MessageId.MsgRawAdc:
      return decodeRawAdcPayload(payload) !== null;
    case MessageId.MsgForceCalibration:
      return decodeForceCalibrationResultPayload(payload) !== null;
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

function encodeMachineConfigMessage(cfg) {
  const payload = encodeMachineConfigPayload(cfg);
  if (!payload) {
    return { ok: false, error: 'InvalidPayload' };
  }
  return encodeFrame(MessageId.CmdSetMachineConfig, payload);
}

function encodeTestConfigurationMessage(cfg) {
  const payload = encodeTestConfigurationPayload(cfg);
  if (!payload) {
    return { ok: false, error: 'InvalidPayload' };
  }
  return encodeFrame(MessageId.CmdSetTestConfiguration, payload);
}

function encodePlateOriginMessage(x, y, z) {
  const payload = encodeMoveToPayload({ x, y, z });
  if (!payload) {
    return { ok: false, error: 'InvalidPayload' };
  }
  return encodeFrame(MessageId.CmdSetPlateOrigin, payload);
}

function encodeProbeCenterMessage(x, y, z) {
  const payload = encodeMoveToPayload({ x, y, z });
  if (!payload) {
    return { ok: false, error: 'InvalidPayload' };
  }
  return encodeFrame(MessageId.CmdSetProbeCenter, payload);
}

function encodeRestoreMcsMessage(x, y, z) {
  const payload = encodeMoveToPayload({ x, y, z });
  if (!payload) {
    return { ok: false, error: 'InvalidPayload' };
  }
  return encodeFrame(MessageId.CmdRestoreMcs, payload);
}

function encodeForcePathChunkPayload(chunk) {
  const count = Number(chunk.sampleCount);
  if (!Number.isInteger(count) || count < 0 || count > FORCE_PATH_CHUNK_MAX_SAMPLES) {
    return null;
  }
  const out = Buffer.alloc(FORCE_PATH_CHUNK_HEADER_WIRE_SIZE + count * 8);
  let offset = 0;
  out[offset++] = (chunk.pointIndex || 0) & 0xff;
  out.writeUInt16LE((chunk.iteration || 0) & 0xffff, offset);
  offset += 2;
  out.writeUInt32LE((chunk.sequence || 0) >>> 0, offset);
  offset += 4;
  out[offset++] = count & 0xff;
  out.writeUInt32LE((chunk.dropped || 0) >>> 0, offset);
  offset += 4;
  const zs = chunk.z || [];
  const fzs = chunk.fz || [];
  for (let i = 0; i < count; ++i) {
    if (!writeF32LE(out, offset, Number(zs[i])) || !writeF32LE(out, offset + 4, Number(fzs[i]))) {
      return null;
    }
    offset += 8;
  }
  return out;
}

function decodeForcePathChunkPayload(payload) {
  if (!Buffer.isBuffer(payload) || payload.length < FORCE_PATH_CHUNK_HEADER_WIRE_SIZE) {
    return null;
  }
  let offset = 0;
  const pointIndex = payload[offset++];
  const iteration = payload.readUInt16LE(offset);
  offset += 2;
  const sequence = payload.readUInt32LE(offset);
  offset += 4;
  const sampleCount = payload[offset++];
  const dropped = payload.readUInt32LE(offset);
  offset += 4;
  if (sampleCount > FORCE_PATH_CHUNK_MAX_SAMPLES) {
    return null;
  }
  if (payload.length !== FORCE_PATH_CHUNK_HEADER_WIRE_SIZE + sampleCount * 8) {
    return null;
  }
  const z = [];
  const fz = [];
  for (let i = 0; i < sampleCount; ++i) {
    const zv = readF32LE(payload, offset);
    const fv = readF32LE(payload, offset + 4);
    if (!zv.ok || !fv.ok) {
      return null;
    }
    z.push(zv.value);
    fz.push(fv.value);
    offset += 8;
  }
  return { pointIndex, iteration, sequence, sampleCount, dropped, z, fz };
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

const FORCE_RESULT_FLOATS = [
  'beta0',
  'beta1',
  'beta2',
  'pearsonR',
  'rSquared',
  'maxAbsResidualN',
  'meanResidualTensionN',
  'meanResidualCompressionN',
  'linearMaxAbsN',
  'quadraticMaxAbsN',
  'linearR2',
  'quadraticR2',
  'linearBeta0',
  'linearBeta1',
  'linearBeta2',
  'linearValid',
  'quadraticBeta0',
  'quadraticBeta1',
  'quadraticBeta2',
  'quadraticValid',
];

function encodeSetForceCalibrationPayload(command) {
  const points = command.points || [];
  if (points.length < 2 || points.length > FORCE_CALIB_MAX_POINTS) {
    return null;
  }
  const out = Buffer.alloc(SET_FORCE_CALIBRATION_HEADER_WIRE_SIZE + points.length * FORCE_CALIB_POINT_WIRE_SIZE);
  out[0] = command.source & 0xff;
  out[1] = command.confirm ? 1 : 0;
  out[2] = command.commit ? 1 : 0;
  out[3] = points.length;
  let offset = 4;
  for (const point of points) {
    out.writeUInt16LE(point.adcNet & 0xffff, offset);
    out[offset + 2] = point.direction & 0xff;
    out[offset + 3] = 0;
    if (!writeF32LE(out, offset + 4, point.massG) || !writeF32LE(out, offset + 8, point.nominalG)) {
      return null;
    }
    offset += FORCE_CALIB_POINT_WIRE_SIZE;
  }
  return out;
}

function decodeSetForceCalibrationPayload(payload) {
  if (!Buffer.isBuffer(payload) || payload.length < SET_FORCE_CALIBRATION_HEADER_WIRE_SIZE) {
    return null;
  }
  const count = payload[3];
  if (payload[0] > 2 || payload[1] > 1 || payload[2] > 1 || count < 2 || count > FORCE_CALIB_MAX_POINTS) {
    return null;
  }
  if (payload.length !== SET_FORCE_CALIBRATION_HEADER_WIRE_SIZE + count * FORCE_CALIB_POINT_WIRE_SIZE) {
    return null;
  }
  const points = [];
  let offset = 4;
  for (let i = 0; i < count; i += 1) {
    const mass = readF32LE(payload, offset + 4);
    const nominal = readF32LE(payload, offset + 8);
    if (!mass.ok || !nominal.ok || payload[offset + 2] > 2) {
      return null;
    }
    points.push({
      adcNet: payload.readUInt16LE(offset),
      direction: payload[offset + 2],
      massG: mass.value,
      nominalG: nominal.value,
    });
    offset += FORCE_CALIB_POINT_WIRE_SIZE;
  }
  return { source: payload[0], confirm: payload[1] === 1, commit: payload[2] === 1, points };
}

function encodeSelectForceCurvePayload(command) {
  const out = Buffer.alloc(SELECT_FORCE_CURVE_WIRE_SIZE);
  out[0] = command.source & 0xff;
  out[1] = command.confirm ? 1 : 0;
  out[2] = command.unit & 0xff;
  out[3] = command.commit ? 1 : 0;
  if (
    !writeF32LE(out, 4, command.beta0 || 0) ||
    !writeF32LE(out, 8, command.beta1 || 0) ||
    !writeF32LE(out, 12, command.beta2 || 0)
  ) {
    return null;
  }
  return out;
}

function decodeSelectForceCurvePayload(payload) {
  if (!Buffer.isBuffer(payload) || payload.length !== SELECT_FORCE_CURVE_WIRE_SIZE) {
    return null;
  }
  if (payload[0] > 3 || payload[1] > 1 || payload[2] > 1 || payload[3] > 1) {
    return null;
  }
  const beta0 = readF32LE(payload, 4);
  const beta1 = readF32LE(payload, 8);
  const beta2 = readF32LE(payload, 12);
  if (!beta0.ok || !beta1.ok || !beta2.ok) {
    return null;
  }
  return {
    source: payload[0],
    confirm: payload[1] === 1,
    unit: payload[2],
    commit: payload[3] === 1,
    beta0: beta0.value,
    beta1: beta1.value,
    beta2: beta2.value,
  };
}

function encodeRawAdcPayload(sample) {
  const out = Buffer.alloc(RAW_ADC_WIRE_SIZE);
  out.writeUInt16LE(sample.x & 0xffff, 0);
  out.writeUInt16LE(sample.y & 0xffff, 2);
  out.writeUInt16LE(sample.z & 0xffff, 4);
  out[6] = sample.valid ? 1 : 0;
  return out;
}

function decodeRawAdcPayload(payload) {
  if (!Buffer.isBuffer(payload) || payload.length !== RAW_ADC_WIRE_SIZE || payload[6] > 1) {
    return null;
  }
  return {
    x: payload.readUInt16LE(0),
    y: payload.readUInt16LE(2),
    z: payload.readUInt16LE(4),
    valid: payload[6] === 1,
  };
}

function encodeForceCalibrationResultPayload(result) {
  const count = result.referenceForceN ? result.referenceForceN.length : 0;
  if (count > FORCE_CALIB_MAX_POINTS) {
    return null;
  }
  const out = Buffer.alloc(FORCE_CALIB_RESULT_HEADER_WIRE_SIZE + count * 4);
  out[0] = result.requestedSource & 0xff;
  out[1] = result.resolvedSource & 0xff;
  out[2] = result.committed ? 1 : 0;
  out[3] = result.reason & 0xff;
  out[4] = result.degree & 0xff;
  out[5] = result.needsConfirmation ? 1 : 0;
  out[6] = result.worstIndex == null ? 0xff : result.worstIndex & 0xff;
  out[7] = count;
  let offset = 8;
  for (const key of FORCE_RESULT_FLOATS) {
    if (!writeF32LE(out, offset, result[key] || 0)) {
      return null;
    }
    offset += 4;
  }
  for (const force of result.referenceForceN || []) {
    if (!writeF32LE(out, offset, force)) {
      return null;
    }
    offset += 4;
  }
  return out;
}

function decodeForceCalibrationResultPayload(payload) {
  if (!Buffer.isBuffer(payload) || payload.length < FORCE_CALIB_RESULT_HEADER_WIRE_SIZE) {
    return null;
  }
  const count = payload[7];
  if (payload[2] > 1 || payload[5] > 1 || count > FORCE_CALIB_MAX_POINTS) {
    return null;
  }
  if (payload.length !== FORCE_CALIB_RESULT_HEADER_WIRE_SIZE + count * 4) {
    return null;
  }
  const result = {
    requestedSource: payload[0],
    resolvedSource: payload[1],
    committed: payload[2] === 1,
    reason: payload[3],
    degree: payload[4],
    needsConfirmation: payload[5] === 1,
    worstIndex: payload[6] === 0xff ? null : payload[6],
    referenceForceN: [],
  };
  let offset = 8;
  for (const key of FORCE_RESULT_FLOATS) {
    const value = readF32LE(payload, offset);
    if (!value.ok) {
      return null;
    }
    result[key] = value.value;
    offset += 4;
  }
  for (let i = 0; i < count; i += 1) {
    const force = readF32LE(payload, offset);
    if (!force.ok) {
      return null;
    }
    result.referenceForceN.push(force.value);
    offset += 4;
  }
  return result;
}

function encodeSetForceCalibrationMessage(command) {
  const payload = encodeSetForceCalibrationPayload(command);
  if (!payload) {
    return { ok: false, error: 'InvalidPayload' };
  }
  return encodeFrame(MessageId.CmdSetForceCalibration, payload);
}

function encodeSelectForceCurveMessage(command) {
  const payload = encodeSelectForceCurvePayload(command);
  if (!payload) {
    return { ok: false, error: 'InvalidPayload' };
  }
  return encodeFrame(MessageId.CmdSelectForceCurve, payload);
}

module.exports = {
  encodeFrame,
  encodeEmptyMessage,
  encodeMoveToMessage,
  encodeBoolMessage,
  encodeLoadProgramPayload,
  encodeLoadProgramMessage,
  encodeMachineConfigMessage,
  encodeTestConfigurationMessage,
  encodePlateOriginMessage,
  encodeProbeCenterMessage,
  encodeRestoreMcsMessage,
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
  encodeMachineConfigPayload,
  decodeMachineConfigPayload,
  encodeTestConfigurationPayload,
  decodeTestConfigurationPayload,
  encodeForcePathChunkPayload,
  decodeForcePathChunkPayload,
  encodeSetForceCalibrationMessage,
  encodeSelectForceCurveMessage,
  decodeRawAdcPayload,
  decodeForceCalibrationResultPayload,
};
