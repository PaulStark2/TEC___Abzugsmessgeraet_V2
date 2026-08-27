'use strict';

const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');

const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'sim-plant-'));
const tmpFile = path.join(tmpDir, 'sim-plant.json');
process.env.SIM_PLANT_STORE_PATH = tmpFile;

const storePath = require.resolve('./simPlantStore');
delete require.cache[storePath];
const store = require('./simPlantStore');

try {
  const defaults = store.defaultSimPlantPayload();
  assert.strictEqual(defaults.limits.xMax, 100);
  assert.strictEqual(defaults.probeDisc.x, 40);
  assert.strictEqual(defaults.forceStiffnessNPerMm, 5);

  const loadedDefaults = store.load();
  assert.deepStrictEqual(loadedDefaults.limits, defaults.limits);

  const saved = store.save({
    limits: { xMax: 150 },
    probeDisc: { x: 45, y: 46, z: 12 },
    forceStiffnessNPerMm: 8,
  });
  assert.strictEqual(saved.limits.xMax, 150);
  assert.strictEqual(saved.limits.xMin, 0);
  assert.strictEqual(saved.probeDisc.x, 45);
  assert.strictEqual(saved.forceStiffnessNPerMm, 8);

  delete require.cache[storePath];
  const reloaded = require('./simPlantStore').load();
  assert.strictEqual(reloaded.limits.xMax, 150);
  assert.strictEqual(reloaded.probeDisc.y, 46);
  assert.strictEqual(reloaded.forceStiffnessNPerMm, 8);

  console.log('simPlantStore.test.js: PASS');
} finally {
  delete process.env.SIM_PLANT_STORE_PATH;
  delete require.cache[storePath];
  fs.rmSync(tmpDir, { recursive: true, force: true });
}
