'use strict';

/**
 * In-memory results store for G9 (last procedure run only).
 */

let currentResults = null;

function clear() {
  currentResults = null;
}

function setResults(results) {
  currentResults = results;
}

function getResults() {
  return currentResults;
}

module.exports = {
  clear,
  setResults,
  getResults,
};
