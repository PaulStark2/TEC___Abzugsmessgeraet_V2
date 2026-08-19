'use strict';

const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { GROUPS, defaultMachineConfigPayload, validateMachineConfigPayload } = require('../server/config/parameterCatalog');

function run() {
  const cfg = defaultMachineConfigPayload();
  assert.strictEqual(cfg.v_slow, 2);
  assert.strictEqual(cfg.tool_offset_z, 0);
  assert.strictEqual(cfg.safe_z, 10);
  assert.strictEqual(cfg.heartbeat_timeout_s, 0.3);
  assert.strictEqual(cfg.measuring_head_diameter, 8);
  assert.strictEqual(cfg.tool_cylindrical_height, 15);
  assert.strictEqual(cfg.probe_xy_search_margin, 2);
  assert.strictEqual(cfg.insert_search_distance, 10);
  assert.strictEqual(cfg.num_fine_searches, 3);
  const keys = new Set(GROUPS.flatMap((g) => g.fields.map((f) => f.key)));
  for (const key of Object.keys(cfg)) {
    assert.ok(keys.has(key), `catalog missing ${key}`);
  }
  for (const group of GROUPS) {
    for (const field of group.fields) {
      assert.ok(field.info && field.unit, `${field.key} needs info+unit`);
    }
  }
  assert.strictEqual(validateMachineConfigPayload(cfg).ok, true);
  const tooFast = { ...cfg, v_approach: 80 };
  assert.strictEqual(validateMachineConfigPayload(tooFast).ok, false);
  const nanCfg = { ...cfg, safe_z: Number.NaN };
  assert.strictEqual(validateMachineConfigPayload(nanCfg).ok, false);
  const future = GROUPS.flatMap((g) => g.fields).filter((f) => f.future).map((f) => f.key);
  assert.ok(future.includes('force_filter'));

  const tmp = path.join(os.tmpdir(), `m17-cfg-${Date.now()}.json`);
  fs.writeFileSync(tmp, JSON.stringify({ v_approach: 8 }), 'utf8');
  const parsed = { ...cfg, ...JSON.parse(fs.readFileSync(tmp, 'utf8')) };
  assert.strictEqual(parsed.v_approach, 8);
  assert.strictEqual(parsed.v_measure, 1);
  fs.unlinkSync(tmp);
  console.log('m17_config_tests.js: PASS');
}

run();
