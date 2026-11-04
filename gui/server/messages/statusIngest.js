'use strict';

const messageStore = require('./messageStore');
const { lookupFault, lookupAbort } = require('./messageCatalog');

let prev = null;

function contextFromStatus(status) {
  if (!status || typeof status !== 'object') {
    return null;
  }
  return {
    machineState: status.machineState,
    processState: status.processState,
    procedurePhase: status.procedurePhase,
    homingState: status.homingState,
    x: status.x,
    y: status.y,
    z: status.z,
    faultReason: status.faultReason,
    faultReasonOrdinal: status.faultReasonOrdinal,
    abortReason: status.abortReason,
    abortReasonOrdinal: status.abortReasonOrdinal,
  };
}

/**
 * Edge-detect machine status and record HMI messages.
 * @param {object} status GUI status snapshot
 */
function ingestStatus(status) {
  if (!status || typeof status !== 'object') {
    return;
  }
  const cur = status;
  const p = prev;
  prev = {
    connected: Boolean(cur.connected),
    machineState: cur.machineState,
    processState: cur.processState,
    homingState: cur.homingState,
    faultReasonOrdinal: Number(cur.faultReasonOrdinal) || 0,
    abortReasonOrdinal: Number(cur.abortReasonOrdinal) || 0,
    eStop: Boolean(cur.eStop),
  };

  if (!p) {
    return;
  }

  const ctx = contextFromStatus(cur);

  if (p.connected && cur.connected === false) {
    messageStore.record({
      severity: 'warning',
      code: 'Gui.ConnectionLost',
      source: 'comm',
      context: ctx,
    });
  }
  if (!p.connected && cur.connected === true) {
    messageStore.record({
      severity: 'info',
      code: 'Gui.ConnectionRestored',
      source: 'comm',
      sticky: false,
      context: ctx,
    });
    messageStore.clearActiveByCode('Gui.ConnectionLost');
  }

  if (p.machineState !== 'Fault' && cur.machineState === 'Fault') {
    const f = lookupFault(cur.faultReasonOrdinal);
    messageStore.record({
      severity: 'fault',
      code: f.code,
      faultReasonOrdinal: cur.faultReasonOrdinal,
      source: 'core',
      context: ctx,
    });
  }
  if (p.machineState === 'Fault' && cur.machineState !== 'Fault') {
    messageStore.clearActiveByCode(lookupFault(p.faultReasonOrdinal).code);
  }

  if (!p.eStop && cur.eStop) {
    messageStore.record({
      severity: 'fault',
      code: 'Fault.EStop',
      faultReasonOrdinal: 1,
      source: 'core',
      context: ctx,
    });
  }
  if (p.eStop && !cur.eStop) {
    messageStore.clearActiveByCode('Fault.EStop');
  }

  if (p.processState !== 'Aborted' && cur.processState === 'Aborted') {
    const abortOrd = Number(cur.abortReasonOrdinal) || 0;
    if (abortOrd > 0) {
      const a = lookupAbort(abortOrd);
      messageStore.record({
        severity: 'error',
        code: a.code,
        abortReasonOrdinal: abortOrd,
        source: 'core',
        context: ctx,
      });
    } else {
      messageStore.record({
        severity: 'error',
        code: 'Abort.Unspecified',
        abortReasonOrdinal: 1,
        title: 'Prozess abgebrochen',
        detail: `Prozess abgebrochen (Phase: ${cur.procedurePhase || '—'}).`,
        source: 'core',
        context: ctx,
      });
    }
  }
  // Leave Aborted (new start / Idle): strip soft-abort actives; history stays.
  if (p.processState === 'Aborted' && cur.processState !== 'Aborted') {
    messageStore.clearActiveByCodePrefix('Abort.');
  }

  if (p.homingState !== 'HomingError' && cur.homingState === 'HomingError') {
    messageStore.record({
      severity: 'error',
      code: 'Gui.HomingError',
      source: 'core',
      context: ctx,
    });
  }
  if (p.homingState === 'HomingError' && cur.homingState !== 'HomingError') {
    messageStore.clearActiveByCode('Gui.HomingError');
  }
}

module.exports = {
  ingestStatus,
};
