'use strict';

const assert = require('assert');
const { createGuiSession, isSessionExemptMutation } = require('./guiSession');
const {
  evaluateShutdown,
  SHUTDOWN_BLOCKED_MESSAGE,
  SHUTDOWN_UNAVAILABLE_MESSAGE,
} = require('./shutdownPolicy');

function testLogin() {
  const session = createGuiSession({ GUI_USERNAME: 'bediener', GUI_PASSWORD: 'geheim' });
  assert.strictEqual(session.isAuthenticated(), false);
  assert.strictEqual(session.login('bediener', 'falsch'), false);
  assert.strictEqual(session.isAuthenticated(), false);
  assert.strictEqual(session.login('fremd', 'geheim'), false);
  assert.strictEqual(session.login('bediener', 'geheim'), true);
  assert.strictEqual(session.isAuthenticated(), true);
  session.logout();
  assert.strictEqual(session.isAuthenticated(), false);
}

function testExemptMutations() {
  assert.strictEqual(isSessionExemptMutation('GET', '/api/session'), true);
  assert.strictEqual(isSessionExemptMutation('POST', '/api/session/login'), true);
  assert.strictEqual(isSessionExemptMutation('POST', '/api/system/shutdown'), true);
  assert.strictEqual(isSessionExemptMutation('POST', '/api/simulation/estop'), true);
  assert.strictEqual(isSessionExemptMutation('POST', '/api/command'), false);
  assert.strictEqual(isSessionExemptMutation('POST', '/api/procedure/start'), false);
  assert.strictEqual(isSessionExemptMutation('PUT', '/api/machine-config'), false);
  assert.strictEqual(isSessionExemptMutation('DELETE', '/api/program/current'), false);
}

function testShutdownPolicy() {
  assert.deepStrictEqual(evaluateShutdown({ allowSystemPower: true, procedureState: 'running' }), {
    ok: false,
    status: 409,
    error: SHUTDOWN_BLOCKED_MESSAGE,
  });
  assert.deepStrictEqual(evaluateShutdown({ allowSystemPower: true, procedureState: 'paused' }), {
    ok: false,
    status: 409,
    error: SHUTDOWN_BLOCKED_MESSAGE,
  });
  assert.deepStrictEqual(evaluateShutdown({ allowSystemPower: false, procedureState: 'idle' }), {
    ok: false,
    status: 403,
    error: SHUTDOWN_UNAVAILABLE_MESSAGE,
  });
  assert.deepStrictEqual(evaluateShutdown({ allowSystemPower: true, procedureState: 'idle' }), {
    ok: true,
  });
  assert.deepStrictEqual(evaluateShutdown({ allowSystemPower: true, procedureState: 'completed' }), {
    ok: true,
  });
}

testLogin();
testExemptMutations();
testShutdownPolicy();
console.log('guiSession.test.js: ok');
