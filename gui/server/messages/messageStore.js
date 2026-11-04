'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { resolveMessage } = require('./messageCatalog');

const FILE_PATH = path.join(__dirname, '..', 'data', 'messages.jsonl');
const RETENTION_MS = 7 * 24 * 60 * 60 * 1000;
const MAX_ENTRIES = 2000;

/** @type {Array<object>} */
let entries = [];
let loaded = false;
/** @type {((evt: object) => void) | null} */
let onChange = null;

function nowIso() {
  return new Date().toISOString();
}

function ensureLoaded() {
  if (loaded) {
    return;
  }
  loaded = true;
  entries = [];
  try {
    if (!fs.existsSync(FILE_PATH)) {
      return;
    }
    const text = fs.readFileSync(FILE_PATH, 'utf8');
    for (const line of text.split(/\r?\n/)) {
      if (!line.trim()) {
        continue;
      }
      try {
        const row = JSON.parse(line);
        if (row && row.id && row.ts) {
          entries.push(row);
        }
      } catch {
        // skip corrupt line
      }
    }
  } catch {
    entries = [];
  }
  pruneInMemory();
}

function pruneInMemory() {
  const cutoff = Date.now() - RETENTION_MS;
  entries = entries.filter((e) => {
    const t = Date.parse(e.ts);
    return Number.isFinite(t) && t >= cutoff;
  });
  if (entries.length > MAX_ENTRIES) {
    entries = entries.slice(entries.length - MAX_ENTRIES);
  }
}

function rewriteFile() {
  const dir = path.dirname(FILE_PATH);
  if (!fs.existsSync(dir)) {
    fs.mkdirSync(dir, { recursive: true });
  }
  const body = entries.map((e) => JSON.stringify(e)).join('\n') + (entries.length ? '\n' : '');
  fs.writeFileSync(FILE_PATH, body, 'utf8');
}

function appendLine(row) {
  const dir = path.dirname(FILE_PATH);
  if (!fs.existsSync(dir)) {
    fs.mkdirSync(dir, { recursive: true });
  }
  fs.appendFileSync(FILE_PATH, `${JSON.stringify(row)}\n`, 'utf8');
}

function setOnChange(handler) {
  onChange = typeof handler === 'function' ? handler : null;
}

function notify(evt) {
  if (onChange) {
    try {
      onChange(evt);
    } catch {
      // ignore listener errors
    }
  }
}

function severityRank(sev) {
  switch (sev) {
    case 'fault':
      return 4;
    case 'error':
      return 3;
    case 'warning':
      return 2;
    case 'info':
      return 1;
    default:
      return 0;
  }
}

/**
 * Record a message. Dedupes identical active code (refreshes ts/detail).
 * @param {object} input
 */
function record(input) {
  ensureLoaded();
  const resolved = resolveMessage(input || {});
  const severity = input.severity || 'error';
  const sticky = input.sticky !== false && severity !== 'info';
  const source = input.source || 'gui';
  const context = input.context && typeof input.context === 'object' ? input.context : null;

  if (sticky) {
    const existing = entries.find((e) => e.active && e.code === resolved.code);
    if (existing) {
      existing.ts = nowIso();
      existing.title = resolved.title;
      existing.detail = resolved.detail;
      existing.context = context;
      existing.severity = severity;
      rewriteFile();
      notify({ type: 'update', message: existing, summary: getSummary() });
      return existing;
    }
  }

  const row = {
    id: crypto.randomUUID(),
    ts: nowIso(),
    severity,
    code: resolved.code,
    title: resolved.title,
    detail: resolved.detail,
    source,
    context,
    active: sticky,
    ackedAt: null,
    clearedAt: null,
  };
  entries.push(row);
  pruneInMemory();
  appendLine(row);
  notify({ type: 'create', message: row, summary: getSummary() });
  return row;
}

function list({ activeOnly = false, limit = 200 } = {}) {
  ensureLoaded();
  pruneInMemory();
  let rows = entries.slice();
  if (activeOnly) {
    rows = rows.filter((e) => e.active);
  }
  rows.sort((a, b) => String(b.ts).localeCompare(String(a.ts)));
  const lim = Math.max(1, Math.min(1000, Number(limit) || 200));
  return rows.slice(0, lim);
}

function getById(id) {
  ensureLoaded();
  return entries.find((e) => e.id === id) || null;
}

function ack(id) {
  ensureLoaded();
  const row = entries.find((e) => e.id === id);
  if (!row) {
    return null;
  }
  row.active = false;
  row.ackedAt = nowIso();
  rewriteFile();
  notify({ type: 'ack', message: row, summary: getSummary() });
  return row;
}

function ackAll() {
  ensureLoaded();
  const ts = nowIso();
  let n = 0;
  for (const row of entries) {
    if (row.active) {
      row.active = false;
      row.ackedAt = ts;
      n += 1;
    }
  }
  if (n > 0) {
    rewriteFile();
    notify({ type: 'ack_all', count: n, summary: getSummary() });
  }
  return n;
}

function clearActiveByCode(code) {
  ensureLoaded();
  const ts = nowIso();
  let changed = false;
  for (const row of entries) {
    if (row.active && row.code === code) {
      row.active = false;
      row.clearedAt = ts;
      changed = true;
    }
  }
  if (changed) {
    rewriteFile();
    notify({ type: 'cleared', code, summary: getSummary() });
  }
}

/**
 * Deactivate all active rows whose code starts with prefix (e.g. "Abort.").
 * History rows remain; only the status strip is cleared.
 */
function clearActiveByCodePrefix(prefix) {
  ensureLoaded();
  if (!prefix) {
    return 0;
  }
  const ts = nowIso();
  let n = 0;
  for (const row of entries) {
    if (row.active && typeof row.code === 'string' && row.code.startsWith(prefix)) {
      row.active = false;
      row.clearedAt = ts;
      n += 1;
    }
  }
  if (n > 0) {
    rewriteFile();
    notify({ type: 'cleared_prefix', prefix, count: n, summary: getSummary() });
  }
  return n;
}

function getSummary() {
  ensureLoaded();
  const active = entries.filter((e) => e.active);
  active.sort((a, b) => severityRank(b.severity) - severityRank(a.severity) || String(b.ts).localeCompare(String(a.ts)));
  const top = active[0] || null;
  return {
    activeCount: active.length,
    topTitle: top ? top.title : null,
    topSeverity: top ? top.severity : null,
    topId: top ? top.id : null,
    topCode: top ? top.code : null,
  };
}

function pruneNow() {
  ensureLoaded();
  const before = entries.length;
  pruneInMemory();
  if (entries.length !== before) {
    rewriteFile();
  }
  return entries.length;
}

module.exports = {
  RETENTION_MS,
  FILE_PATH,
  setOnChange,
  record,
  list,
  getById,
  ack,
  ackAll,
  clearActiveByCode,
  clearActiveByCodePrefix,
  getSummary,
  pruneNow,
};
