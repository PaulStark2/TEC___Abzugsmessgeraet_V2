'use strict';

const programStore = require('../server/program/programStore');
const selectionStore = require('../server/program/selectionStore');

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

const SAMPLE_PROGRAM = {
  version: 1,
  programName: 'SelectTest',
  testPoints: [
    { name: 'A', sequenceNumber: 2, x: 1, y: 2, z: 3 },
    { name: 'B', sequenceNumber: 5, x: 4, y: 5, z: 6 },
    { name: 'C', sequenceNumber: 10, x: 7, y: 8, z: 9 },
  ],
};

function setup() {
  programStore.clearProgram();
  selectionStore.clear();
  programStore.setProgram(SAMPLE_PROGRAM);
}

function run() {
  setup();
  assert(selectionStore.getSelection().confirmed === false, 'starts unconfirmed');
  assert(selectionStore.getSelection().draftSequenceNumbers.length === 0, 'starts empty draft');

  // selectAll
  let r = selectionStore.setDraft({ selectAll: true });
  assert(r.ok, 'selectAll ok');
  assert(r.selection.draftSequenceNumbers.length === 3, 'selectAll count');
  assert(
    JSON.stringify(r.selection.draftSequenceNumbers) === JSON.stringify([2, 5, 10]),
    'selectAll order by program'
  );

  // dedupe invalid sequence
  r = selectionStore.setDraft({ selectedSequenceNumbers: [5, 5, 2, 999] });
  assert(!r.ok, 'unknown seq rejected');
  assert(r.error.includes('999'), 'unknown seq message');

  // valid partial with dedupe
  r = selectionStore.setDraft({ selectedSequenceNumbers: [10, 5, 5, 2] });
  assert(r.ok, 'partial ok');
  assert(JSON.stringify(r.selection.draftSequenceNumbers) === JSON.stringify([2, 5, 10]), 'deduped order');

  // confirm
  r = selectionStore.confirmSelection();
  assert(r.ok, 'confirm ok');
  assert(r.selection.confirmed === true, 'confirmed flag');
  assert(r.selection.points.length === 3, 'confirmed points');
  assert(r.selection.points[0].sequenceNumber === 2, 'first point seq');

  // empty confirm rejected
  selectionStore.clear();
  r = selectionStore.confirmSelection();
  assert(!r.ok, 'empty confirm rejected');

  // clear-on-upload simulation
  setup();
  selectionStore.setDraft({ selectAll: true });
  selectionStore.confirmSelection();
  programStore.setProgram(SAMPLE_PROGRAM);
  selectionStore.clear();
  assert(selectionStore.getSelection().confirmed === false, 'cleared confirmed');
  assert(selectionStore.getSelection().draftSequenceNumbers.length === 0, 'cleared draft');

  // no program
  programStore.clearProgram();
  selectionStore.clear();
  assert(selectionStore.getSelection() === null, 'no program -> null selection');
  r = selectionStore.setDraft({ selectAll: true });
  assert(!r.ok, 'setDraft without program fails');

  console.log('G6 selectionStore tests: PASS');
}

run();
