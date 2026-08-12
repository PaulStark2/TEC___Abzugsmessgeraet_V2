'use strict';

const MAX_TEST_POINTS = 32;

function isPlainObject(v) {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

function isFiniteNumber(v) {
  return typeof v === 'number' && Number.isFinite(v);
}

function isNonEmptyString(v) {
  return typeof v === 'string' && v.trim().length > 0;
}

function isInteger(v) {
  return typeof v === 'number' && Number.isInteger(v);
}

function validateTestPoint(rawPoint, idx, seenSequenceNumbers) {
  const errors = [];

  const allowedKeys = new Set(['name', 'sequenceNumber', 'x', 'y', 'z']);
  for (const k of Object.keys(rawPoint)) {
    if (!allowedKeys.has(k)) {
      errors.push({
        path: `testPoints[${idx}].${k}`,
        message: 'Unknown property',
      });
    }
  }

  if (!('name' in rawPoint)) {
    errors.push({ path: `testPoints[${idx}].name`, message: 'Missing name' });
  } else if (!isNonEmptyString(rawPoint.name)) {
    errors.push({ path: `testPoints[${idx}].name`, message: 'name must be a non-empty string' });
  }

  if (!('sequenceNumber' in rawPoint)) {
    errors.push({
      path: `testPoints[${idx}].sequenceNumber`,
      message: 'Missing sequenceNumber',
    });
  } else if (!isInteger(rawPoint.sequenceNumber)) {
    errors.push({
      path: `testPoints[${idx}].sequenceNumber`,
      message: 'sequenceNumber must be an integer',
    });
  } else if (rawPoint.sequenceNumber < 0) {
    errors.push({
      path: `testPoints[${idx}].sequenceNumber`,
      message: 'sequenceNumber must be >= 0',
    });
  }

  for (const axis of ['x', 'y', 'z']) {
    if (!(axis in rawPoint)) {
      errors.push({
        path: `testPoints[${idx}].${axis}`,
        message: `Missing ${axis}`,
      });
      continue;
    }
    if (!isFiniteNumber(rawPoint[axis])) {
      errors.push({
        path: `testPoints[${idx}].${axis}`,
        message: `${axis} must be a finite number`,
      });
    }
  }

  if (isInteger(rawPoint.sequenceNumber) && rawPoint.sequenceNumber >= 0) {
    const seq = rawPoint.sequenceNumber;
    if (!seenSequenceNumbers.has(seq)) {
      seenSequenceNumbers.set(seq, []);
    }
    seenSequenceNumbers.get(seq).push(idx);
  }

  return errors;
}

function validateTestProgram(rawObject) {
  const errors = [];

  const allowedRootKeys = new Set(['version', 'programName', 'testPoints']);
  if (!isPlainObject(rawObject)) {
    return { ok: false, errors: [{ path: '', message: 'Root must be a JSON object' }] };
  }

  for (const k of Object.keys(rawObject)) {
    if (!allowedRootKeys.has(k)) {
      errors.push({ path: k, message: 'Unknown property' });
    }
  }

  // version (optional; default 1)
  let version = 1;
  if ('version' in rawObject) {
    if (!isInteger(rawObject.version) || rawObject.version < 1) {
      errors.push({ path: 'version', message: 'version must be an integer >= 1' });
    } else {
      version = rawObject.version;
    }
  }

  // programName (optional)
  let programName;
  if ('programName' in rawObject) {
    if (!isNonEmptyString(rawObject.programName)) {
      errors.push({ path: 'programName', message: 'programName must be a non-empty string' });
    } else {
      programName = rawObject.programName.trim();
    }
  }

  if (!('testPoints' in rawObject)) {
    errors.push({ path: 'testPoints', message: 'Missing testPoints' });
    return { ok: false, errors };
  }

  const rawPoints = rawObject.testPoints;
  if (!Array.isArray(rawPoints)) {
    errors.push({ path: 'testPoints', message: 'testPoints must be an array' });
    return { ok: false, errors };
  }

  if (rawPoints.length < 1) {
    errors.push({ path: 'testPoints', message: 'testPoints must contain at least one point' });
  }

  if (rawPoints.length > MAX_TEST_POINTS) {
    errors.push({
      path: 'testPoints',
      message: `testPoints must have at most ${MAX_TEST_POINTS} points`,
    });
  }

  if (rawPoints.length === 0) {
    return { ok: false, errors };
  }

  const seenSequenceNumbers = new Map(); // seq -> [idx...]

  for (let i = 0; i < rawPoints.length; i++) {
    const pt = rawPoints[i];
    if (!isPlainObject(pt)) {
      errors.push({
        path: `testPoints[${i}]`,
        message: 'Each testPoint must be an object',
      });
      continue;
    }
    errors.push(...validateTestPoint(pt, i, seenSequenceNumbers));
  }

  for (const [seq, idxs] of seenSequenceNumbers.entries()) {
    if (idxs.length > 1) {
      for (const dupIdx of idxs) {
        errors.push({
          path: `testPoints[${dupIdx}].sequenceNumber`,
          message: `sequenceNumber ${seq} is duplicated`,
        });
      }
    }
  }

  if (errors.length > 0) {
    return { ok: false, errors };
  }

  const normalizedPoints = rawPoints
    .map((pt) => ({
      name: pt.name.trim(),
      sequenceNumber: pt.sequenceNumber,
      x: pt.x,
      y: pt.y,
      z: pt.z,
    }))
    .sort((a, b) => a.sequenceNumber - b.sequenceNumber);

  return {
    ok: true,
    value: {
      version,
      programName,
      testPoints: normalizedPoints,
    },
  };
}

module.exports = {
  validateTestProgram,
};

