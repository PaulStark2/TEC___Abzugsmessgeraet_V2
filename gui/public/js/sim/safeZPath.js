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
export function buildProcedurePlannedPath(startPose, machinePoints, safeZ, probeCenter, options = {}) {
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

    // Unclamp: raise to standoff then Safe-Z (deterministic). Force-driven
    // insert/extract/optimize legs are omitted — they are not known pre-start.
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

  // MoveSafeEnd: Core raises Safe-Z then Machine::home() (limit seek). Do not
  // invent an XY park at measuredHome — that is not the commanded TCP path.
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

function toRawPose(machinePose, referenceOffset) {
  const ox = Number(referenceOffset && referenceOffset.x);
  const oy = Number(referenceOffset && referenceOffset.y);
  const oz = Number(referenceOffset && referenceOffset.z);
  return {
    x: Number(machinePose.x) + (Number.isFinite(ox) ? ox : 0),
    y: Number(machinePose.y) + (Number.isFinite(oy) ? oy : 0),
    z: Number(machinePose.z) + (Number.isFinite(oz) ? oz : 0),
  };
}

function axisOutside(value, min, max, axis) {
  if (Number.isFinite(min) && value < min - 1e-6) {
    return { axis, side: 'min', value, limit: min };
  }
  if (Number.isFinite(max) && value > max + 1e-6) {
    return { axis, side: 'max', value, limit: max };
  }
  return null;
}

/**
 * Planned TCP vertices are machine-frame. Plant limits are raw.
 * raw = machine + referenceOffset (identity when offset is omitted).
 */
export function findWorkspaceViolation(vertices, limits, referenceOffset) {
  if (!limits) {
    return { ok: true, skipped: true };
  }
  const list = Array.isArray(vertices) ? vertices : [];
  for (let i = 0; i < list.length; i += 1) {
    const pose = list[i];
    if (!pose || ![pose.x, pose.y, pose.z].every(Number.isFinite)) {
      continue;
    }
    const raw = toRawPose(pose, referenceOffset);
    const hit =
      axisOutside(raw.x, limits.xMin, limits.xMax, 'X') ||
      axisOutside(raw.y, limits.yMin, limits.yMax, 'Y') ||
      axisOutside(raw.z, limits.zMin, limits.zMax, 'Z');
    if (hit) {
      const sign = hit.side === 'max' ? '+' : '−';
      return {
        ok: false,
        code: 'workspace',
        error:
          `Geplante Bahn liegt außerhalb des Bauraums (${hit.axis}${sign}). ` +
          'Die Fahrt kann nicht ausgeführt werden.',
        detail: { ...hit, index: i, raw },
      };
    }
  }
  return { ok: true };
}
