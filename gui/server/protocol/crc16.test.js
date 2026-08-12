'use strict';

const assert = require('assert');
const { crc16CcittFalse } = require('./crc16');

function testKnownAnswer() {
  const crc = crc16CcittFalse(Buffer.from('123456789', 'ascii'));
  assert.strictEqual(crc, 0x29b1);
}

function testEmpty() {
  assert.strictEqual(crc16CcittFalse(Buffer.alloc(0)), 0xffff);
}

function testDeterministic() {
  const a = crc16CcittFalse(Buffer.from([0x54, 0x45, 0x01, 0x10, 0x00, 0x00]));
  const b = crc16CcittFalse(Buffer.from([0x54, 0x45, 0x01, 0x10, 0x00, 0x00]));
  assert.strictEqual(a, b);
}

testKnownAnswer();
testEmpty();
testDeterministic();
console.log('crc16.test.js: PASS');
