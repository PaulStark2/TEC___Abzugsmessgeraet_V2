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
} = require('./m12Codec');
const {
  MessageId,
  DecodeResult,
  AckResultCode,
  NackErrorCode,
  STATUS_REPORT_PAYLOAD_WIRE_SIZE,
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
  };
  const payload = encodeStatusReportPayload(status);
  assert.strictEqual(payload.length, STATUS_REPORT_PAYLOAD_WIRE_SIZE);
  const frame = encodeFrame(MessageId.MsgStatusReport, payload);
  const decoded = decodeFrame(frame.frame);
  assert.strictEqual(decoded.result, DecodeResult.Ok);
  const report = decodeStatusReportPayload(decoded.frame.payload);
  assert.ok(nearEqual(report.x, 3));
  assert.ok(nearEqual(report.fx, 0.1));
  const gui = statusReportToGuiStatus(report, true);
  assert.strictEqual(gui.machineState, 'Active');
  assert.strictEqual(gui.motionState, 'Moving');
  assert.strictEqual(gui.pneumaticExtended, true);
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
console.log('m12Codec.test.js: PASS');
