/**
 * Deterministic planned path for GUI preview.
 *
 * Data source: geometric approach waypoints that mirror ProcedureController
 * command legs (Safe-Z → XY @ Safe-Z → Z coarse → Z fine). NOT a live
 * MotionPlanner dry-run — force-driven Phase 1–3 Z and Machine::home() limit
 * seeks cannot be predicted from targets alone, so they are omitted rather
 * than drawn as misleading chords.
 *
 * For pure Safe-Z / XY / Z legs, each segment is identical to the TCP path
 * the MotionPlanner produces for the same moveTo (independent axes; planar
 * or vertical moves are straight).
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
export function buildApproachSegments(from, target, safeZ, options = {}) {
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
    segments.push({ type: 'SafeZ', from: { ...cur }, to, speedLabel: 'v_approach' });
    cur = to;
  }

  if (!sameXY(cur, target)) {
    const to = { x: target.x, y: target.y, z: safeZ };
    segments.push({ type: 'XY', from: { ...cur }, to, speedLabel: 'v_approach' });
    cur = to;
  }

  const coarseZ = useStandoff ? target.z + standoff : target.z;
  if (!near(cur.z, coarseZ)) {
    const to = { x: cur.x, y: cur.y, z: coarseZ };
    segments.push({
      type: useStandoff && includeFine ? 'ZCoarse' : 'Z',
      from: { ...cur },
      to,
      speedLabel: 'v_approach',
    });
    cur = to;
  }

  if (includeFine && useStandoff && !near(cur.z, target.z)) {
    const to = { x: cur.x, y: cur.y, z: target.z };
    segments.push({ type: 'ZFine', from: { ...cur }, to, speedLabel: 'v_slow' });
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
export function buildProcedurePlannedPath(startPose, machinePoints, safeZ, probeCenter, options = {}) {
  const segments = [];
  const vertices = [];
  if (!startPose || !Number.isFinite(safeZ)) {
    return { segments, vertices };
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
    // Probe: Safe-Z → XY → coarse to standoff (M19 continues from there — no fine to probe.z).
    const probeSegs = buildApproachSegments(cur, probeCenter, safeZ, {
      approachStandoffMm: standoff,
      includeFine: false,
    });
    for (const s of probeSegs) {
      segments.push({ ...s, phase: 'ProbeApproach' });
      vertices.push({ ...s.to });
      cur = { ...s.to };
    }
    // Expected raise back to Safe-Z after M19 (MoveSafePose).
    if (!near(cur.z, safeZ)) {
      const to = { x: cur.x, y: cur.y, z: safeZ };
      segments.push({
        type: 'SafeZ',
        from: { ...cur },
        to,
        speedLabel: 'v_approach',
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

    // Unclamp: raise to standoff then Safe-Z (deterministic). Force-driven
    // insert/extract/optimize legs are omitted — they are not known pre-start.
    const standZ = Number.isFinite(standoff) ? p.z + standoff : p.z;
    if (!near(cur.z, standZ)) {
      const toStand = { x: cur.x, y: cur.y, z: standZ };
      segments.push({
        type: 'ZFine',
        from: { ...cur },
        to: toStand,
        speedLabel: 'v_slow',
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
        speedLabel: 'v_approach',
        phase: 'NextPoint',
        pointIndex: i,
      });
      vertices.push({ ...toSafe });
      cur = toSafe;
    }
  }

  // MoveSafeEnd: Core raises Safe-Z then Machine::home() (limit seek). Do not
  // invent an XY park at measuredHome — that is not the commanded TCP path.
  if (!near(cur.z, safeZ)) {
    const toSafe = { x: cur.x, y: cur.y, z: safeZ };
    segments.push({
      type: 'SafeZ',
      from: { ...cur },
      to: toSafe,
      speedLabel: 'v_approach',
      phase: 'MoveSafeEnd',
    });
    vertices.push({ ...toSafe });
  }

  return { segments, vertices };
}
