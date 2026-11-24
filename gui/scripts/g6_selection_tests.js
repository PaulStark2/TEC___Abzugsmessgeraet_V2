'use strict';

const programStore = require('../server/program/programStore');
const selectionStore = require('../server/program/selectionStore');
const { validateTestProgram } = require('../server/program/validateTestProgram');

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

function sample() {
  const validation = validateTestProgram({
    job: {
      buildJobNumber: 'SelectTest',
      plates: [
        {
          plate: 'A',
          orders: [
            {
              orderUid: 'ORD-B',
              teeth: [
                { toothNumber: 21, point: { x: 2, y: 0, z: 1 } },
                { toothNumber: 11, point: { x: 1, y: 0, z: 1 } },
              ],
            },
            {
              orderUid: 'ORD-A',
              teeth: [{ toothNumber: 33, point: { x: 3, y: 0, z: 2 } }],
            },
          ],
        },
        {
          plate: 'B',
          orders: [
            {
              orderUid: 'ORD-C',
              teeth: [{ toothNumber: 12, point: { x: 4, y: 0, z: 3 } }],
            },
          ],
        },
      ],
    },
  });
  assert(validation.ok, 'sample job valid');
  return validation.value;
}

function idsOf(selection) {
  return (selection.preview || selection.points).map((point) => point.toothNumber);
}

function idOf(selection, orderUid, toothNumber) {
  return selection.preview.find((point) => point.orderUid === orderUid && point.toothNumber === toothNumber).id;
}

function setup() {
  programStore.clearProgram();
  selectionStore.clear();
  programStore.setProgram(sample());
}

function run() {
  setup();
  assert(selectionStore.getSelection().confirmed === false, 'starts unconfirmed');
  assert(selectionStore.getSelection().selectedIds.length === 0, 'starts empty');

  let r = selectionStore.setDraft({ selectAll: true });
  assert(r.ok, 'selectAll ok');
  assert(
    JSON.stringify(idsOf(r.selection)) === JSON.stringify([11, 21, 33, 12]),
    'empty select-all uses file order and tooth number'
  );

  r = selectionStore.setDraft({ selectAll: true });
  assert(r.ok && r.selection.preview.length === 0, 'select-all again clears a full selection');

  r = selectionStore.setDraft({
    orderSelection: { plate: 'A', orderUid: 'ORD-A', selected: true },
  });
  assert(JSON.stringify(idsOf(r.selection)) === JSON.stringify([33]), 'one order appended');

  const tooth21 = selectionStore.getSelection();
  const id11 = 'A|ORD-B|11';
  const id21 = 'A|ORD-B|21';
  r = selectionStore.setDraft({ toothRank: [idOf(tooth21, 'ORD-A', 33), id21] });
  assert(JSON.stringify(idsOf(r.selection)) === JSON.stringify([33, 21]), 'a single tooth may be chosen');

  r = selectionStore.setDraft({ selectAll: true });
  assert(
    JSON.stringify(idsOf(r.selection)) === JSON.stringify([33, 21, 11, 12]),
    'select-all keeps the existing order and appends the open points'
  );

  const dragged = [idOf(r.selection, 'ORD-C', 12), idOf(r.selection, 'ORD-A', 33), id21, id11];
  r = selectionStore.setDraft({ toothRank: dragged });
  assert(r.selection.preview.map((point) => point.id).join(',') === dragged.join(','), 'drag sets any order');
  assert(r.selection.preview.map((point) => point.sequenceNumber).join(',') === '1,2,3,4', 'sequence follows the drag');

  r = selectionStore.setDraft({
    orderSelection: { plate: 'A', orderUid: 'ORD-B', selected: false },
  });
  assert(JSON.stringify(idsOf(r.selection)) === JSON.stringify([12, 33]), 'removing an order leaves the rest');

  r = selectionStore.setDraft({
    orderSelection: { plate: 'A', orderUid: 'ORD-B', selected: true },
  });
  assert(
    JSON.stringify(idsOf(r.selection)) === JSON.stringify([12, 33, 11, 21]),
    'a newly checked order is appended, teeth by tooth number'
  );

  r = selectionStore.confirmSelection();
  assert(r.ok && r.selection.confirmed, 'confirm ok');
  assert(r.selection.points[0].toothNumber === 12, 'frozen order is the dragged order');

  const tooMany = [];
  for (let i = 1; i <= 33; i += 1) {
    tooMany.push({ toothNumber: i, x: 0, y: 0, z: 1 });
  }
  const big = validateTestProgram({
    job: {
      buildJobNumber: 'BIG',
      plates: [
        {
          plate: 'A',
          orders: [
            {
              orderUid: 'O',
              teeth: tooMany.map((tooth) => ({
                toothNumber: tooth.toothNumber,
                point: { x: 0, y: 0, z: 1 },
              })),
            },
          ],
        },
      ],
    },
  });
  programStore.setProgram(big.value);
  selectionStore.clear();
  r = selectionStore.setDraft({ selectAll: true });
  assert(r.ok, 'draft may list more than 32');
  r = selectionStore.confirmSelection();
  assert(!r.ok, 'confirm rejects more than 32');

  programStore.clearProgram();
  selectionStore.clear();
  assert(selectionStore.getSelection() === null, 'no program');

  console.log('G6 selectionStore tests: PASS');
}

try {
  run();
} catch (err) {
  console.error('G6 selectionStore tests: FAIL', err.message);
  process.exit(1);
}
