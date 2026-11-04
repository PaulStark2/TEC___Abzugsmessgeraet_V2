'use strict';

/**
 * Results store — last N procedure runs (newest first).
 * Persisted under gui/server/data/results-history.json so history survives
 * GUI restarts and does not depend on a loaded program.
 */

const fs = require('fs');
const path = require('path');

const MAX_HISTORY = 3;
const FILE_PATH = path.join(__dirname, '..', 'data', 'results-history.json');

/** @type {Array<object>} */
let runs = [];

function ensureDataDir() {
  const dir = path.dirname(FILE_PATH);
  if (!fs.existsSync(dir)) {
    fs.mkdirSync(dir, { recursive: true });
  }
}

function persist() {
  try {
    ensureDataDir();
    const payload = {
      version: 1,
      savedAt: new Date().toISOString(),
      runs,
    };
    fs.writeFileSync(FILE_PATH, `${JSON.stringify(payload)}\n`, 'utf8');
  } catch (err) {
    console.warn(
      '[resultsStore] persist failed:',
      err && err.message ? err.message : err
    );
  }
}

function loadFromDisk() {
  try {
    if (!fs.existsSync(FILE_PATH)) {
      runs = [];
      return;
    }
    const raw = fs.readFileSync(FILE_PATH, 'utf8');
    const parsed = JSON.parse(raw);
    const list = Array.isArray(parsed)
      ? parsed
      : parsed && Array.isArray(parsed.runs)
        ? parsed.runs
        : [];
    runs = list.filter((r) => r && typeof r === 'object').slice(0, MAX_HISTORY);
  } catch (err) {
    console.warn(
      '[resultsStore] load failed:',
      err && err.message ? err.message : err
    );
    runs = [];
  }
}

function clear() {
  runs = [];
  persist();
}

function setResults(results) {
  if (!results) {
    runs = [];
    persist();
    return;
  }
  runs = [results];
  persist();
}

function pushResults(results) {
  if (!results) return;
  runs = [results, ...runs].slice(0, MAX_HISTORY);
  persist();
}

function getResults() {
  return runs.length > 0 ? runs[0] : null;
}

function getHistory() {
  return runs.map((r) => r);
}

function getByIndex(index) {
  const i = Number(index);
  if (!Number.isFinite(i) || i < 0 || i >= runs.length) {
    return null;
  }
  return runs[i];
}

loadFromDisk();

module.exports = {
  clear,
  setResults,
  pushResults,
  getResults,
  getHistory,
  getByIndex,
  MAX_HISTORY,
  FILE_PATH,
};
