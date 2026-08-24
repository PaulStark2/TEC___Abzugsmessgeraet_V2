'use strict';

const assert = require('assert');
const {
  physicalGeometryFromMachineConfig,
  plantProbeDiscCenterOnly,
  probeVisualFromSources,
} = require('./physicalGeometry');

function testConfigIsAuthoritative() {
  const config = {
    measuring_head_diameter: 12,
    measuring_head_thickness: 4,
    tool_diameter: 3.5,
    tool_cylindrical_height: 18,
  };
  const geo = physicalGeometryFromMachineConfig(config);
  assert.strictEqual(geo.diameter, 12);
  assert.strictEqual(geo.thickness, 4);
  assert.strictEqual(geo.toolDiameter, 3.5);
  assert.strictEqual(geo.toolCylindricalHeight, 18);

  const visual = probeVisualFromSources({
    plantProbe: {
      x: 40,
      y: 41,
      z: 9,
      diameter: 99,
      thickness: 99,
      toolDiameter: 99,
      toolCylindricalHeight: 99,
    },
    machineConfig: config,
  });
  assert.strictEqual(visual.x, 40);
  assert.strictEqual(visual.y, 41);
  assert.strictEqual(visual.z, 9);
  assert.strictEqual(visual.diameter, 12);
  assert.strictEqual(visual.thickness, 4);
  assert.strictEqual(visual.toolDiameter, 3.5);
  assert.strictEqual(visual.toolCylindricalHeight, 18);
}

function testPlantCommandStripsGeometry() {
  const stripped = plantProbeDiscCenterOnly({
    x: 1,
    y: 2,
    z: 3,
    diameter: 99,
    thickness: 88,
    toolDiameter: 77,
    toolCylindricalHeight: 66,
  });
  assert.deepStrictEqual(stripped, { x: 1, y: 2, z: 3 });
  assert.strictEqual(Object.prototype.hasOwnProperty.call(stripped, 'diameter'), false);
  assert.deepStrictEqual(plantProbeDiscCenterOnly({ diameter: 9 }), {});
}

testConfigIsAuthoritative();
testPlantCommandStripsGeometry();
console.log('physicalGeometry.test.js: PASS');
