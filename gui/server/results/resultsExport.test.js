'use strict';

const assert = require('node:assert/strict');
const { buildSamplesCsv, buildSummaryCsv, excelNum } = require('./resultsExport');
const { LEG_INSERT, LEG_EXTRACT } = require('./forcePathStore');

function testExcelNum() {
  assert.equal(excelNum(3.14159, 2), '3,14');
  assert.equal(excelNum(null), '');
}

function testSamplesCsvExcelDe() {
  const csv = buildSamplesCsv({
    points: [
      {
        pointIndex: 0,
        name: 'Kontakt A',
        curves: [
          {
            iteration: 0,
            leg: LEG_INSERT,
            samples: [
              { index: 0, z: 12, zRel: 0, fz: 0.1 },
              { index: 1, z: 10, zRel: -2, fz: 3.5 },
            ],
          },
        ],
      },
    ],
  });
  assert.ok(csv.startsWith('\uFEFF'));
  const body = csv.slice(1);
  assert.ok(body.includes('PunktIndex;PunktName;'));
  assert.ok(body.includes('z_rel_mm;F_N'));
  assert.ok(body.includes('3,5000'));
  assert.ok(!body.split('\n')[0].includes(','));
}

function testSummaryCsvPeaks() {
  const csv = buildSummaryCsv({
    points: [
      {
        pointIndex: 0,
        name: 'P1',
        insert: {
          count: 2,
          meanPeakN: 3.6,
          stddevPeakN: 0.1414,
          peaksN: [3.5, 3.7],
        },
        extract: {
          count: 1,
          meanPeakN: 2.8,
          stddevPeakN: null,
          peaksN: [2.8],
        },
      },
    ],
  });
  assert.ok(csv.startsWith('\uFEFF'));
  assert.ok(csv.includes('Spitzen_N'));
  assert.ok(csv.includes('3,5000;3,7000') || csv.includes('"3,5000;3,7000"'));
  assert.ok(csv.includes('Eingliedern'));
  assert.ok(csv.includes(String(LEG_EXTRACT)));
}

testExcelNum();
testSamplesCsvExcelDe();
testSummaryCsvPeaks();
console.log('resultsExport.test.js OK');
