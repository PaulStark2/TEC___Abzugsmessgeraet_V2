'use strict';

/**
 * Confirmed run order for one loaded job.
 *
 * The stored list is the operator's sequence. Automatic order (plates and
 * orders as in the file, teeth by tooth number) is used only for points that
 * are added. Existing points stay where the operator left them, including
 * after a free drag. "Alles auswählen" appends the points that are still
 * open; if every point is already selected it clears the list.
 */

const programStore = require('./programStore');
const { listTeeth, toothId } = require('./validateTestProgram');

const MAX_SELECTION = 32;

/** @type {string[]} selected tooth ids in run order */
let toothRank = [];
let confirmed = false;
let confirmedAt = null;
/** @type {object[]} */
let confirmedPoints = [];

function clear() {
  toothRank = [];
  confirmed = false;
  confirmedAt = null;
  confirmedPoints = [];
}

function getProgram() {
  return programStore.getProgram();
}

function allTeeth() {
  const program = getProgram();
  if (!program) {
    return null;
  }
  return listTeeth(program);
}

function knownIdSet(teeth) {
  return new Set(teeth.map((tooth) => tooth.id));
}

/**
 * File order of plates and orders, teeth inside an order by tooth number.
 * @returns {string[]}
 */
function automaticIds() {
  const program = getProgram();
  const teeth = allTeeth();
  if (!program || !teeth) {
    return [];
  }
  const known = knownIdSet(teeth);
  const out = [];
  for (const plate of program.plates || []) {
    for (const order of plate.orders || []) {
      const sorted = [...(order.teeth || [])].sort((a, b) => a.toothNumber - b.toothNumber);
      for (const tooth of sorted) {
        const id = tooth.id || toothId(plate.plate, order.orderUid, tooth.toothNumber);
        if (known.has(id) && !out.includes(id)) {
          out.push(id);
        }
      }
    }
  }
  return out;
}

/**
 * Teeth of one order, sorted by tooth number. null if the order is unknown.
 * @returns {string[]|null}
 */
function orderToothIds(plateId, orderUid) {
  const program = getProgram();
  if (!program) {
    return null;
  }
  const plate = (program.plates || []).find((item) => item.plate === plateId);
  if (!plate) {
    return null;
  }
  const order = (plate.orders || []).find((item) => item.orderUid === orderUid);
  if (!order) {
    return null;
  }
  return [...(order.teeth || [])]
    .sort((a, b) => a.toothNumber - b.toothNumber)
    .map((tooth) => tooth.id || toothId(plateId, orderUid, tooth.toothNumber));
}

function appendMissing(current, incoming) {
  const have = new Set(current);
  const next = current.slice();
  for (const id of incoming) {
    if (!have.has(id)) {
      have.add(id);
      next.push(id);
    }
  }
  return next;
}

function resolveRunOrder() {
  const teeth = allTeeth();
  if (!teeth) {
    return [];
  }
  const byId = new Map(teeth.map((tooth) => [tooth.id, tooth]));
  return toothRank.filter((id) => byId.has(id)).map((id) => byId.get(id));
}

function withSequence(list) {
  return list.map((point, index) => ({
    ...point,
    sequenceNumber: index + 1,
  }));
}

function buildSelectionPayload() {
  const program = getProgram();
  const teeth = allTeeth();
  if (!program || !teeth) {
    return null;
  }
  const preview = withSequence(resolveRunOrder());
  const points = confirmed ? confirmedPoints.map((point) => ({ ...point })) : preview;
  return {
    confirmed,
    confirmedAt,
    selectedIds: preview.map((point) => point.id),
    toothRank: preview.map((point) => point.id),
    preview,
    points,
    draftSequenceNumbers: points.map((point) => point.sequenceNumber),
    meta: {
      programName: program.programName || program.buildJobNumber || null,
      buildJobNumber: program.buildJobNumber || null,
      schemaVersion: program.schemaVersion || 2,
      totalPoints: teeth.length,
      selectedCount: preview.length,
      maxSelection: MAX_SELECTION,
    },
  };
}

function sanitizeIdList(raw, known) {
  if (!Array.isArray(raw)) {
    return { ok: false, error: 'selectedIds must be an array' };
  }
  const seen = new Set();
  const ids = [];
  for (const item of raw) {
    const id = String(item);
    if (seen.has(id)) {
      continue;
    }
    if (!known.has(id)) {
      return { ok: false, error: `Unknown tooth id: ${id}` };
    }
    seen.add(id);
    ids.push(id);
  }
  return { ok: true, ids };
}

/**
 * @param {{
 *   toothRank?: string[],
 *   selectAll?: boolean,
 *   orderSelection?: { plate: string, orderUid: string, selected: boolean },
 *   selectedIds?: string[],
 * }} body
 * toothRank replaces the sequence exactly (drag, or one tooth added at the end /
 * removed). selectAll appends every open point in automatic order, or clears
 * the list when everything is already selected. orderSelection adds or removes
 * one order; added teeth are sorted by tooth number and appended.
 */
function setDraft(body) {
  const teeth = allTeeth();
  if (!getProgram() || !teeth) {
    return { ok: false, error: 'No program loaded' };
  }
  const known = knownIdSet(teeth);
  const input = body || {};
  let next = toothRank.filter((id) => known.has(id));

  if (Array.isArray(input.toothRank)) {
    const parsed = sanitizeIdList(input.toothRank, known);
    if (!parsed.ok) {
      return parsed;
    }
    next = parsed.ids;
  } else if (input.selectAll === true) {
    const all = automaticIds();
    next = next.length === all.length ? [] : appendMissing(next, all);
  } else if (input.orderSelection && typeof input.orderSelection === 'object') {
    const plate = String(input.orderSelection.plate ?? '');
    const orderUid = String(input.orderSelection.orderUid ?? '');
    const ids = orderToothIds(plate, orderUid);
    if (!ids) {
      return { ok: false, error: 'Unknown order' };
    }
    if (input.orderSelection.selected) {
      next = appendMissing(next, ids);
    } else {
      const drop = new Set(ids);
      next = next.filter((id) => !drop.has(id));
    }
  } else if (Array.isArray(input.selectedIds)) {
    const parsed = sanitizeIdList(input.selectedIds, known);
    if (!parsed.ok) {
      return parsed;
    }
    const keep = new Set(parsed.ids);
    next = appendMissing(
      next.filter((id) => keep.has(id)),
      parsed.ids
    );
  }

  toothRank = next;
  confirmed = false;
  confirmedAt = null;
  confirmedPoints = [];
  return { ok: true, selection: buildSelectionPayload() };
}

function confirmSelection() {
  if (!allTeeth()) {
    return { ok: false, error: 'No program loaded' };
  }
  const ordered = withSequence(resolveRunOrder());
  if (ordered.length === 0) {
    return { ok: false, error: 'Selection is empty' };
  }
  if (ordered.length > MAX_SELECTION) {
    return {
      ok: false,
      error: `Höchstens ${MAX_SELECTION} Prüfstellen pro Lauf (aktuell ${ordered.length})`,
    };
  }
  confirmedPoints = ordered;
  confirmed = true;
  confirmedAt = new Date().toISOString();
  return { ok: true, selection: buildSelectionPayload() };
}

function getSelection() {
  if (!allTeeth()) {
    return null;
  }
  return buildSelectionPayload();
}

module.exports = {
  MAX_SELECTION,
  clear,
  setDraft,
  confirmSelection,
  getSelection,
};
