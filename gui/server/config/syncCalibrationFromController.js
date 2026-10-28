'use strict';

/**
 * Persist toolOffset (and optionally probeCenter) from MsgMachineConfig.
 *
 * persistProbeCenter=true  → FullCalib/Install: write probe_center_* + tool
 * persistProbeCenter=false → OffsetOnly: update tool only; never rewrite probeCenter
 *
 * Never reconstructs probeCenter as probe_offset_reference − toolOffset.
 *
 * OffsetOnly also compares |new − old| toolOffset against
 * tool_offset_change_tolerance_mm (GUI-only param). Above tolerance → anomaly flag.
 */

const calibrationStore = require('./calibrationStore');
const machineConfigStore = require('./machineConfigStore');

/** Euclidean 3D distance between two tool-offset poses [mm]. */
function toolOffsetDeltaMm(a, b) {
  if (!a || !b) {
    return null;
  }
  const dx = Number(a.x) - Number(b.x);
  const dy = Number(a.y) - Number(b.y);
  const dz = Number(a.z) - Number(b.z);
  if (![dx, dy, dz].every(Number.isFinite)) {
    return null;
  }
  return Math.hypot(dx, dy, dz);
}

function resolveToleranceMm(cfg) {
  const raw = cfg && cfg.tool_offset_change_tolerance_mm;
  const n = Number(raw);
  if (Number.isFinite(n) && n >= 0) {
    return n;
  }
  return 0.5;
}

async function syncCalibrationFromController(machineClient, options = {}) {
  const persistProbeCenter = options.persistProbeCenter === true;
  if (!machineClient || typeof machineClient.getMachineConfig !== 'function') {
    return { ok: false, error: 'Kalibrierungssync ist auf diesem Backend nicht verfügbar' };
  }
  const status =
    typeof machineClient.getStatus === 'function' ? machineClient.getStatus() : { connected: true };
  if (status.connected === false) {
    return { ok: false, error: 'Controller nicht verbunden' };
  }

  const cfgResult = await machineClient.getMachineConfig();
  if (!cfgResult || !cfgResult.ok || !cfgResult.config) {
    return { ok: false, error: (cfgResult && cfgResult.error) || 'CmdGetMachineConfig failed' };
  }
  const c = cfgResult.config;
  const storedCfg = machineConfigStore.load();
  machineConfigStore.save({
    ...storedCfg,
    tool_offset_x: c.tool_offset_x,
    tool_offset_y: c.tool_offset_y,
    tool_offset_z: c.tool_offset_z,
  });

  let calib = calibrationStore.load();
  const previousTool = calib.toolOffset
    ? { x: calib.toolOffset.x, y: calib.toolOffset.y, z: calib.toolOffset.z }
    : { x: 0, y: 0, z: 0 };
  const toolPose = {
    x: c.tool_offset_x,
    y: c.tool_offset_y,
    z: c.tool_offset_z,
  };

  let toolOffsetChangeAnomaly = false;
  let toolOffsetDelta = null;
  const toleranceMm = resolveToleranceMm(storedCfg);

  if (persistProbeCenter) {
    if (!status.probeCenterValid) {
      return { ok: false, error: 'probeCenter not valid on controller after Install' };
    }
    const px = Number(c.probe_center_x);
    const py = Number(c.probe_center_y);
    const pz = Number(c.probe_center_z);
    if (![px, py, pz].every(Number.isFinite)) {
      return { ok: false, error: 'MsgMachineConfig missing probe_center_*' };
    }
    calib = calibrationStore.setProbeCenter({ x: px, y: py, z: pz }, toolPose);
    calib = calibrationStore.clearToolOffsetAnomaly();
  } else if (calib.probeCenterValid && calib.probeCenter) {
    toolOffsetDelta = toolOffsetDeltaMm(previousTool, toolPose);
    calib = calibrationStore.save({
      ...calib,
      toolOffset: toolPose,
    });
    if (toolOffsetDelta !== null && toolOffsetDelta > toleranceMm) {
      toolOffsetChangeAnomaly = true;
      calib = calibrationStore.setToolOffsetAnomaly({
        previous: previousTool,
        current: toolPose,
        deltaMm: toolOffsetDelta,
        toleranceMm,
      });
    } else {
      calib = calibrationStore.clearToolOffsetAnomaly();
    }
  }

  return {
    ok: true,
    calibration: calib,
    config: machineConfigStore.load(),
    persistProbeCenter,
    legacySemanticsWarning: calibrationStore.isLegacyToolOffsetSemantics(calib),
    toolOffsetChangeAnomaly,
    toolOffsetDeltaMm: toolOffsetDelta,
    toolOffsetChangeToleranceMm: toleranceMm,
  };
}

module.exports = {
  syncCalibrationFromController,
  toolOffsetDeltaMm,
};
