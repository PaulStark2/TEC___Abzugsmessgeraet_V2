'use strict';

/**
 * Job-JSON for company test points.
 * Everything outside `job` is ignored. Coordinates are plate-frame millimetres.
 * `point.z` is the clamp floor (maximum depth), not the approach height.
 */

const ID_SEP = '|';

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

function toothId(plate, orderUid, toothNumber) {
  return `${plate}${ID_SEP}${orderUid}${ID_SEP}${toothNumber}`;
}

function toothLabel(plate, orderUid, toothNumber) {
  return `Platte ${plate} · ${orderUid} · Zahn ${toothNumber}`;
}

function pushError(errors, path, message) {
  errors.push({ path, message });
}

/**
 * Flat teeth in file order (plates, then orders, then teeth as stored).
 * @param {{ plates?: Array }} program normalized program
 */
function listTeeth(program) {
  const out = [];
  const plates = program && Array.isArray(program.plates) ? program.plates : [];
  for (const plate of plates) {
    const orders = Array.isArray(plate.orders) ? plate.orders : [];
    for (const order of orders) {
      const teeth = Array.isArray(order.teeth) ? order.teeth : [];
      for (const tooth of teeth) {
        const id = tooth.id || toothId(plate.plate, order.orderUid, tooth.toothNumber);
        out.push({
          id,
          plate: plate.plate,
          orderUid: order.orderUid,
          toothNumber: tooth.toothNumber,
          name: tooth.name || toothLabel(plate.plate, order.orderUid, tooth.toothNumber),
          x: tooth.x,
          y: tooth.y,
          z: tooth.z,
        });
      }
    }
  }
  return out;
}

function validateTooth(raw, path, errors, seenTeeth) {
  if (!isPlainObject(raw)) {
    pushError(errors, path, 'Each tooth must be an object');
    return null;
  }
  if (!('toothNumber' in raw) || !isInteger(raw.toothNumber)) {
    pushError(errors, `${path}.toothNumber`, 'toothNumber must be an integer');
  } else if (seenTeeth.has(raw.toothNumber)) {
    pushError(errors, `${path}.toothNumber`, `toothNumber ${raw.toothNumber} is duplicated in this order`);
  } else {
    seenTeeth.add(raw.toothNumber);
  }
  const point = raw.point;
  if (!isPlainObject(point)) {
    pushError(errors, `${path}.point`, 'point must be an object with x, y, z');
    return null;
  }
  for (const axis of ['x', 'y', 'z']) {
    if (!(axis in point) || !isFiniteNumber(point[axis])) {
      pushError(errors, `${path}.point.${axis}`, `${axis} must be a finite number`);
    }
  }
  if (!isInteger(raw.toothNumber) || !isPlainObject(point)) {
    return null;
  }
  if (!['x', 'y', 'z'].every((axis) => isFiniteNumber(point[axis]))) {
    return null;
  }
  return {
    toothNumber: raw.toothNumber,
    x: point.x,
    y: point.y,
    z: point.z,
  };
}

function validateTestProgram(rawObject) {
  const errors = [];
  if (!isPlainObject(rawObject)) {
    return { ok: false, errors: [{ path: '', message: 'Root must be a JSON object' }] };
  }
  if (!isPlainObject(rawObject.job)) {
    pushError(
      errors,
      'job',
      'Missing job. Expected the company job file (buildJobNumber, plates, orders, teeth). The flat testPoints format is no longer accepted.'
    );
    return { ok: false, errors };
  }

  const job = rawObject.job;
  let buildJobNumber = '';
  if (!isNonEmptyString(job.buildJobNumber)) {
    pushError(errors, 'job.buildJobNumber', 'buildJobNumber must be a non-empty string');
  } else {
    buildJobNumber = job.buildJobNumber.trim();
  }

  if (!Array.isArray(job.plates) || job.plates.length < 1) {
    pushError(errors, 'job.plates', 'plates must be a non-empty array');
    return { ok: false, errors };
  }

  const seenPlates = new Set();
  const plates = [];

  for (let pi = 0; pi < job.plates.length; pi += 1) {
    const rawPlate = job.plates[pi];
    const platePath = `job.plates[${pi}]`;
    if (!isPlainObject(rawPlate)) {
      pushError(errors, platePath, 'Each plate must be an object');
      continue;
    }
    if (!isNonEmptyString(rawPlate.plate)) {
      pushError(errors, `${platePath}.plate`, 'plate must be a non-empty string');
      continue;
    }
    const plateId = rawPlate.plate.trim();
    if (seenPlates.has(plateId)) {
      pushError(errors, `${platePath}.plate`, `plate ${plateId} is duplicated`);
      continue;
    }
    seenPlates.add(plateId);

    if (!Array.isArray(rawPlate.orders) || rawPlate.orders.length < 1) {
      pushError(errors, `${platePath}.orders`, 'orders must be a non-empty array');
      continue;
    }

    const seenOrders = new Set();
    const orders = [];
    for (let oi = 0; oi < rawPlate.orders.length; oi += 1) {
      const rawOrder = rawPlate.orders[oi];
      const orderPath = `${platePath}.orders[${oi}]`;
      if (!isPlainObject(rawOrder)) {
        pushError(errors, orderPath, 'Each order must be an object');
        continue;
      }
      if (!isNonEmptyString(rawOrder.orderUid)) {
        pushError(errors, `${orderPath}.orderUid`, 'orderUid must be a non-empty string');
        continue;
      }
      const orderUid = rawOrder.orderUid.trim();
      if (seenOrders.has(orderUid)) {
        pushError(errors, `${orderPath}.orderUid`, `orderUid ${orderUid} is duplicated on this plate`);
        continue;
      }
      seenOrders.add(orderUid);

      if (!Array.isArray(rawOrder.teeth) || rawOrder.teeth.length < 1) {
        pushError(errors, `${orderPath}.teeth`, 'teeth must be a non-empty array');
        continue;
      }

      const seenTeeth = new Set();
      const teeth = [];
      for (let ti = 0; ti < rawOrder.teeth.length; ti += 1) {
        const tooth = validateTooth(rawOrder.teeth[ti], `${orderPath}.teeth[${ti}]`, errors, seenTeeth);
        if (tooth) {
          teeth.push({
            ...tooth,
            id: toothId(plateId, orderUid, tooth.toothNumber),
            name: toothLabel(plateId, orderUid, tooth.toothNumber),
          });
        }
      }
      if (teeth.length > 0) {
        orders.push({ orderUid, teeth });
      }
    }
    if (orders.length > 0) {
      plates.push({ plate: plateId, orders });
    }
  }

  if (errors.length > 0) {
    return { ok: false, errors };
  }
  if (plates.length < 1) {
    pushError(errors, 'job.plates', 'No valid plate remained');
    return { ok: false, errors };
  }

  const program = {
    schemaVersion: 2,
    unit: 'mm',
    buildJobNumber,
    programName: buildJobNumber,
    plates,
  };
  program.testPoints = listTeeth(program);
  if (program.testPoints.length < 1) {
    return { ok: false, errors: [{ path: 'job', message: 'Job contains no teeth' }] };
  }

  return { ok: true, value: program };
}

/**
 * Serialize a normalized program back to a job file object.
 * @param {object} program
 * @param {object} [preservedRoot] extra root keys to keep (not job)
 */
function toJobFile(program, preservedRoot) {
  const root = isPlainObject(preservedRoot) ? { ...preservedRoot } : {};
  delete root.job;
  delete root.schemaVersion;
  delete root.unit;
  const plates = (program.plates || []).map((plate) => ({
    plate: plate.plate,
    orders: (plate.orders || []).map((order) => ({
      orderUid: order.orderUid,
      teeth: (order.teeth || []).map((tooth) => ({
        toothNumber: tooth.toothNumber,
        point: { x: tooth.x, y: tooth.y, z: tooth.z },
      })),
    })),
  }));
  return {
    ...root,
    schemaVersion: 2,
    unit: 'mm',
    job: {
      buildJobNumber: program.buildJobNumber,
      plates,
    },
  };
}

module.exports = {
  ID_SEP,
  toothId,
  toothLabel,
  listTeeth,
  validateTestProgram,
  toJobFile,
};
