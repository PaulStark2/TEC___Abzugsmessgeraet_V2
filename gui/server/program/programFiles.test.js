'use strict';

const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');

const programFiles = require('./programFiles');

function withTempProgramsDir(fn) {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'tec-programs-'));
  const originalDir = programFiles.PROGRAMS_DIR;
  const prefix = `test_${Date.now()}_`;
  try {
    fn(prefix, () => {
      const dir = programFiles.PROGRAMS_DIR;
      if (!fs.existsSync(dir)) {
        return;
      }
      for (const name of fs.readdirSync(dir)) {
        if (name.startsWith(prefix)) {
          fs.unlinkSync(path.join(dir, name));
        }
      }
    });
  } finally {
    try {
      fs.rmSync(tmp, { recursive: true, force: true });
    } catch {
      // ignore
    }
    void originalDir;
  }
}

withTempProgramsDir((prefix, cleanup) => {
  try {
    const create = programFiles.teachPoint({
      mode: 'create',
      buildJobNumber: `${prefix}Demo`,
      plateId: 'A',
      orderUid: 'ORD-1',
      toothNumber: 33,
      pose: { x: 1, y: 2, z: -3 },
    });
    assert.strictEqual(create.ok, true, create.error || 'create failed');
    assert.strictEqual(create.fileName, `${prefix}Demo.json`);
    assert.strictEqual(create.point.toothNumber, 33);
    assert.strictEqual(create.point.z, -3);
    assert.strictEqual(create.program.buildJobNumber, `${prefix}Demo`);

    const append = programFiles.teachPoint({
      mode: 'append',
      fileName: create.fileName,
      plateId: 'A',
      orderUid: 'ORD-1',
      toothNumber: 34,
      pose: { x: 4, y: 5, z: -6 },
    });
    assert.strictEqual(append.ok, true, append.error || 'append failed');
    assert.strictEqual(append.point.toothNumber, 34);
    assert.strictEqual(append.program.testPoints.length, 2);

    const dupTooth = programFiles.teachPoint({
      mode: 'append',
      fileName: create.fileName,
      plateId: 'A',
      orderUid: 'ORD-1',
      toothNumber: 33,
      pose: { x: 0, y: 0, z: 0 },
    });
    assert.strictEqual(dupTooth.ok, false);

    const listed = programFiles.listPrograms().filter((f) => f.fileName.startsWith(prefix));
    assert.ok(listed.some((f) => f.fileName === create.fileName && f.pointCount === 2));

    const dup = programFiles.teachPoint({
      mode: 'create',
      buildJobNumber: `${prefix}Demo`,
      plateId: 'A',
      orderUid: 'ORD-1',
      toothNumber: 11,
      pose: { x: 0, y: 0, z: 0 },
    });
    assert.strictEqual(dup.ok, false);

    assert.strictEqual(programFiles.sanitizeFileName('../evil.json'), null);
    assert.strictEqual(programFiles.sanitizeFileName('ok_file.json'), 'ok_file.json');
  } finally {
    cleanup();
  }
});

console.log('programFiles.test.js: PASS');
