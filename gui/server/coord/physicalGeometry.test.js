'use strict';

const assert = require('assert');
const {
  DEFAULT_PHYSICAL_GEOMETRY,
  physicalGeometryFromMachineConfig,
  plantProbeDiscCenterOnly,
  probeVisualFromSources,
  resolvePhysicalGeometry,
} = require('./physicalGeometry');

function testMachineConfigNoLongerOwnsGeometry() {
  assert.strictEqual(
    physicalGeometryFromMachineConfig({
      measuring_head_diameter: 12,
      measuring_head_thickness: 4,
      tool_diameter: 3.5,
      tool_cylindrical_height: 18,
    }),
    null
  );
  assert.strictEqual(physicalGeometryFromMachineConfig(null), null);
}

function testPlantGeometryWithDefaults() {
  const geo = resolvePhysicalGeometry({ diameter: 12, thickness: 4 });
  assert.strictEqual(geo.diameter, 12);
  assert.strictEqual(geo.thickness, 4);
  assert.strictEqual(geo.toolDiameter, DEFAULT_PHYSICAL_GEOMETRY.toolDiameter);
  assert.strictEqual(geo.toolCylindricalHeight, DEFAULT_PHYSICAL_GEOMETRY.toolCylindricalHeight);

  const visual = probeVisualFromSources({
    plantProbe: {
      x: 40,
      y: 41,
      z: 9,
      diameter: 12,
      thickness: 4,
      toolDiameter: 3.5,
      toolCylindricalHeight: 18,
    },
    machineConfig: { measuring_head_diameter: 99 },
  });
  assert.strictEqual(visual.x, 40);
  assert.strictEqual(visual.y, 41);
  assert.strictEqual(visual.z, 9);
  assert.strictEqual(visual.diameter, 12);
  assert.strictEqual(visual.thickness, 4);
  assert.strictEqual(visual.toolDiameter, 3.5);
  assert.strictEqual(visual.toolCylindricalHeight, 18);

  const defaultsOnly = probeVisualFromSources({ plantProbe: { x: 1, y: 2, z: 3 } });
  assert.strictEqual(defaultsOnly.diameter, DEFAULT_PHYSICAL_GEOMETRY.diameter);
  assert.strictEqual(defaultsOnly.toolDiameter, DEFAULT_PHYSICAL_GEOMETRY.toolDiameter);
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

testMachineConfigNoLongerOwnsGeometry();
testPlantGeometryWithDefaults();
testPlantCommandStripsGeometry();
console.log('physicalGeometry.test.js: PASS');
