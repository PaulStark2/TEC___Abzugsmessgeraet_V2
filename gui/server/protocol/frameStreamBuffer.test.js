'use strict';

const assert = require('assert');
const { FrameStreamBuffer, MAX_BUFFER_SIZE } = require('./frameStreamBuffer');
const { encodeMoveToMessage, encodeEmptyMessage } = require('./m12Codec');
const { MessageId, DecodeResult, MAX_FRAME_SIZE } = require('./protocolTypes');

function testPartialThenComplete() {
  const encoded = encodeMoveToMessage(1, 2, 3);
  const buffer = new FrameStreamBuffer();
  buffer.append(encoded.frame.subarray(0, Math.floor(encoded.frame.length / 2)));
  assert.strictEqual(buffer.tryPopFrame().ok, false);
  buffer.append(encoded.frame.subarray(Math.floor(encoded.frame.length / 2)));
  const popped = buffer.tryPopFrame();
  assert.ok(popped.ok);
  assert.strictEqual(popped.decodeResult, DecodeResult.Ok);
  assert.strictEqual(popped.frame.messageId, MessageId.CmdMoveTo);
}

function testTwoFrames() {
  const a = encodeMoveToMessage(1, 0, 0);
  const b = encodeMoveToMessage(2, 0, 0);
  const buffer = new FrameStreamBuffer();
  buffer.append(Buffer.concat([a.frame, b.frame]));
  const first = buffer.tryPopFrame();
  const second = buffer.tryPopFrame();
  assert.ok(first.ok && second.ok);
  assert.strictEqual(buffer.size(), 0);
}

function testFramePlusPartial() {
  const a = encodeEmptyMessage(MessageId.CmdPause);
  const b = encodeEmptyMessage(MessageId.CmdResume);
  const buffer = new FrameStreamBuffer();
  buffer.append(a.frame);
  buffer.append(b.frame.subarray(0, 3));
  assert.ok(buffer.tryPopFrame().ok);
  assert.strictEqual(buffer.tryPopFrame().ok, false);
  buffer.append(b.frame.subarray(3));
  assert.ok(buffer.tryPopFrame().ok);
}

function testCrcErrorThenGood() {
  const badEncoded = encodeMoveToMessage(1, 0, 0);
  const bad = Buffer.from(badEncoded.frame);
  bad[bad.length - 1] ^= 0xff;
  const good = encodeMoveToMessage(2, 0, 0);
  const buffer = new FrameStreamBuffer();
  buffer.append(Buffer.concat([bad, good.frame]));
  const popped = buffer.tryPopFrame();
  assert.ok(popped.ok);
  assert.strictEqual(popped.decodeResult, DecodeResult.Ok);
  assert.strictEqual(popped.frame.messageId, MessageId.CmdMoveTo);
}

function testGarbageThenResync() {
  const good = encodeMoveToMessage(3, 0, 0);
  const buffer = new FrameStreamBuffer();
  buffer.append(Buffer.from([0x00, 0xff, 0x11, 0x22]));
  buffer.append(good.frame);
  const popped = buffer.tryPopFrame();
  assert.ok(popped.ok);
  assert.strictEqual(popped.decodeResult, DecodeResult.Ok);
}

function testOversize() {
  const buffer = new FrameStreamBuffer();
  buffer.append(Buffer.alloc(MAX_BUFFER_SIZE + 1, 0x54));
  assert.ok(buffer.overflowed());
  assert.strictEqual(buffer.size(), 0);
}

function testNeedMoreData() {
  const encoded = encodeEmptyMessage(MessageId.CmdStop);
  const buffer = new FrameStreamBuffer();
  buffer.append(encoded.frame.subarray(0, 5));
  assert.strictEqual(buffer.tryPopFrame().ok, false);
  assert.ok(buffer.size() > 0);
}

testPartialThenComplete();
testTwoFrames();
testFramePlusPartial();
testCrcErrorThenGood();
testGarbageThenResync();
testOversize();
testNeedMoreData();
console.log('frameStreamBuffer.test.js: PASS');
