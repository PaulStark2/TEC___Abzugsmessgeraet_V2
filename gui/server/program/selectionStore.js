'use strict';

const programStore = require('./programStore');

let draftSequenceNumbers = [];
let confirmed = false;
let confirmedAt = null;
let confirmedPoints = [];

function clear() {
  draftSequenceNumbers = [];
  confirmed = false;
  confirmedAt = null;
  confirmedPoints = [];
}

function getProgramPoints() {
  const program = programStore.getProgram();
  if (!program) {
    return null;
  }
  return program.testPoints || [];
}

function validSequenceSet() {
  const points = getProgramPoints();
  if (!points) {
    return new Set();
  }
  return new Set(points.map((p) => p.sequenceNumber));
}

function sortByProgramOrder(sequenceNumbers) {
  const points = getProgramPoints();
  if (!points) {
    return [...sequenceNumbers];
  }
  const orderMap = new Map(points.map((p, i) => [p.sequenceNumber, i]));
  return [...sequenceNumbers].sort((a, b) => orderMap.get(a) - orderMap.get(b));
}

function resolvePoints(sequenceNumbers) {
  const points = getProgramPoints();
  if (!points) {
    return [];
  }
  const bySeq = new Map(points.map((p) => [p.sequenceNumber, p]));
  return sortByProgramOrder(sequenceNumbers)
    .map((seq) => bySeq.get(seq))
    .filter(Boolean);
}

function buildSelectionPayload() {
  const points = getProgramPoints();
  const program = programStore.getProgram();

  return {
    confirmed,
    confirmedAt,
    draftSequenceNumbers: [...draftSequenceNumbers],
    selectedSequenceNumbers: confirmed ? draftSequenceNumbers.map((n) => n) : [...draftSequenceNumbers],
    points: confirmed
      ? confirmedPoints.map((p) => ({
          name: p.name,
          sequenceNumber: p.sequenceNumber,
          x: p.x,
          y: p.y,
          z: p.z,
        }))
      : resolvePoints(draftSequenceNumbers).map((p) => ({
          name: p.name,
          sequenceNumber: p.sequenceNumber,
          x: p.x,
          y: p.y,
          z: p.z,
        })),
    meta: program
      ? {
          programName: program.programName ?? null,
          version: program.version,
          totalPoints: points ? points.length : 0,
        }
      : null,
  };
}

/**
 * @param {{ selectedSequenceNumbers?: number[], selectAll?: boolean }} body
 */
function setDraft(body) {
  const points = getProgramPoints();
  if (!points) {
    return { ok: false, error: 'No program loaded' };
  }

  const valid = validSequenceSet();
  let nums;

  if (body && body.selectAll === true) {
    nums = points.map((p) => p.sequenceNumber);
  } else {
    if (!body || !Array.isArray(body.selectedSequenceNumbers)) {
      return { ok: false, error: 'selectedSequenceNumbers must be an array' };
    }

    const seen = new Set();
    nums = [];
    for (const raw of body.selectedSequenceNumbers) {
      const seq = Number(raw);
      if (!Number.isFinite(seq) || !Number.isInteger(seq)) {
        return { ok: false, error: `Invalid sequenceNumber: ${raw}` };
      }
      if (seen.has(seq)) {
        continue;
      }
      if (!valid.has(seq)) {
        return { ok: false, error: `Unknown sequenceNumber: ${seq}` };
      }
      seen.add(seq);
      nums.push(seq);
    }
  }

  draftSequenceNumbers = sortByProgramOrder(nums);
  confirmed = false;
  confirmedAt = null;
  confirmedPoints = [];

  return {
    ok: true,
    selection: buildSelectionPayload(),
  };
}

function confirmSelection() {
  if (!getProgramPoints()) {
    return { ok: false, error: 'No program loaded' };
  }
  if (draftSequenceNumbers.length === 0) {
    return { ok: false, error: 'Selection is empty' };
  }

  confirmedPoints = resolvePoints(draftSequenceNumbers);
  confirmed = true;
  confirmedAt = new Date().toISOString();

  return {
    ok: true,
    selection: buildSelectionPayload(),
  };
}

function getSelection() {
  if (!getProgramPoints()) {
    return null;
  }
  return buildSelectionPayload();
}

module.exports = {
  clear,
  setDraft,
  confirmSelection,
  getSelection,
};
