'use strict';

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const path = require('path');

describe('resultsStore', () => {
  it('exposes history API and persists to data file path', () => {
    const store = require('./resultsStore');
    assert.equal(typeof store.pushResults, 'function');
    assert.equal(typeof store.getHistory, 'function');
    assert.equal(typeof store.getByIndex, 'function');
    assert.ok(Array.isArray(store.getHistory()));
    assert.equal(store.MAX_HISTORY, 3);
    assert.ok(String(store.FILE_PATH).endsWith(path.join('data', 'results-history.json')));

    const marker = {
      programName: `__unit_test_${Date.now()}`,
      completedAt: new Date().toISOString(),
      processState: 'Completed',
      analysis: { points: [], curves: [] },
    };
    const before = store.getHistory().map((r) => r);
    store.pushResults(marker);
    assert.equal(store.getResults().programName, marker.programName);
    assert.ok(fs.existsSync(store.FILE_PATH));

    // Restore prior history without leaving the test marker as newest forever.
    store.clear();
    for (let i = before.length - 1; i >= 0; i -= 1) {
      store.pushResults(before[i]);
    }
  });
});
