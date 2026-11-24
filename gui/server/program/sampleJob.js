'use strict';

const { validateTestProgram } = require('./validateTestProgram');

function rawJob(teeth, options = {}) {
  const plate = options.plate || 'A';
  const orderUid = options.orderUid || 'ORD-1';
  const buildJobNumber = options.buildJobNumber || 'TEST';
  return {
    schemaVersion: 2,
    unit: 'mm',
    job: {
      buildJobNumber,
      plates: [
        {
          plate,
          orders: [
            {
              orderUid,
              teeth: teeth.map((tooth, index) => ({
                toothNumber: tooth.toothNumber != null ? tooth.toothNumber : index + 1,
                point: { x: tooth.x, y: tooth.y, z: tooth.z },
              })),
            },
          ],
        },
      ],
    },
  };
}

function normalizedJob(teeth, options = {}) {
  const validation = validateTestProgram(rawJob(teeth, options));
  if (!validation.ok) {
    throw new Error(validation.errors.map((error) => error.message).join('; '));
  }
  return validation.value;
}

module.exports = {
  rawJob,
  normalizedJob,
};
