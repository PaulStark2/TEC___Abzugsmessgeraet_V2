'use strict';

const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');

// Isolate store file by pointing FILE_PATH via monkey-patch after require is awkward;
// use the real store against a temp copy of the module pattern: record/list/ack.

const messageStore = require('./messageStore');

function run() {
  messageStore.pruneNow();
  const before = messageStore.list({ limit: 500 }).length;

  const row = messageStore.record({
    severity: 'error',
    code: 'Gui.CommandRejected',
    title: 'Testfehler',
    detail: 'Nur Unit-Test',
    source: 'gui',
  });
  assert.ok(row.id);
  assert.strictEqual(row.active, true);

  const summary = messageStore.getSummary();
  assert.ok(summary.activeCount >= 1);
  assert.ok(summary.topTitle);

  const acked = messageStore.ack(row.id);
  assert.ok(acked);
  assert.strictEqual(acked.active, false);
  assert.ok(acked.ackedAt);

  const afterAck = messageStore.getSummary();
  assert.ok(afterAck.activeCount >= 0);

  const listed = messageStore.list({ limit: 50 });
  assert.ok(listed.some((m) => m.id === row.id));

  // History entry still present after ack
  assert.ok(listed.find((m) => m.id === row.id && m.ackedAt));

  const abortRow = messageStore.record({
    severity: 'error',
    code: 'Abort.SoftStop',
    abortReasonOrdinal: 37,
    source: 'core',
  });
  assert.strictEqual(abortRow.active, true);
  assert.ok(messageStore.getSummary().activeCount >= 1);
  const cleared = messageStore.clearActiveByCodePrefix('Abort.');
  assert.ok(cleared >= 1);
  assert.strictEqual(messageStore.getById(abortRow.id).active, false);
  assert.strictEqual(messageStore.getSummary().activeCount, 0);
  assert.strictEqual(messageStore.getSummary().topTitle, null);

  console.log(`messageStore.test.js: PASS (entries before=${before}, after list=${listed.length})`);
}

run();
