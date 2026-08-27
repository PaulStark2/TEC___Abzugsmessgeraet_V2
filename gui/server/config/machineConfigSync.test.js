'use strict';

const assert = require('assert');
const {
  compareCriticalMachineConfig,
  CRITICAL_CONFIG_KEYS,
} = require('./machineConfigSync');

function testMatch() {
  const a = { a_testpoints: 2, corner_testpoints: 25, v_homing: 5 };
  const b = { a_testpoints: 2, corner_testpoints: 25, v_homing: 5 };
  const cmp = compareCriticalMachineConfig(a, b);
  assert.strictEqual(cmp.drift, false);
  assert.deepStrictEqual(cmp.mismatchedKeys, []);
}

function testDrift() {
  const a = { a_testpoints: 2, corner_testpoints: 25 };
  const b = { a_testpoints: 0, corner_testpoints: 25 };
  const cmp = compareCriticalMachineConfig(a, b);
  assert.strictEqual(cmp.drift, true);
  assert.deepStrictEqual(cmp.mismatchedKeys, ['a_testpoints']);
}

function testMissingKeysIgnored() {
  const a = { a_testpoints: 2 };
  const b = { corner_testpoints: 1 };
  const cmp = compareCriticalMachineConfig(a, b);
  assert.strictEqual(cmp.drift, false);
}

assert.ok(CRITICAL_CONFIG_KEYS.includes('a_testpoints'));
testMatch();
testDrift();
testMissingKeysIgnored();
console.log('machineConfigSync.test.js: ok');
