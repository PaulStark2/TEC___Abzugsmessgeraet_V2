'use strict';

const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const {
  GROUPS,
  defaultMachineConfigPayload,
  validateMachineConfigPayload,
  evaluateCornerConsistencyHints,
} = require('../server/config/parameterCatalog');
const { migrateLegacyKeys } = require('../server/config/machineConfigStore');

function run() {
  const cfg = defaultMachineConfigPayload();
  assert.strictEqual(cfg.v_fine_testpoints, 2);
  assert.strictEqual(cfg.v_approach_testpoints, 10);
  assert.strictEqual(cfg.v_unclamp, 1);
  assert.strictEqual(cfg.a_testpoints, 0);
  assert.strictEqual(cfg.corner_testpoints, 0);
  assert.strictEqual(cfg.a_homing, 0);
  assert.strictEqual(cfg.v_search, 2);
  assert.strictEqual(cfg.v_search_fine, 1);
  assert.strictEqual(cfg.v_approach_probe, 10);
  assert.strictEqual(cfg.a_probe_brake, 500);
  assert.strictEqual(cfg.phase1_a, 0);
  assert.strictEqual(cfg.phase2_corner, 0);
  assert.strictEqual(cfg.tool_offset_z, 0);
  assert.strictEqual(cfg.safe_z_buffer, 30);
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
  const tooFast = { ...cfg, v_approach_testpoints: 80 };
  assert.strictEqual(validateMachineConfigPayload(tooFast).ok, false);
  const nanCfg = { ...cfg, safe_z_buffer: Number.NaN };
  assert.strictEqual(validateMachineConfigPayload(nanCfg).ok, false);
  const future = GROUPS.flatMap((g) => g.fields).filter((f) => f.future).map((f) => f.key);
  assert.ok(future.includes('force_filter'));

  const migrated = migrateLegacyKeys({
    v_approach: 8,
    v_slow: 1.5,
    v_measure: 0.8,
    acceleration: 12,
    probe_search_speed: 3,
    fine_search_speed: 0.5,
  });
  assert.strictEqual(migrated.v_approach_testpoints, 8);
  assert.strictEqual(migrated.v_fine_testpoints, 1.5);
  assert.strictEqual(migrated.v_unclamp, 0.8);
  assert.strictEqual(migrated.a_testpoints, 12);
  assert.strictEqual(migrated.v_search, 3);
  assert.strictEqual(migrated.v_search_fine, 0.5);
  assert.strictEqual(migrated.v_approach, undefined);

  const inconsistent = {
    ...cfg,
    corner_testpoints: 1,
    a_testpoints: 0,
    v_approach_testpoints: 10,
  };
  const hints = evaluateCornerConsistencyHints(inconsistent);
  assert.ok(hints.length >= 1);
  assert.ok(hints[0].message.includes('a > 0'));

  const tmp = path.join(os.tmpdir(), `m17-cfg-${Date.now()}.json`);
  fs.writeFileSync(tmp, JSON.stringify({ v_approach_testpoints: 8 }), 'utf8');
  const parsed = { ...cfg, ...JSON.parse(fs.readFileSync(tmp, 'utf8')) };
  assert.strictEqual(parsed.v_approach_testpoints, 8);
  assert.strictEqual(parsed.v_unclamp, 1);
  fs.unlinkSync(tmp);
  console.log('m17_config_tests.js: PASS');
}

run();
