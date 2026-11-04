'use strict';

/**
 * Motorsport-style G-G diagram (Claude demo look): grid rings, cloud, trail, live point.
 * Used for force (Fx/Fy + Fz strip) and velocity (vx/vy + vz strip).
 */

function cssVar(name, fallback) {
  try {
    const v = getComputedStyle(document.documentElement).getPropertyValue(name).trim();
    return v || fallback;
  } catch {
    return fallback;
  }
}

/**
 * Lock CSS box before setting bitmap size. Otherwise canvas width/height
 * attributes inflate the layout (esp. height) and the plot grows downward
 * every animation frame.
 */
function availableSquareCssPx(canvas) {
  const parent = canvas.parentElement;
  if (parent && parent.classList.contains('gg-layout')) {
    const style = getComputedStyle(parent);
    const gap = parseFloat(style.columnGap || style.gap) || 0;
    const strip = parent.querySelector('.gg-strip');
    const stripW = strip ? strip.getBoundingClientRect().width : 72;
    // Use clientWidth (layout width) of the row, not our possibly inflated rect.
    const rowW = parent.clientWidth || parent.getBoundingClientRect().width;
    return Math.max(1, Math.round(rowW - stripW - gap));
  }
  return Math.max(1, Math.round(canvas.clientWidth || canvas.getBoundingClientRect().width));
}

function fitSquare(canvas) {
  const dpr = window.devicePixelRatio || 1;
  const css = availableSquareCssPx(canvas);
  canvas.style.width = `${css}px`;
  canvas.style.height = `${css}px`;
  const px = Math.max(1, Math.round(css * dpr));
  if (canvas.width !== px || canvas.height !== px) {
    canvas.width = px;
    canvas.height = px;
  }
  return { css, dpr, px };
}

function fitStrip(canvas, squareCssPx) {
  const dpr = window.devicePixelRatio || 1;
  const cssW = Math.max(1, Math.round(canvas.clientWidth || 72));
  const cssH = Math.max(1, Math.round(squareCssPx || cssW));
  canvas.style.width = `${cssW}px`;
  canvas.style.height = `${cssH}px`;
  const w = Math.max(1, Math.round(cssW * dpr));
  const h = Math.max(1, Math.round(cssH * dpr));
  if (canvas.width !== w || canvas.height !== h) {
    canvas.width = w;
    canvas.height = h;
  }
  return { w, h, dpr, cssW, cssH };
}

/**
 * @param {object} opts
 * @param {HTMLCanvasElement} opts.xyCanvas
 * @param {HTMLCanvasElement} opts.zCanvas
 * @param {HTMLElement} opts.readEl
 * @param {string[]} opts.readLabels  e.g. ['Fx','Fy','|Fxy|']
 * @param {string[]} opts.axisLabels  [top, bottom, left, right]
 * @param {string} opts.zLabel
 * @param {string} opts.unit
 * @param {number} [opts.cloudMax]
 * @param {number} [opts.trailMax]
 */
export function createGgDiagram(opts) {
  const xyCanvas = opts.xyCanvas;
  const zCanvas = opts.zCanvas;
  const readEl = opts.readEl;
  const readLabels = opts.readLabels || ['X', 'Y', '|XY|'];
  const axisLabels = opts.axisLabels || ['+Y', '−Y', '−X', '+X'];
  const zLabel = opts.zLabel || 'Z';
  const unit = opts.unit || '';
  const cloudMax = opts.cloudMax || 180;
  const trailMax = opts.trailMax || 60;
  const autoScale = opts.autoScale !== false;
  // Floors so bore-search / fine motion stay readable (0.05 N, ~0.01 mm/s).
  let minXy = Math.max(1e-9, Number(opts.minScaleXy) || 0.08);
  let minZ = Math.max(1e-9, Number(opts.minScaleZ) || 0.08);
  let maxXy = Math.max(minXy, Number(opts.maxScaleXy) || 1e9);
  let maxZ = Math.max(minZ, Number(opts.maxScaleZ) || 1e9);
  const headroom = Number(opts.scaleHeadroom) || 1.35;
  const shrinkTau = Number(opts.scaleShrink) || 0.06; // per sample toward quieter envelope

  const initialXy = Math.max(minXy, Number(opts.initialScaleXy ?? opts.initialScale) || minXy);
  const initialZ = Math.max(minZ, Number(opts.initialScaleZ ?? opts.initialScale) || minZ);
  let viewXy = initialXy;
  let viewZ = initialZ;
  /** @type {Array<[number, number]>} */
  const cloud = [];
  /** @type {Array<[number, number]>} */
  const trail = [];
  /** @type {number[]} */
  const zTrail = [];
  let lastX = 0;
  let lastY = 0;
  let lastZ = 0;

  function fmtRing(v) {
    const n = Number(v);
    if (!Number.isFinite(n)) return '—';
    if (Math.abs(n) >= 10) return n.toFixed(0);
    if (Math.abs(n) >= 1) return n.toFixed(1);
    if (Math.abs(n) >= 0.1) return n.toFixed(2);
    return n.toFixed(3);
  }

  function clampScale(v, lo, hi) {
    return Math.min(hi, Math.max(lo, v));
  }

  /**
   * Autoscale from the recent trail window.
   * Expand immediately for new peaks; shrink slowly when quiet again.
   * Large shrink clears the long cloud so old points don't appear to explode outward.
   */
  function updateAutoScale() {
    if (!autoScale) return;
    let peakXy = 0;
    let peakZ = 0;
    for (let i = 0; i < trail.length; i += 1) {
      peakXy = Math.max(peakXy, Math.abs(trail[i][0]), Math.abs(trail[i][1]));
    }
    peakXy = Math.max(peakXy, Math.abs(lastX), Math.abs(lastY));
    for (let i = 0; i < zTrail.length; i += 1) {
      peakZ = Math.max(peakZ, Math.abs(zTrail[i]));
    }
    peakZ = Math.max(peakZ, Math.abs(lastZ));

    const targetXy = clampScale(Math.max(minXy, peakXy * headroom), minXy, maxXy);
    const targetZ = clampScale(Math.max(minZ, peakZ * headroom), minZ, maxZ);

    const prevXy = viewXy;
    const prevZ = viewZ;

    if (targetXy >= viewXy) {
      viewXy = targetXy;
    } else {
      viewXy = viewXy + (targetXy - viewXy) * shrinkTau;
      if (viewXy - targetXy < minXy * 0.02) viewXy = targetXy;
    }
    if (targetZ >= viewZ) {
      viewZ = targetZ;
    } else {
      viewZ = viewZ + (targetZ - viewZ) * shrinkTau;
      if (viewZ - targetZ < minZ * 0.02) viewZ = targetZ;
    }

    viewXy = clampScale(viewXy, minXy, maxXy);
    viewZ = clampScale(viewZ, minZ, maxZ);

    // Drop long history when zooming in a lot — avoids the "drift outward" illusion.
    if (prevXy > 0 && viewXy < prevXy * 0.82) cloud.length = 0;
    if (prevZ > 0 && viewZ < prevZ * 0.82) {
      // z cloud is only zTrail; trim older half
      if (zTrail.length > 10) zTrail.splice(0, Math.floor(zTrail.length / 2));
    }
  }

  /**
   * @param {number|{xy?:number,z?:number,minXy?:number,minZ?:number,maxXy?:number,maxZ?:number}} next
   */
  function setScale(next) {
    if (next && typeof next === 'object') {
      if (Number.isFinite(Number(next.minXy))) minXy = Math.max(1e-9, Number(next.minXy));
      if (Number.isFinite(Number(next.minZ))) minZ = Math.max(1e-9, Number(next.minZ));
      if (Number.isFinite(Number(next.maxXy))) maxXy = Math.max(minXy, Number(next.maxXy));
      if (Number.isFinite(Number(next.maxZ))) maxZ = Math.max(minZ, Number(next.maxZ));
      if (Number.isFinite(Number(next.xy))) {
        viewXy = clampScale(Number(next.xy), minXy, maxXy);
      }
      if (Number.isFinite(Number(next.z))) {
        viewZ = clampScale(Number(next.z), minZ, maxZ);
      }
      return;
    }
    const s = Math.max(1e-9, Number(next) || minXy);
    viewXy = clampScale(s, minXy, maxXy);
    viewZ = clampScale(s, minZ, maxZ);
  }

  function clear() {
    cloud.length = 0;
    trail.length = 0;
    zTrail.length = 0;
    lastX = 0;
    lastY = 0;
    lastZ = 0;
    viewXy = clampScale(Math.max(minXy, initialXy), minXy, maxXy);
    viewZ = clampScale(Math.max(minZ, initialZ), minZ, maxZ);
  }

  function pushSample(x, y, z) {
    const nx = Number(x) || 0;
    const ny = Number(y) || 0;
    const nz = Number(z) || 0;
    lastX = nx;
    lastY = ny;
    lastZ = nz;

    cloud.push([nx, ny]);
    if (cloud.length > cloudMax) cloud.shift();
    trail.push([nx, ny]);
    if (trail.length > trailMax) trail.shift();
    zTrail.push(nz);
    if (zTrail.length > trailMax) zTrail.shift();

    updateAutoScale();
  }

  function draw() {
    const accent = cssVar('--gg-accent', cssVar('--accent', '#d8221a'));
    const grid = cssVar('--gg-grid', cssVar('--border', '#2e3945'));
    const muted = cssVar('--gg-muted', cssVar('--muted', '#8f9aa6'));

    const squareFit = fitSquare(xyCanvas);

    // XY plot
    {
      const m = squareFit;
      const s = m.px;
      const f = m.dpr;
      const c = xyCanvas.getContext('2d');
      c.clearRect(0, 0, s, s);
      const cx = s / 2;
      const cy = s / 2;
      const u = (s / 2 - 26 * f) / 5;

      c.font = `${12 * f}px "Barlow Semi Condensed","Segoe UI Narrow",Arial,sans-serif`;
      c.strokeStyle = grid;
      c.fillStyle = muted;
      c.lineWidth = f;
      for (let r = 1; r <= 5; r += 1) {
        c.beginPath();
        c.arc(cx, cy, r * u, 0, Math.PI * 2);
        c.stroke();
        c.fillText(fmtRing((viewXy * r) / 5), cx + 3 * f, cy - r * u - 3 * f);
      }
      c.beginPath();
      c.moveTo(cx - 5 * u, cy);
      c.lineTo(cx + 5 * u, cy);
      c.moveTo(cx, cy - 5 * u);
      c.lineTo(cx, cy + 5 * u);
      c.stroke();

      c.textAlign = 'center';
      c.fillText(axisLabels[0], cx, 14 * f);
      c.fillText(axisLabels[1], cx, s - 6 * f);
      c.textAlign = 'left';
      c.fillText(axisLabels[2], 4 * f, cy - 5 * f);
      c.textAlign = 'right';
      c.fillText(axisLabels[3], s - 4 * f, cy - 5 * f);

      c.setLineDash([6 * f, 5 * f]);
      c.strokeStyle = muted;
      c.beginPath();
      c.arc(cx, cy, 5 * u * 0.92, 0, Math.PI * 2);
      c.stroke();
      c.setLineDash([]);

      const toPx = (vx, vy) => [
        cx + (vx / viewXy) * 5 * u,
        cy - (vy / viewXy) * 5 * u,
      ];

      c.fillStyle = accent;
      c.globalAlpha = 0.22;
      for (let i = 0; i < cloud.length; i += 1) {
        const p = toPx(cloud[i][0], cloud[i][1]);
        c.beginPath();
        c.arc(p[0], p[1], 1.8 * f, 0, Math.PI * 2);
        c.fill();
      }
      c.globalAlpha = 1;
      c.strokeStyle = accent;
      c.lineWidth = 2.5 * f;
      c.lineCap = 'round';
      for (let j = 1; j < trail.length; j += 1) {
        c.globalAlpha = (j / trail.length) * 0.8;
        const a = toPx(trail[j - 1][0], trail[j - 1][1]);
        const b = toPx(trail[j][0], trail[j][1]);
        c.beginPath();
        c.moveTo(a[0], a[1]);
        c.lineTo(b[0], b[1]);
        c.stroke();
      }
      c.globalAlpha = 1;
      const live = toPx(lastX, lastY);
      c.fillStyle = accent;
      c.beginPath();
      c.arc(live[0], live[1], 6 * f, 0, Math.PI * 2);
      c.fill();
    }

    // Z strip
    {
      const m = fitStrip(zCanvas, squareFit.css);
      const c = zCanvas.getContext('2d');
      c.clearRect(0, 0, m.w, m.h);
      const f = m.dpr;
      const cx = m.w / 2;
      const cy = m.h / 2;
      const half = m.h / 2 - 18 * f;

      c.strokeStyle = grid;
      c.lineWidth = f;
      c.beginPath();
      c.moveTo(cx, cy - half);
      c.lineTo(cx, cy + half);
      c.moveTo(cx - 10 * f, cy);
      c.lineTo(cx + 10 * f, cy);
      c.stroke();

      c.fillStyle = muted;
      c.font = `${11 * f}px "Barlow Semi Condensed","Segoe UI Narrow",Arial,sans-serif`;
      c.textAlign = 'center';
      c.fillText(`+${zLabel}`, cx, 12 * f);
      c.fillText(`−${zLabel}`, cx, m.h - 4 * f);
      c.fillText(fmtRing(viewZ), cx, cy - half + 14 * f);

      c.setLineDash([5 * f, 4 * f]);
      c.strokeStyle = muted;
      c.beginPath();
      c.moveTo(cx - 8 * f, cy - half * 0.92);
      c.lineTo(cx + 8 * f, cy - half * 0.92);
      c.moveTo(cx - 8 * f, cy + half * 0.92);
      c.lineTo(cx + 8 * f, cy + half * 0.92);
      c.stroke();
      c.setLineDash([]);

      const toY = (vz) => cy - (vz / viewZ) * half;
      c.strokeStyle = accent;
      c.lineWidth = 2.2 * f;
      for (let j = 1; j < zTrail.length; j += 1) {
        c.globalAlpha = (j / zTrail.length) * 0.8;
        c.beginPath();
        c.moveTo(cx, toY(zTrail[j - 1]));
        c.lineTo(cx, toY(zTrail[j]));
        c.stroke();
      }
      c.globalAlpha = 1;
      c.fillStyle = accent;
      c.beginPath();
      c.arc(cx, toY(lastZ), 5 * f, 0, Math.PI * 2);
      c.fill();
    }

    if (readEl) {
      const mag = Math.sqrt(lastX * lastX + lastY * lastY);
      const fmt = (v) => {
        const n = Number(v);
        if (!Number.isFinite(n)) return '—';
        const a = Math.abs(n);
        const digits = a >= 10 ? 1 : a >= 1 ? 2 : a >= 0.1 ? 3 : 4;
        return `${n.toFixed(digits)}${unit ? ` ${unit}` : ''}`;
      };
      readEl.innerHTML = `
        <div>${readLabels[0]}<strong>${fmt(lastX)}</strong></div>
        <div>${readLabels[1]}<strong>${fmt(lastY)}</strong></div>
        <div>${readLabels[2]}<strong>${fmt(mag)}</strong></div>
        <div>${zLabel}<strong>${fmt(lastZ)}</strong></div>
      `;
    }
  }

  return {
    pushSample,
    setScale,
    clear,
    draw,
  };
}

/**
 * Derive velocity from pose samples with EMA smoothing.
 */
export function createVelocityEstimator(alpha = 0.35) {
  let prev = null;
  let vx = 0;
  let vy = 0;
  let vz = 0;

  function reset() {
    prev = null;
    vx = 0;
    vy = 0;
    vz = 0;
  }

  function update(pose, motionState) {
    const now = performance.now();
    const x = Number(pose && pose.x);
    const y = Number(pose && pose.y);
    const z = Number(pose && pose.z);
    if (![x, y, z].every(Number.isFinite)) {
      return { vx, vy, vz };
    }
    if (!prev) {
      prev = { x, y, z, t: now };
      return { vx: 0, vy: 0, vz: 0 };
    }
    const dt = (now - prev.t) / 1000;
    if (dt <= 1e-4 || dt > 0.5 || motionState === 'Idle') {
      prev = { x, y, z, t: now };
      if (motionState === 'Idle') {
        vx = 0;
        vy = 0;
        vz = 0;
      }
      return { vx, vy, vz };
    }
    const rx = (x - prev.x) / dt;
    const ry = (y - prev.y) / dt;
    const rz = (z - prev.z) / dt;
    vx = alpha * rx + (1 - alpha) * vx;
    vy = alpha * ry + (1 - alpha) * vy;
    vz = alpha * rz + (1 - alpha) * vz;
    prev = { x, y, z, t: now };
    return { vx, vy, vz };
  }

  return { update, reset };
}
