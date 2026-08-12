'use strict';

let currentProgram = null; // normalized program (validated) or null

function setProgram(program) {
  currentProgram = program;
}

function getProgram() {
  return currentProgram;
}

function clearProgram() {
  currentProgram = null;
}

module.exports = {
  setProgram,
  getProgram,
  clearProgram,
};

