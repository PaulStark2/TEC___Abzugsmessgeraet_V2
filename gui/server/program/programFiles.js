'use strict';

const fs = require('fs');
const path = require('path');
const { validateTestProgram, toJobFile, toothId } = require('./validateTestProgram');

const PROGRAMS_DIR = path.join(__dirname, '..', 'data', 'programs');

function ensureProgramsDir() {
  if (!fs.existsSync(PROGRAMS_DIR)) {
    fs.mkdirSync(PROGRAMS_DIR, { recursive: true });
  }
}

/**
 * Accept only a bare "*.json" basename (no path separators).
 * @param {string} fileName
 * @returns {string|null}
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
    .filter((name) => /^[a-zA-Z0-9_-]+\.json$/i.test(name))
    .sort((a, b) => a.localeCompare(b));

  const out = [];
  for (const fileName of names) {
    try {
      const raw = JSON.parse(fs.readFileSync(path.join(PROGRAMS_DIR, fileName), 'utf8'));
      const validation = validateTestProgram(raw);
      if (!validation.ok) {
        out.push({ fileName, programName: null, buildJobNumber: null, pointCount: 0, valid: false });
        continue;
      }
      out.push({
        fileName,
        programName: validation.value.programName || null,
        buildJobNumber: validation.value.buildJobNumber || null,
        pointCount: validation.value.testPoints.length,
        valid: true,
      });
    } catch {
      out.push({ fileName, programName: null, buildJobNumber: null, pointCount: 0, valid: false });
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
  return { ok: true, fileName: resolved.fileName, program: validation.value, raw };
}

function writeProgram(fileName, program, preservedRoot) {
  const resolved = resolveProgramPath(fileName);
  if (!resolved) {
    return { ok: false, error: 'Ungültiger Dateiname' };
  }
  const fileObject = toJobFile(program, preservedRoot);
  const validation = validateTestProgram(fileObject);
  if (!validation.ok) {
    return { ok: false, error: 'Programmvalidierung fehlgeschlagen', errors: validation.errors };
  }
  ensureProgramsDir();
  fs.writeFileSync(resolved.fullPath, `${JSON.stringify(fileObject, null, 2)}\n`, 'utf8');
  return { ok: true, fileName: resolved.fileName, program: validation.value };
}

function findPlate(program, plateId) {
  return (program.plates || []).find((plate) => plate.plate === plateId) || null;
}

function insertTooth(program, plateId, orderUid, tooth) {
  let plate = findPlate(program, plateId);
  if (!plate) {
    plate = { plate: plateId, orders: [] };
    program.plates.push(plate);
  }
  let order = plate.orders.find((item) => item.orderUid === orderUid);
  if (!order) {
    order = { orderUid, teeth: [] };
    plate.orders.push(order);
  }
  if (order.teeth.some((item) => item.toothNumber === tooth.toothNumber)) {
    return { ok: false, error: `Zahn ${tooth.toothNumber} existiert in Auftrag ${orderUid} bereits` };
  }
  order.teeth.push(tooth);
  return { ok: true };
}

/**
 * Create a job file with one tooth, or append a tooth.
 * The jogged pose is stored as the JSON point (clamp floor), not the approach height.
 * @param {{
 *   mode: 'create'|'append',
 *   fileName?: string,
 *   buildJobNumber?: string,
 *   plateId: string,
 *   orderUid: string,
 *   toothNumber: number,
 *   pose: {x:number,y:number,z:number},
 * }} opts
 */
function teachPoint(opts) {
  const mode = opts && opts.mode;
  const plateId = typeof opts.plateId === 'string' ? opts.plateId.trim() : '';
  const orderUid = typeof opts.orderUid === 'string' ? opts.orderUid.trim() : '';
  const toothNumber = Number(opts.toothNumber);
  const pose = opts.pose;
  if (!plateId) {
    return { ok: false, error: 'Platte darf nicht leer sein' };
  }
  if (!orderUid) {
    return { ok: false, error: 'Auftrags-UID darf nicht leer sein' };
  }
  if (!Number.isInteger(toothNumber)) {
    return { ok: false, error: 'Zahnnummer muss eine ganze Zahl sein' };
  }
  if (!pose || ![pose.x, pose.y, pose.z].every(Number.isFinite)) {
    return { ok: false, error: 'Ungültige Bauplatten-Koordinaten' };
  }
  const tooth = {
    toothNumber,
    id: toothId(plateId, orderUid, toothNumber),
    x: pose.x,
    y: pose.y,
    z: pose.z,
  };

  if (mode === 'create') {
    const buildJobNumber =
      typeof opts.buildJobNumber === 'string' && opts.buildJobNumber.trim()
        ? opts.buildJobNumber.trim()
        : `${plateId}-${toothNumber}`;
    const fileName = opts.fileName
      ? sanitizeFileName(opts.fileName)
      : fileNameFromProgramName(buildJobNumber);
    if (!fileName) {
      return { ok: false, error: 'Ungültiger Baujob-/Dateiname' };
    }
    const resolved = resolveProgramPath(fileName);
    if (!resolved) {
      return { ok: false, error: 'Ungültiger Dateiname' };
    }
    if (fs.existsSync(resolved.fullPath)) {
      return { ok: false, error: `Datei ${fileName} existiert bereits` };
    }
    const program = {
      schemaVersion: 2,
      unit: 'mm',
      buildJobNumber,
      programName: buildJobNumber,
      plates: [
        {
          plate: plateId,
          orders: [{ orderUid, teeth: [tooth] }],
        },
      ],
    };
    const written = writeProgram(fileName, program);
    if (!written.ok) {
      return written;
    }
    const saved = written.program.testPoints[0];
    return {
      ok: true,
      mode: 'create',
      fileName: written.fileName,
      program: written.program,
      point: saved,
    };
  }

  if (mode === 'append') {
    const existing = readProgram(opts.fileName);
    if (!existing.ok) {
      return existing;
    }
    const program = {
      schemaVersion: existing.program.schemaVersion || 2,
      unit: 'mm',
      buildJobNumber: existing.program.buildJobNumber,
      programName: existing.program.programName,
      plates: existing.program.plates.map((plate) => ({
        plate: plate.plate,
        orders: plate.orders.map((order) => ({
          orderUid: order.orderUid,
          teeth: order.teeth.map((item) => ({ ...item })),
        })),
      })),
    };
    const inserted = insertTooth(program, plateId, orderUid, tooth);
    if (!inserted.ok) {
      return inserted;
    }
    const preserved = { ...existing.raw };
    const written = writeProgram(existing.fileName, program, preserved);
    if (!written.ok) {
      return written;
    }
    const saved = written.program.testPoints.find((item) => item.id === tooth.id);
    return {
      ok: true,
      mode: 'append',
      fileName: written.fileName,
      program: written.program,
      point: saved,
    };
  }

  return { ok: false, error: 'mode muss create oder append sein' };
}

module.exports = {
  PROGRAMS_DIR,
  ensureProgramsDir,
  sanitizeFileName,
  fileNameFromProgramName,
  listPrograms,
  readProgram,
  writeProgram,
  teachPoint,
};
