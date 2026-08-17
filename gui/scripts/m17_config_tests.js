'use strict';

const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { GROUPS, defaultMachineConfigPayload } = require('../server/config/parameterCatalog');

function run() {
  const cfg = defaultMachineConfigPayload();
  assert.strictEqual(cfg.v_slow, 2);
  assert.strictEqual(cfg.tool_offset_z, 0);
  const keys = new Set(GROUPS.flatMap((g) => g.fields.map((f) => f.key)));
  for (const key of Object.keys(cfg)) {
    assert.ok(keys.has(key), `catalog missing ${key}`);
  }
  for (const group of GROUPS) {
    for (const field of group.fields) {
      assert.ok(field.info && field.unit, `${field.key} needs info+unit`);
    }
  }

  const tmp = path.join(os.tmpdir(), `m17-cfg-${Date.now()}.json`);
  fs.writeFileSync(tmp, JSON.stringify({ v_approach: 8 }), 'utf8');
  const parsed = { ...cfg, ...JSON.parse(fs.readFileSync(tmp, 'utf8')) };
  assert.strictEqual(parsed.v_approach, 8);
  assert.strictEqual(parsed.v_measure, 1);
  fs.unlinkSync(tmp);
  console.log('m17_config_tests.js: PASS');
}

run();
