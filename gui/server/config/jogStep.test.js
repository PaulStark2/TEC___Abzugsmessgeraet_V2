'use strict';

/**
 * Unit tests for jog step raster helpers.
 */

const assert = require('assert');
const { snapJogStepMm, isJogStepAllowed } = require('./jogStep');

assert.strictEqual(snapJogStepMm(0.1, 0.1), 0.1);
assert.strictEqual(snapJogStepMm(0.05, 0.1), 0.1);
assert.strictEqual(snapJogStepMm(0.24, 0.1), 0.2);
assert.strictEqual(snapJogStepMm(0.26, 0.1), 0.3);
assert.strictEqual(snapJogStepMm('1.5', 0.5), 1.5);
assert.strictEqual(snapJogStepMm(-1, 0.1), 0.1);
assert.strictEqual(snapJogStepMm(NaN, 0.1), 0.1);

assert.strictEqual(isJogStepAllowed(0.2, 0.1), true);
assert.strictEqual(isJogStepAllowed(0.15, 0.1), false);
assert.strictEqual(isJogStepAllowed(0.05, 0.1), false);
assert.strictEqual(isJogStepAllowed(1, 0.1), true);

console.log('jogStep.test.js: ok');
