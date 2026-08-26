/**
 * Deterministic planned path for GUI preview.
 *
 * Data source: geometric approach waypoints that mirror ProcedureController
 * command legs (Safe-Z → XY @ Safe-Z → Z coarse → Z fine). NOT a live
 * MotionPlanner dry-run — force-driven Phase 1–3 Z and Machine::home() limit
 * seeks cannot be predicted from targets alone, so they are omitted rather
 * than drawn as misleading chords.
 *
 * Corners are drawn sharp (X then Y). Live motion may blend when corner_radius > 0.
 * previewNote: "Vorschau ohne Kurvenradius"
 */

function near(a, b, eps = 1e-3) {
  return Math.abs(a - b) <= eps;
}

function sameXY(a, b) {
  return near(a.x, b.x) && near(a.y, b.y);
}

/**
 * @param {{x:number,y:number,z:number}} from
 * @param {{x:number,y:number,z:number}} target
 * @param {number} safeZ
 * @param {{ approachStandoffMm?: number, includeFine?: boolean }} [options]
 */
function buildApproachSegments(from, target, safeZ, options = {}) {
  const segments = [];
  if (!from || !target || !Number.isFinite(safeZ)) {
    return segments;
  }
  const standoff = Number(options.approachStandoffMm);
  const useStandoff = Number.isFinite(standoff) && standoff > 0;
  const includeFine = options.includeFine !== false;
  let cur = { x: from.x, y: from.y, z: from.z };

  if (!near(cur.z, safeZ)) {
    const to = { x: cur.x, y: cur.y, z: safeZ };
    segments.push({ type: 'SafeZ', from: { ...cur }, to, speedLabel: 'v_approach_testpoints' });
    cur = to;
  }

  if (!sameXY(cur, target)) {
    // Core MotionPlanner: X then Y at Safe-Z (never simultaneous XY).
    if (!near(cur.x, target.x)) {
      const toX = { x: target.x, y: cur.y, z: safeZ };
      segments.push({ type: 'X', from: { ...cur }, to: toX, speedLabel: 'v_approach_testpoints' });
      cur = toX;
    }
    if (!near(cur.y, target.y)) {
      const toY = { x: cur.x, y: target.y, z: safeZ };
      segments.push({ type: 'Y', from: { ...cur }, to: toY, speedLabel: 'v_approach_testpoints' });
      cur = toY;
    }
  }

  const coarseZ = useStandoff ? target.z + standoff : target.z;
  if (!near(cur.z, coarseZ)) {
    const to = { x: cur.x, y: cur.y, z: coarseZ };
    segments.push({
      type: useStandoff && includeFine ? 'ZCoarse' : 'Z',
      from: { ...cur },
      to,
      speedLabel: 'v_approach_testpoints',
    });
    cur = to;
  }

  if (includeFine && useStandoff && !near(cur.z, target.z)) {
    const to = { x: cur.x, y: cur.y, z: target.z };
    segments.push({ type: 'ZFine', from: { ...cur }, to, speedLabel: 'v_fine_testpoints' });
    cur = to;
  }

  return segments;
}

/**
 * @param {{x:number,y:number,z:number}} startPose frozen plan origin
 * @param {Array<{x:number,y:number,z:number}>} machinePoints selected points in machine frame
 * @param {number} safeZ
 * @param {{x:number,y:number,z:number}|null} probeCenter
 * @param {{ approachStandoffMm?: number, includeProbe?: boolean }} [options]
 */
function buildProcedurePlannedPath(startPose, machinePoints, safeZ, probeCenter, options = {}) {
  const segments = [];
  const vertices = [];
  if (!startPose || !Number.isFinite(safeZ)) {
    return { segments, vertices, previewNote: 'Vorschau ohne Kurvenradius' };
  }

  const standoff = Number(options.approachStandoffMm);
  const includeProbe = options.includeProbe !== false;

  let cur = { x: startPose.x, y: startPose.y, z: startPose.z };
  vertices.push({ ...cur });

  if (
    includeProbe &&
    probeCenter &&
    [probeCenter.x, probeCenter.y, probeCenter.z].every(Number.isFinite)
  ) {
    const probeSegs = buildApproachSegments(cur, probeCenter, safeZ, {
      approachStandoffMm: standoff,
      includeFine: false,
    });
    for (const s of probeSegs) {
      segments.push({ ...s, phase: 'ProbeApproach' });
      vertices.push({ ...s.to });
      cur = { ...s.to };
    }
    if (!near(cur.z, safeZ)) {
      const to = { x: cur.x, y: cur.y, z: safeZ };
      segments.push({
        type: 'SafeZ',
        from: { ...cur },
        to,
        speedLabel: 'v_approach_testpoints',
        phase: 'MoveSafePose',
      });
      vertices.push({ ...to });
      cur = to;
    }
  }

  const points = Array.isArray(machinePoints) ? machinePoints : [];
  for (let i = 0; i < points.length; i += 1) {
    const p = points[i];
    const segs = buildApproachSegments(cur, p, safeZ, {
      approachStandoffMm: standoff,
      includeFine: true,
    });
    for (const s of segs) {
      segments.push({ ...s, phase: 'PointApproach', pointIndex: i });
      vertices.push({ ...s.to });
      cur = { ...s.to };
    }

    const standZ = Number.isFinite(standoff) ? p.z + standoff : p.z;
    if (!near(cur.z, standZ)) {
      const toStand = { x: cur.x, y: cur.y, z: standZ };
      segments.push({
        type: 'ZFine',
        from: { ...cur },
        to: toStand,
        speedLabel: 'v_fine_testpoints',
        phase: 'Unclamp',
        pointIndex: i,
      });
      vertices.push({ ...toStand });
      cur = toStand;
    }
    if (!near(cur.z, safeZ)) {
      const toSafe = { x: cur.x, y: cur.y, z: safeZ };
      segments.push({
        type: 'SafeZ',
        from: { ...cur },
        to: toSafe,
        speedLabel: 'v_approach_testpoints',
        phase: 'NextPoint',
        pointIndex: i,
      });
      vertices.push({ ...toSafe });
      cur = toSafe;
    }
  }

  if (!near(cur.z, safeZ)) {
    const toSafe = { x: cur.x, y: cur.y, z: safeZ };
    segments.push({
      type: 'SafeZ',
      from: { ...cur },
      to: toSafe,
      speedLabel: 'v_approach_testpoints',
      phase: 'MoveSafeEnd',
    });
    vertices.push({ ...toSafe });
  }

  return { segments, vertices, previewNote: 'Vorschau ohne Kurvenradius' };
}

module.exports = { buildApproachSegments, buildProcedurePlannedPath };
