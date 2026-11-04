'use strict';

const assert = require('assert');
const {
  encodeFrame,
  encodeEmptyMessage,
  encodeMoveToMessage,
  encodeBoolMessage,
  encodeLoadProgramMessage,
  encodeLoadProgramPayload,
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
  encodeMachineConfigPayload,
  decodeMachineConfigPayload,
  encodeTestConfigurationPayload,
  decodeTestConfigurationPayload,
  encodeForcePathChunkPayload,
  decodeForcePathChunkPayload,
} = require('./m12Codec');
const {
  MessageId,
  DecodeResult,
  AckResultCode,
  NackErrorCode,
  STATUS_REPORT_PAYLOAD_WIRE_SIZE,
  MACHINE_CONFIG_PAYLOAD_WIRE_SIZE,
  NO_CURRENT_POINT_INDEX,
} = require('./protocolTypes');
const { crc16CcittFalse } = require('./crc16');

function nearEqual(a, b, eps = 1e-5) {
  return Math.abs(a - b) <= eps;
}

function testMoveToRoundtrip() {
  const encoded = encodeMoveToMessage(1.5, -2.25, 10);
  assert.ok(encoded.ok);
  const decoded = decodeFrame(encoded.frame);
  assert.strictEqual(decoded.result, DecodeResult.Ok);
  assert.strictEqual(decoded.frame.messageId, MessageId.CmdMoveTo);
  const payload = decodeMoveToPayload(decoded.frame.payload);
  assert.ok(nearEqual(payload.x, 1.5));
  assert.ok(nearEqual(payload.y, -2.25));
  assert.ok(nearEqual(payload.z, 10));
}

function testBoolRoundtrip() {
  const encoded = encodeBoolMessage(MessageId.CmdSetSpreader, true);
  assert.ok(encoded.ok);
  const decoded = decodeFrame(encoded.frame);
  assert.strictEqual(decoded.result, DecodeResult.Ok);
  const payload = decodeBoolPayload(decoded.frame.payload);
  assert.strictEqual(payload.value, 1);
}

function testEmptyCommand() {
  const encoded = encodeEmptyMessage(MessageId.CmdPause);
  assert.ok(encoded.ok);
  const decoded = decodeFrame(encoded.frame);
  assert.strictEqual(decoded.result, DecodeResult.Ok);
  assert.strictEqual(decoded.frame.payloadLength, 0);
}

function testAckNack() {
  const ackPayload = encodeAckPayload({
    refMessageId: MessageId.CmdMoveTo,
    resultCode: AckResultCode.Ok,
  });
  const ackFrame = encodeFrame(MessageId.RspAck, ackPayload);
  assert.ok(ackFrame.ok);
  const ackDecoded = decodeFrame(ackFrame.frame);
  assert.strictEqual(ackDecoded.result, DecodeResult.Ok);
  const ack = decodeAckPayload(ackDecoded.frame.payload);
  assert.strictEqual(ack.refMessageId, MessageId.CmdMoveTo);
  assert.strictEqual(ack.resultCode, AckResultCode.Ok);

  const nackPayload = encodeNackPayload({
    refMessageId: MessageId.CmdMoveTo,
    errorCode: NackErrorCode.InvalidPayload,
    detail: 0,
  });
  const nackFrame = encodeFrame(MessageId.RspNack, nackPayload);
  const nackDecoded = decodeFrame(nackFrame.frame);
  const nack = decodeNackPayload(nackDecoded.frame.payload);
  assert.strictEqual(nack.errorCode, NackErrorCode.InvalidPayload);
}

function testStatusDecode() {
  const status = {
    machineState: 1,
    motionState: 1,
    processState: 0,
    x: 3,
    y: 4,
    z: 5,
    fx: 0.1,
    fy: 0.2,
    fz: 0.3,
    limits: 0,
    eStop: 0,
    spreaderExtend: 1,
    spreaderExtended: 1,
    fixtureExtend: 0,
    fixtureExtended: 0,
    currentPointIndex: NO_CURRENT_POINT_INDEX,
    programPointCount: 0,
    homingState: 0,
    blockedDirections: 0,
    flags: 0,
    procedurePhase: 0,
    faultReason: 0,
    spreaderState: 2,
    faultManual: 0,
  };
  const payload = encodeStatusReportPayload(status);
  assert.strictEqual(payload.length, STATUS_REPORT_PAYLOAD_WIRE_SIZE);
  const frame = encodeFrame(MessageId.MsgStatusReport, payload);
  const decoded = decodeFrame(frame.frame);
  assert.strictEqual(decoded.result, DecodeResult.Ok);
  const report = decodeStatusReportPayload(decoded.frame.payload);
  assert.ok(nearEqual(report.x, 3));
  assert.ok(nearEqual(report.fx, 0.1));
  assert.strictEqual(report.spreaderState, 2);
  assert.strictEqual(report.faultManual, 0);
  const gui = statusReportToGuiStatus(report, true);
  assert.strictEqual(gui.machineState, 'Active');
  assert.strictEqual(gui.motionState, 'Moving');
  assert.strictEqual(gui.pneumaticExtended, true);
  assert.strictEqual(gui.spreaderState, 'Extended');
  assert.strictEqual(gui.faultManual, false);
  assert.strictEqual(gui.currentPointIndex, null);
  assert.strictEqual(gui.connected, true);
}

function testCrcError() {
  const encoded = encodeMoveToMessage(1, 2, 3);
  const bad = Buffer.from(encoded.frame);
  bad[bad.length - 1] ^= 0xff;
  const decoded = decodeFrame(bad);
  assert.strictEqual(decoded.result, DecodeResult.CrcError);
}

function testPartialNeedMore() {
  const encoded = encodeEmptyMessage(MessageId.CmdStop);
  const partial = encoded.frame.subarray(0, 4);
  const decoded = decodeFrame(partial);
  assert.strictEqual(decoded.result, DecodeResult.NeedMoreData);
}

function testCrcMatchesKnownPattern() {
  // Empty STOP frame CRC over header only.
  const header = Buffer.from([0x54, 0x45, 0x01, MessageId.CmdStop, 0x00, 0x00]);
  const crc = crc16CcittFalse(header);
  const encoded = encodeEmptyMessage(MessageId.CmdStop);
  const wireCrc = encoded.frame[6] | (encoded.frame[7] << 8);
  assert.strictEqual(wireCrc, crc);
}

function testPayloadHelpers() {
  const move = encodeMoveToPayload({ x: 1, y: 2, z: 3 });
  assert.deepStrictEqual(decodeMoveToPayload(move), { x: 1, y: 2, z: 3 });
  assert.deepStrictEqual(decodeBoolPayload(encodeBoolPayload(false)), { value: 0 });
}

function testLoadProgramEncode() {
  const payload = encodeLoadProgramPayload([
    { x: 1, y: 2, z: 3 },
    { x: 4, y: 5, z: 6 },
  ]);
  assert.ok(payload);
  assert.strictEqual(payload[0], 2);
  assert.strictEqual(payload.length, 1 + 2 * 12);
  const encoded = encodeLoadProgramMessage([
    { x: 1, y: 2, z: 3 },
    { x: 4, y: 5, z: 6 },
  ]);
  assert.ok(encoded.ok);
  const decoded = decodeFrame(encoded.frame);
  assert.strictEqual(decoded.result, DecodeResult.Ok);
  assert.strictEqual(decoded.frame.messageId, MessageId.CmdLoadProgram);
  assert.strictEqual(decoded.frame.payload[0], 2);
}

testMoveToRoundtrip();
testBoolRoundtrip();
testEmptyCommand();
testAckNack();
testStatusDecode();
testCrcError();
testPartialNeedMore();
testCrcMatchesKnownPattern();
testPayloadHelpers();
testLoadProgramEncode();

function sampleMachineConfig() {
  return {
    v_approach_testpoints: 10,
    v_fine_testpoints: 2,
    v_unclamp: 1,
    v_homing: 5,
    a_testpoints: 0,
    corner_testpoints: 0,
    a_homing: 0,
    force_filter: 0,
    xy_tolerance_N: 0.5,
    xy_correction_step: 0.05,
    xy_max_correction: 2,
    insertion_buffer: 1,
    after_seat_retract_mm: 2,
    extract_after_free_mm: 0,
    extract_after_free_v: 1,
    collision_approach_N: 20,
    collision_retract_N: 20,
    collision_clamp_N: 20,
    collision_unclamp_N: 20,
    collision_contact_N: 40,
    collision_measure_N: 150,
    spreader_extend_time_s: 0.5,
    spreader_retract_time_s: 0.5,
    force_tolerance_N: 0.5,
    v_search: 2,
    home_offset_x: 0,
    home_offset_y: 0,
    home_offset_z: 0,
    tool_offset_x: 0.25,
    tool_offset_y: -0.5,
    tool_offset_z: 1,
    safe_z_buffer: 30,
    safe_z_calib_buffer: 30,
    sample_timeout_s: 0.5,
    heartbeat_timeout_s: 0.3,
    xy_correct_timeout_s: 2,
    approach_distance: 15,
    probe_v_approach: 10,
    probe_wall_force_N: 0.5,
    probe_floor_force_N: 0.5,
    probe_xy_search_limit: 5,
    probe_z_search_limit: 20,
    probe_z_lift_mm: 2,
    probe_bore_timeout_s: 60,
    probe_bore_refine_passes: 1,
    insert_search_distance: 10,
    phase1_v_extract: 1,
    phase1_v_insert: 1,
    phase1_a: 0,
    phase1_corner: 0,
    phase3_v_extract: 2,
    phase3_v_insert: 2,
    phase3_a: 0,
    phase3_corner: 0,
    probe_center_x: 0,
    probe_center_y: 0,
    probe_center_z: 0,
  };
}

function testMachineConfigRoundtrip() {
  const payload = encodeMachineConfigPayload(sampleMachineConfig());
  assert.ok(payload);
  assert.strictEqual(payload.length, MACHINE_CONFIG_PAYLOAD_WIRE_SIZE);
  const decoded = decodeMachineConfigPayload(payload);
  assert.ok(nearEqual(decoded.tool_offset_z, 1));
  assert.ok(nearEqual(decoded.safe_z_buffer, 30));
  assert.ok(nearEqual(decoded.safe_z_calib_buffer, 30));
  assert.ok(nearEqual(decoded.heartbeat_timeout_s, 0.3));
  assert.ok(nearEqual(decoded.probe_wall_force_N, 0.5));
  assert.ok(nearEqual(decoded.probe_z_lift_mm, 2));
  assert.ok(nearEqual(decoded.probe_xy_search_limit, 5));
  assert.ok(nearEqual(decoded.insert_search_distance, 10));
  assert.strictEqual(decoded.probe_bore_refine_passes, 1);
  assert.ok(nearEqual(decoded.phase3_v_insert, 2));
  assert.ok(nearEqual(decoded.corner_testpoints, 0));
  assert.ok(nearEqual(decoded.probe_v_approach, 10));
  assert.ok(nearEqual(decoded.approach_distance, 15));
  assert.ok(nearEqual(decoded.after_seat_retract_mm, 2));
  assert.ok(nearEqual(decoded.extract_after_free_mm, 0));
  assert.ok(nearEqual(decoded.extract_after_free_v, 1));
  assert.ok(nearEqual(decoded.v_homing_fast, 18));
  assert.ok(nearEqual(decoded.homing_approach_distance, 50));
  assert.ok(nearEqual(decoded.a_homing_fast, 500));
  assert.ok(nearEqual(decoded.force_path_dz_mm, 0.01));
  assert.ok(nearEqual(decoded.force_path_dz_fine_mm, 0.005));
  assert.ok(nearEqual(decoded.force_near_target_frac, 0.8));
  assert.ok(nearEqual(decoded.phase3_v_insert_fine, 0.5));
  assert.ok(nearEqual(decoded.force_range_N, 30));
  assert.ok(nearEqual(decoded.force_hard_limit_N, 30));
  assert.ok(nearEqual(decoded.force_hard_limit_x_N, 30));
  assert.ok(nearEqual(decoded.force_hard_limit_y_N, 30));
  assert.ok(nearEqual(decoded.collision_approach_x_N, 20));
  assert.ok(nearEqual(decoded.a_manual, 500));
  assert.ok(nearEqual(decoded.coord_tolerance_mm, 0.5));
  assert.ok(nearEqual(decoded.test_lateral_force_N, 5));
  assert.strictEqual(decoded.anomaly_skip, 0);
  const frame = encodeFrame(MessageId.CmdSetMachineConfig, payload);
  assert.ok(frame.ok);
  const parsed = decodeFrame(frame.frame);
  assert.strictEqual(parsed.result, DecodeResult.Ok);
  const getFrame = encodeEmptyMessage(MessageId.CmdGetMachineConfig);
  assert.ok(getFrame.ok);
  assert.strictEqual(decodeFrame(getFrame.frame).result, DecodeResult.Ok);
  const invFrame = encodeEmptyMessage(MessageId.CmdInvalidateHoming);
  assert.ok(invFrame.ok);
  assert.strictEqual(decodeFrame(invFrame.frame).result, DecodeResult.Ok);
}

function testTestConfigurationRoundtrip() {
  const payload = encodeTestConfigurationPayload({
    mode: 1,
    target_force_N: 12.5,
    iterations: 7,
    active_point_index: 3,
  });
  assert.ok(payload);
  const decoded = decodeTestConfigurationPayload(payload);
  assert.strictEqual(decoded.mode, 1);
  assert.strictEqual(decoded.iterations, 7);
  assert.ok(nearEqual(decoded.target_force_N, 12.5));
  assert.strictEqual(encodeTestConfigurationPayload({
    mode: 2,
    target_force_N: 1,
    iterations: 1,
    active_point_index: 0,
  }), null);
}

testMachineConfigRoundtrip();
testTestConfigurationRoundtrip();

function testForcePathChunkRoundtrip() {
  const payload = encodeForcePathChunkPayload({
    pointIndex: 2,
    iteration: 3,
    sequence: 9,
    sampleCount: 2,
    dropped: 4,
    leg: 1,
    z: [1.25, 1.5],
    fz: [8.5, 9],
  });
  assert.ok(payload);
  const decoded = decodeForcePathChunkPayload(payload);
  assert.strictEqual(decoded.pointIndex, 2);
  assert.strictEqual(decoded.iteration, 3);
  assert.strictEqual(decoded.dropped, 4);
  assert.strictEqual(decoded.leg, 1);
  assert.ok(nearEqual(decoded.z[1], 1.5));
  const frame = encodeFrame(MessageId.MsgForcePathChunk, payload);
  assert.strictEqual(decodeFrame(frame.frame).result, DecodeResult.Ok);
  assert.strictEqual(encodeForcePathChunkPayload({ sampleCount: 33, leg: 0, z: [], fz: [] }), null);
  assert.strictEqual(
    encodeForcePathChunkPayload({ sampleCount: 1, leg: 2, z: [0], fz: [0] }),
    null
  );
}

function testStatusReportProcedurePhase() {
  const payload = encodeStatusReportPayload({
    machineState: 0,
    motionState: 0,
    processState: 1,
    x: 0,
    y: 0,
    z: 0,
    fx: 0,
    fy: 0,
    fz: 0,
    limits: 0,
    eStop: 0,
    spreaderExtend: 0,
    spreaderExtended: 0,
    fixtureExtend: 0,
    fixtureExtended: 0,
    currentPointIndex: NO_CURRENT_POINT_INDEX,
    programPointCount: 0,
    homingState: 0,
    blockedDirections: 0,
    flags: 0,
    procedurePhase: 11,
    faultReason: 7,
    spreaderState: 4,
    faultManual: 1,
    abortReason: 20,
  });
  assert.strictEqual(payload.length, STATUS_REPORT_PAYLOAD_WIRE_SIZE);
  assert.strictEqual(STATUS_REPORT_PAYLOAD_WIRE_SIZE, 46);
  const decoded = decodeStatusReportPayload(payload);
  assert.strictEqual(decoded.procedurePhase, 11);
  assert.strictEqual(decoded.faultReason, 7);
  assert.strictEqual(decoded.spreaderState, 4);
  assert.strictEqual(decoded.faultManual, 1);
  assert.strictEqual(decoded.abortReason, 20);
  assert.strictEqual(decoded.currentIteration || 0, 0);
  const gui = statusReportToGuiStatus(decoded, true);
  assert.strictEqual(gui.procedurePhase, 'TestOrAge');
  assert.strictEqual(gui.faultReason, 'CommunicationTimeout');
  assert.strictEqual(gui.spreaderState, 'SafeDeenergized');
  assert.strictEqual(gui.faultManual, true);
  assert.strictEqual(gui.abortReason, 20);
  assert.strictEqual(gui.abortReasonOrdinal, 20);
  assert.strictEqual(gui.currentIteration, 0);
}

testForcePathChunkRoundtrip();
testStatusReportProcedurePhase();
console.log('m12Codec.test.js: PASS');
