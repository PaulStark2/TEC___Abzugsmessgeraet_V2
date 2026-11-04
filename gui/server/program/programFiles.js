'use strict';

const fs = require('fs');
const path = require('path');
const { validateTestProgram } = require('./validateTestProgram');

const PROGRAMS_DIR = path.join(__dirname, '..', 'data', 'programs');
const MAX_TEST_POINTS = 32;

function ensureProgramsDir() {
  if (!fs.existsSync(PROGRAMS_DIR)) {
    fs.mkdirSync(PROGRAMS_DIR, { recursive: true });
  }
}

/**
 * Accept only a bare "*.json" basename (no path separators).
 * @param {string} fileName
 * @returns {string|null} sanitized basename or null
 */
function sanitizeFileName(fileName) {
  if (typeof fileName !== 'string') {
    return null;
  }
  const base = path.basename(fileName.trim());
  if (!base || base !== fileName.trim()) {
    return null;
  }
  if (!/^[a-zA-Z0-9_-]+\.json$/i.test(base)) {
    return null;
  }
  return base;
}

/**
 * Build a safe file name from a program name.
 * @param {string} programName
 * @returns {string|null}
 */
function fileNameFromProgramName(programName) {
  if (typeof programName !== 'string') {
    return null;
  }
  const stem = programName
    .trim()
    .replace(/[^a-zA-Z0-9_-]+/g, '_')
    .replace(/^_+|_+$/g, '')
    .slice(0, 64);
  if (!stem) {
    return null;
  }
  return `${stem}.json`;
}

function resolveProgramPath(fileName) {
  const safe = sanitizeFileName(fileName);
  if (!safe) {
    return null;
  }
  ensureProgramsDir();
  const full = path.join(PROGRAMS_DIR, safe);
  if (path.dirname(full) !== PROGRAMS_DIR) {
    return null;
  }
  return { fileName: safe, fullPath: full };
}

function listPrograms() {
  ensureProgramsDir();
  const names = fs
    .readdirSync(PROGRAMS_DIR)
    .filter((n) => /^[a-zA-Z0-9_-]+\.json$/i.test(n))
    .sort((a, b) => a.localeCompare(b));

  const out = [];
  for (const fileName of names) {
    try {
      const raw = JSON.parse(fs.readFileSync(path.join(PROGRAMS_DIR, fileName), 'utf8'));
      const validation = validateTestProgram(raw);
      if (!validation.ok) {
        out.push({
          fileName,
          programName: null,
          pointCount: 0,
          valid: false,
        });
        continue;
      }
      out.push({
        fileName,
        programName: validation.value.programName || null,
        pointCount: validation.value.testPoints.length,
        valid: true,
      });
    } catch {
      out.push({
        fileName,
        programName: null,
        pointCount: 0,
        valid: false,
      });
    }
  }
  return out;
}

function readProgram(fileName) {
  const resolved = resolveProgramPath(fileName);
  if (!resolved) {
    return { ok: false, error: 'Ungültiger Dateiname' };
  }
  if (!fs.existsSync(resolved.fullPath)) {
    return { ok: false, error: 'Datei nicht gefunden' };
  }
  let raw;
  try {
    raw = JSON.parse(fs.readFileSync(resolved.fullPath, 'utf8'));
  } catch {
    return { ok: false, error: 'Datei ist kein gültiges JSON' };
  }
  const validation = validateTestProgram(raw);
  if (!validation.ok) {
    return { ok: false, error: 'Programmvalidierung fehlgeschlagen', errors: validation.errors };
  }
  return { ok: true, fileName: resolved.fileName, program: validation.value };
}

function writeProgram(fileName, program) {
  const resolved = resolveProgramPath(fileName);
  if (!resolved) {
    return { ok: false, error: 'Ungültiger Dateiname' };
  }
  const validation = validateTestProgram(program);
  if (!validation.ok) {
    return { ok: false, error: 'Programmvalidierung fehlgeschlagen', errors: validation.errors };
  }
  ensureProgramsDir();
  const payload = {
    version: validation.value.version,
    ...(validation.value.programName ? { programName: validation.value.programName } : {}),
    testPoints: validation.value.testPoints,
  };
  fs.writeFileSync(resolved.fullPath, `${JSON.stringify(payload, null, 2)}\n`, 'utf8');
  return { ok: true, fileName: resolved.fileName, program: validation.value };
}

function nextSequenceNumber(testPoints) {
  if (!Array.isArray(testPoints) || testPoints.length === 0) {
    return 1;
  }
  let max = 0;
  for (const p of testPoints) {
    const seq = Number(p.sequenceNumber);
    if (Number.isInteger(seq) && seq > max) {
      max = seq;
    }
  }
  return max + 1;
}

/**
 * Create a new program file with one point, or append a point to an existing file.
 * @param {{ mode: 'create'|'append', fileName?: string, programName?: string, pointName: string, plate: {x,y,z} }} opts
 */
function teachPoint(opts) {
  const mode = opts && opts.mode;
  const pointName = typeof opts.pointName === 'string' ? opts.pointName.trim() : '';
  const plate = opts.plate;
  if (!pointName) {
    return { ok: false, error: 'Punktname darf nicht leer sein' };
  }
  if (!plate || ![plate.x, plate.y, plate.z].every(Number.isFinite)) {
    return { ok: false, error: 'Ungültige Bauplatten-Koordinaten' };
  }

  if (mode === 'create') {
    const programName =
      typeof opts.programName === 'string' && opts.programName.trim()
        ? opts.programName.trim()
        : pointName;
    const fileName = opts.fileName
      ? sanitizeFileName(opts.fileName)
      : fileNameFromProgramName(programName);
    if (!fileName) {
      return { ok: false, error: 'Ungültiger Programm-/Dateiname' };
    }
    const resolved = resolveProgramPath(fileName);
    if (!resolved) {
      return { ok: false, error: 'Ungültiger Dateiname' };
    }
    if (fs.existsSync(resolved.fullPath)) {
      return { ok: false, error: `Datei ${fileName} existiert bereits` };
    }
    const program = {
      version: 1,
      programName,
      testPoints: [
        {
          name: pointName,
          sequenceNumber: 1,
          x: plate.x,
          y: plate.y,
          z: plate.z,
        },
      ],
    };
    const written = writeProgram(fileName, program);
    if (!written.ok) {
      return written;
    }
    return {
      ok: true,
      mode: 'create',
      fileName: written.fileName,
      program: written.program,
      point: written.program.testPoints[0],
    };
  }

  if (mode === 'append') {
    const existing = readProgram(opts.fileName);
    if (!existing.ok) {
      return existing;
    }
    if (existing.program.testPoints.length >= MAX_TEST_POINTS) {
      return { ok: false, error: `Maximal ${MAX_TEST_POINTS} Prüfpunkte pro Datei` };
    }
    const seq = nextSequenceNumber(existing.program.testPoints);
    const point = {
      name: pointName,
      sequenceNumber: seq,
      x: plate.x,
      y: plate.y,
      z: plate.z,
    };
    const program = {
      version: existing.program.version || 1,
      ...(existing.program.programName ? { programName: existing.program.programName } : {}),
      testPoints: [...existing.program.testPoints, point],
    };
    const written = writeProgram(existing.fileName, program);
    if (!written.ok) {
      return written;
    }
    return {
      ok: true,
      mode: 'append',
      fileName: written.fileName,
      program: written.program,
      point,
    };
  }

  return { ok: false, error: 'mode muss create oder append sein' };
}

module.exports = {
  PROGRAMS_DIR,
  MAX_TEST_POINTS,
  ensureProgramsDir,
  sanitizeFileName,
  fileNameFromProgramName,
  listPrograms,
  readProgram,
  writeProgram,
  nextSequenceNumber,
  teachPoint,
};
