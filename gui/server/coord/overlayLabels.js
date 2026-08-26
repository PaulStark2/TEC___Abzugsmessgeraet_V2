'use strict';

/**
 * Overlay copy for the same planned Safe-Z segments / machine points as the 3D path.
 * Does not plan a second trajectory.
 */

function isFinitePoint(p) {
  return Boolean(p && [p.x, p.y, p.z].every((v) => typeof v === 'number' && Number.isFinite(v)));
}

function formatCoord(value) {
  return Number(value).toFixed(1);
}

function pointId(point, index) {
  if (point && point.sequenceNumber !== undefined && point.sequenceNumber !== null) {
    return String(point.sequenceNumber);
  }
  if (point && point.name) {
    return String(point.name);
  }
  return String(index + 1);
}

function buildPointLabels(points) {
  if (!Array.isArray(points)) {
    return [];
  }
  const labels = [];
  points.forEach((point, index) => {
    if (!isFinitePoint(point)) {
      return;
    }
    const id = pointId(point, index);
    labels.push({
      kind: 'point',
      id,
      x: point.x,
      y: point.y,
      z: point.z,
      text: `${id}  ${formatCoord(point.x)}, ${formatCoord(point.y)}, ${formatCoord(point.z)}`,
    });
  });
  return labels;
}

function formatSigned(value) {
  const n = Number(value);
  const abs = Math.abs(n).toFixed(1);
  return `${n >= 0 ? '+' : '−'}${abs}`;
}

function segmentDirection(seg) {
  const dx = seg.to.x - seg.from.x;
  const dy = seg.to.y - seg.from.y;
  const dz = seg.to.z - seg.from.z;
  if (seg.type === 'SafeZ' || seg.type === 'Z' || seg.type === 'ZCoarse' || seg.type === 'ZFine' ||
      seg.type === 'ZMeasure') {
    return dz >= 0 ? '+Z' : '−Z';
  }
  if (seg.type === 'XY') {
    return `${formatSigned(dx)}, ${formatSigned(dy)}`;
  }
  return `${formatSigned(dx)}, ${formatSigned(dy)}, ${formatSigned(dz)}`;
}

function formatSpeed(seg, config) {
  const label = seg && seg.speedLabel ? String(seg.speedLabel) : '';
  if (!config) {
    return label;
  }
  if (label === 'v_approach_testpoints' && Number.isFinite(Number(config.v_approach_testpoints))) {
    return `v_approach_testpoints ${Number(config.v_approach_testpoints)} mm/s`;
  }
  if (label === 'v_fine_testpoints' && Number.isFinite(Number(config.v_fine_testpoints))) {
    return `v_fine_testpoints ${Number(config.v_fine_testpoints)} mm/s`;
  }
  if (label === 'v_unclamp' && Number.isFinite(Number(config.v_unclamp))) {
    return `v_unclamp ${Number(config.v_unclamp)} mm/s`;
  }
  if (label === 'v_fine_testpoints/v_unclamp') {
    const fine = Number(config.v_fine_testpoints);
    const unclamp = Number(config.v_unclamp);
    if (Number.isFinite(fine) && Number.isFinite(unclamp)) {
      return `v_fine_testpoints ${fine} / v_unclamp ${unclamp} mm/s`;
    }
  }
  return label;
}

function buildSegmentLabels(segments, config) {
  if (!Array.isArray(segments)) {
    return [];
  }
  const labels = [];
  for (const seg of segments) {
    if (!seg || !isFinitePoint(seg.from) || !isFinitePoint(seg.to)) {
      continue;
    }
    labels.push({
      kind: 'segment',
      type: seg.type,
      speedLabel: seg.speedLabel || '',
      speedText: formatSpeed(seg, config),
      direction: segmentDirection(seg),
      x: (seg.from.x + seg.to.x) / 2,
      y: (seg.from.y + seg.to.y) / 2,
      z: (seg.from.z + seg.to.z) / 2,
      text: `${formatSpeed(seg, config)}  ${segmentDirection(seg)}`,
    });
  }
  return labels;
}

function collectOverlayLabels({
  points,
  segments,
  machineConfig,
  showPointLabels,
  showSegmentLabels,
}) {
  const out = [];
  if (showPointLabels) {
    out.push(...buildPointLabels(points));
  }
  if (showSegmentLabels) {
    out.push(...buildSegmentLabels(segments, machineConfig));
  }
  return out;
}

module.exports = {
  buildPointLabels,
  buildSegmentLabels,
  collectOverlayLabels,
  formatSpeed,
  segmentDirection,
};
