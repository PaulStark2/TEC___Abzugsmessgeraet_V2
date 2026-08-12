'use strict';

const programStore = require('../server/program/programStore');

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

function run() {
  programStore.clearProgram();
  assert(programStore.getProgram() === null, 'store should start empty');

  const p = { version: 1, programName: 'X', testPoints: [{ name: 'A', sequenceNumber: 0, x: 0, y: 0, z: 0 }] };
  programStore.setProgram(p);
  assert(programStore.getProgram() !== null, 'store should have program after setProgram');
  assert(programStore.getProgram().version === 1, 'version stored');

  programStore.clearProgram();
  assert(programStore.getProgram() === null, 'store should be empty after clearProgram');

  console.log('G5 programStore tests: PASS');
}

run();

