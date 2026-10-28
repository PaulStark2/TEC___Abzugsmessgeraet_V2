'use strict';

/**
 * Evaluate K1–K2–K3 L-shape for plate yaw about Z.
 * Convention: K2→K1 = plate +Y, K2→K3 = plate +X. Nominal edge 55 mm.
 * Mirrors core/PlateRotation.hpp.
 */

const DEG = Math.PI / 180;

function wrapPi(a) {
  let x = a;
  while (x > Math.PI) x -= 2 * Math.PI;
  while (x < -Math.PI) x += 2 * Math.PI;
  return x;
}

function defaultThresholds() {
  return {
    nom_edge_mm: 55,
    tol_length_warn_mm: 0.3,
    tol_length_reject_mm: 1.0,
    tol_yaw_ignore_deg: 0.05,
    tol_yaw_apply_deg: 0.2,
    tol_ortho_warn_deg: 0.2,
    tol_ortho_reject_deg: 1.0,
    tol_parallel_warn_deg: 0.2,
  };
}

function evaluatePlateRotation(k1, k2, k3) {
  const v12x = k1.x - k2.x;
  const v12y = k1.y - k2.y;
  const v23x = k3.x - k2.x;
  const v23y = k3.y - k2.y;
  const L12 = Math.hypot(v12x, v12y);
  const L23 = Math.hypot(v23x, v23y);
  const theta_y_rad = Math.atan2(-v12x, v12y);
  const theta_x_rad = Math.atan2(v23y, v23x);
  const yaw_rad = 0.5 * (theta_x_rad + theta_y_rad);
  const yaw_split_rad = Math.abs(wrapPi(theta_x_rad - theta_y_rad));
  const dot = v12x * v23x + v12y * v23y;
  const cross = v12x * v23y - v12y * v23x;
  const ang = Math.atan2(cross, dot);
  const ortho_err_rad = Math.abs(Math.abs(ang) - Math.PI / 2);
  const zMean = (k1.z + k2.z + k3.z) / 3;
  const z_span_mm = Math.max(Math.abs(k1.z - zMean), Math.abs(k2.z - zMean), Math.abs(k3.z - zMean));
  return {
    L12,
    L23,
    theta_x_rad,
    theta_y_rad,
    yaw_rad,
    yaw_deg: (yaw_rad * 180) / Math.PI,
    ortho_err_rad,
    ortho_err_deg: (ortho_err_rad * 180) / Math.PI,
    yaw_split_rad,
    yaw_split_deg: (yaw_split_rad * 180) / Math.PI,
    z_span_mm,
  };
}

function decidePlateRotation(evalResult, thresholds) {
  const t = { ...defaultThresholds(), ...(thresholds || {}) };
  const ignore = t.tol_yaw_ignore_deg * DEG;
  const apply = t.tol_yaw_apply_deg * DEG;
  const orthoWarn = t.tol_ortho_warn_deg * DEG;
  const orthoReject = t.tol_ortho_reject_deg * DEG;
  const parallelWarn = t.tol_parallel_warn_deg * DEG;

  const d12 = Math.abs(evalResult.L12 - t.nom_edge_mm);
  const d23 = Math.abs(evalResult.L23 - t.nom_edge_mm);
  const dLen = Math.max(d12, d23);

  let length = 'ok';
  let may_apply = true;
  if (dLen >= t.tol_length_reject_mm) {
    length = 'reject';
    may_apply = false;
  } else if (dLen >= t.tol_length_warn_mm) {
    length = 'warn';
  }

  let ortho = 'ok';
  if (evalResult.ortho_err_rad >= orthoReject) {
    ortho = 'reject';
    may_apply = false;
  } else if (evalResult.ortho_err_rad >= orthoWarn) {
    ortho = 'warn';
  }

  let parallel = 'ok';
  if (evalResult.yaw_split_rad >= parallelWarn) {
    parallel = 'warn';
  }

  const absYaw = Math.abs(evalResult.yaw_rad);
  let yaw = 'ok';
  let recommended_yaw_rad = 0;
  if (absYaw < ignore) {
    recommended_yaw_rad = 0;
  } else if (absYaw >= apply) {
    yaw = 'warn';
    recommended_yaw_rad = evalResult.yaw_rad;
  } else {
    recommended_yaw_rad = evalResult.yaw_rad;
  }

  return { length, ortho, parallel, yaw, recommended_yaw_rad, may_apply, thresholds: t };
}

module.exports = {
  defaultThresholds,
  evaluatePlateRotation,
  decidePlateRotation,
};
