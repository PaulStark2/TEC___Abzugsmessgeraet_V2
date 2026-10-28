'use strict';

/** In-memory force-path curves streamed from MsgForcePathChunk. Not a database. */

const LEG_INSERT = 0;
const LEG_EXTRACT = 1;

let curves = [];

function clear() {
  curves = [];
}

function appendChunk(chunk) {
  if (!chunk || !Array.isArray(chunk.z) || !Array.isArray(chunk.fz)) {
    return;
  }
  const pointIndex = Number(chunk.pointIndex);
  const iteration = Number(chunk.iteration);
  const leg = Number(chunk.leg);
  if (!Number.isFinite(leg) || (leg !== LEG_INSERT && leg !== LEG_EXTRACT)) {
    return;
  }
  let curve = curves.find(
    (c) => c.pointIndex === pointIndex && c.iteration === iteration && c.leg === leg
  );
  if (!curve) {
    curve = {
      pointIndex,
      iteration,
      leg,
      dropped: 0,
      samples: [],
    };
    curves.push(curve);
  }
  curve.dropped = Number(chunk.dropped) || curve.dropped;
  for (let i = 0; i < chunk.z.length; ++i) {
    curve.samples.push({ z: chunk.z[i], fz: chunk.fz[i] });
  }
}

function getCurves() {
  return curves.map((c) => ({
    ...c,
    samples: c.samples.map((s) => ({ ...s })),
  }));
}

module.exports = {
  LEG_INSERT,
  LEG_EXTRACT,
  clear,
  appendChunk,
  getCurves,
};
