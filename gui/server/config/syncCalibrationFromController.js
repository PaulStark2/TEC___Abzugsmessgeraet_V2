'use strict';

/**
 * Persist toolOffset (and optionally probeCenter) from MsgMachineConfig.
 *
 * persistProbeCenter=true  → FullCalib/Install: write probe_center_* + tool
 * persistProbeCenter=false → OffsetOnly: update tool only; never rewrite probeCenter
 *
 * Never reconstructs probeCenter as probe_offset_reference − toolOffset.
 */

const calibrationStore = require('./calibrationStore');
const machineConfigStore = require('./machineConfigStore');

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
  const toolPose = {
    x: c.tool_offset_x,
    y: c.tool_offset_y,
    z: c.tool_offset_z,
  };

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
  } else if (calib.probeCenterValid && calib.probeCenter) {
    calib = calibrationStore.save({
      ...calib,
      toolOffset: toolPose,
    });
  }

  return {
    ok: true,
    calibration: calib,
    config: machineConfigStore.load(),
    persistProbeCenter,
    legacySemanticsWarning: calibrationStore.isLegacyToolOffsetSemantics(calib),
  };
}

module.exports = {
  syncCalibrationFromController,
};
