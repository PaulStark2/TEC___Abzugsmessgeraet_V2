'use strict';

/**
 * DE title/detail catalog for HMI messages.
 * Codes are stable keys shared with Core AbortReason ordinals and GUI sources.
 * See docs/HMI_Messages.md.
 */

const ABORT_BY_ORDINAL = Object.freeze({
  0: { code: 'Abort.None', title: 'Kein Abbruch', detail: 'Kein Soft-Abort latched.' },
  1: {
    code: 'Abort.Unspecified',
    title: 'Prozess abgebrochen',
    detail: 'Die Prozedur wurde abgebrochen (kein genauerer Grund vom Controller).',
  },
  2: {
    code: 'Abort.ForceTareBusy',
    title: 'Ruheprobe belegt',
    detail: 'Eine Ruheprobe läuft bereits; der nächste Schritt konnte nicht starten.',
  },
  3: {
    code: 'Abort.PointApproachXyMiss',
    title: 'Anfahrt XY verfehlt',
    detail: 'Vor der Z-Absenkung lag die Pose nicht am programmierten XY-Ziel (Soft-Limit?).',
  },
  4: {
    code: 'Abort.PointApproachSafeZ',
    title: 'Safe-Z verloren',
    detail: 'Während der Punktanfahrt war Safe-Z nicht mehr erreicht.',
  },
  5: {
    code: 'Abort.ProbeApproachSafeZLeg0',
    title: 'Probe: Safe-Z nicht erreicht',
    detail: 'Anheben auf Safe-Z vor der Einmess-Anfahrt ist fehlgeschlagen.',
  },
  6: {
    code: 'Abort.ProbeApproachXyMiss',
    title: 'Probe: XY verfehlt',
    detail: 'Vor der Z-Absenkung lag die Pose nicht über dem Einmesszentrum (tool_offset).',
  },
  7: {
    code: 'Abort.ProbeApproachSafeZLeg1',
    title: 'Probe: Safe-Z verlassen',
    detail: 'Vor der groben Z-Absenkung wurde Safe-Z verlassen.',
  },
  8: {
    code: 'Abort.ProbeBoreSearchFailed',
    title: 'Einmess-Bohrung fehlgeschlagen',
    detail: 'Die kraftbasierte Bohrungssuche (Einmessen) ist fehlgeschlagen.',
  },
  9: {
    code: 'Abort.ProbeCalibrationError',
    title: 'Einmessfehler',
    detail: 'ProbeCalibration meldete einen Fehler.',
  },
  10: {
    code: 'Abort.PlateBoreTimeout',
    title: 'Plattenbohrung Timeout',
    detail: 'Die Teach-Bohrungssuche hat das Zeitlimit überschritten.',
  },
  11: {
    code: 'Abort.PlateBoreWallNotFound',
    title: 'Plattenbohrung: Wand fehlt',
    detail: 'Bei der Teach-Suche wurde keine Bohrungswand gefunden.',
  },
  12: {
    code: 'Abort.PlateBoreFloorNotFound',
    title: 'Plattenbohrung: Boden fehlt',
    detail: 'Bei der Teach-Suche wurde kein Bohrungsboden gefunden.',
  },
  13: {
    code: 'Abort.PlateBoreSearchFailed',
    title: 'Plattenbohrung fehlgeschlagen',
    detail: 'Die Teach-Bohrungssuche ist fehlgeschlagen.',
  },
  14: {
    code: 'Abort.PlateBoreStartFailed',
    title: 'Plattenbohrung Start fehlgeschlagen',
    detail: 'Nach der Ruheprobe konnte die Teach-Suche nicht gestartet werden.',
  },
  15: {
    code: 'Abort.CalibApproachSafeZLeg0',
    title: 'Kalib-Anfahrt: Safe-Z',
    detail: 'Safe-Z-Calib vor der Teach-Anfahrt wurde nicht erreicht.',
  },
  16: {
    code: 'Abort.CalibApproachXyMiss',
    title: 'Kalib-Anfahrt: XY verfehlt',
    detail: 'Vor der Z-Absenkung lag die Pose nicht am Teach-Ziel.',
  },
  17: {
    code: 'Abort.CalibApproachSafeZLeg1',
    title: 'Kalib-Anfahrt: Safe-Z verlassen',
    detail: 'Safe-Z-Calib wurde vor der groben Z-Absenkung verlassen.',
  },
  18: {
    code: 'Abort.PreclampStartFailed',
    title: 'PreClamp Start fehlgeschlagen',
    detail: 'Die Bohrungssuche vor dem Klemmen konnte nicht gestartet werden.',
  },
  19: {
    code: 'Abort.PreclampTimeout',
    title: 'PreClamp Timeout',
    detail: 'Die PreClamp-Bohrungssuche hat das Zeitlimit überschritten.',
  },
  20: {
    code: 'Abort.PreclampWallNotFound',
    title: 'PreClamp: Wand fehlt',
    detail: 'Bei PreClamp wurde keine Bohrungswand gefunden.',
  },
  21: {
    code: 'Abort.PreclampSearchFailed',
    title: 'PreClamp fehlgeschlagen',
    detail: 'Die PreClamp-Bohrungssuche ist fehlgeschlagen.',
  },
  22: {
    code: 'Abort.ClampAfterTestStage',
    title: 'Clamp ungültiger Stage',
    detail: 'Clamp wurde in einem ungültigen point_stage erreicht.',
  },
  23: {
    code: 'Abort.InsertInitialNoSeatForce',
    title: 'Seat ohne Prüfkraft',
    detail: 'SeatToForce hat die Prüfkraft nicht erreicht (Bewegung beendet / Z-Min).',
  },
  24: {
    code: 'Abort.TestOrAgeNoKraftfreiExtract',
    title: 'Ausgliedern ohne Kraftfrei',
    detail: 'TestOrAge: Ausgliedern ohne kraftfreien Zustand.',
  },
  25: {
    code: 'Abort.TestOrAgeNoZugOnExtract',
    title: 'Ausgliedern ohne Zug/Kraftfrei',
    detail: 'TestOrAge: beim Ausgliedern wurde weder Zug noch kraftfrei erkannt.',
  },
  26: {
    code: 'Abort.TestOrAgeExtractAfterZug',
    title: 'Ausgliedern nach Zug fehlgeschlagen',
    detail: 'TestOrAge: nach erkannter Zugkraft kein kraftfreier Zustand.',
  },
  27: {
    code: 'Abort.TestOrAgeInsertNoTarget',
    title: 'Eingliedern ohne Prüfkraft',
    detail: 'TestOrAge: Eingliedern hat die Prüfkraft nicht erreicht.',
  },
  28: {
    code: 'Abort.TestOrAgeExtractNoKraftfrei',
    title: 'Ausgliedern ohne Kraftfrei',
    detail: 'TestOrAge: Zyklus-Ausgliedern ohne kraftfreien Zustand.',
  },
  29: {
    code: 'Abort.UnclampAfterTestReseatFailed',
    title: 'Unclamp: Re-Seat fehlgeschlagen',
    detail: 'Nach dem Test konnte nicht erneut bis zur Prüfkraft eingegliedert werden.',
  },
  30: {
    code: 'Abort.UnclampAfterTestSafeZ',
    title: 'Unclamp: Safe-Z nicht erreicht',
    detail: 'Nach dem Test wurde Safe-Z nicht erreicht.',
  },
  31: {
    code: 'Abort.ProbeRemeasureStartFailed',
    title: 'Nachmessung Start fehlgeschlagen',
    detail: 'OffsetOnly-Nachmessung konnte nicht gestartet werden.',
  },
  32: {
    code: 'Abort.ProbeRemeasureError',
    title: 'Nachmessung Fehler',
    detail: 'OffsetOnly-Nachmessung meldete einen Fehler.',
  },
  33: {
    code: 'Abort.ProbeRemeasureNotAtSafeZ',
    title: 'Nachmessung: nicht auf Safe-Z',
    detail: 'Vor/bei der Nachmessung war Safe-Z nicht erreicht.',
  },
  34: {
    code: 'Abort.ProbeRemeasureXyMiss',
    title: 'Nachmessung: XY verfehlt',
    detail: 'Nachmessung: XY-Ziel wurde nicht erreicht.',
  },
  35: {
    code: 'Abort.MoveSafePoseFailed',
    title: 'Safe-Pose fehlgeschlagen',
    detail: 'MoveSafePose konnte Safe-Z nicht sicher erreichen.',
  },
  36: {
    code: 'Abort.NextPointFailed',
    title: 'Nächster Punkt fehlgeschlagen',
    detail: 'Der Wechsel zum nächsten Prüfpunkt ist fehlgeschlagen.',
  },
  37: {
    code: 'Abort.SoftStop',
    title: 'Soft-STOP',
    detail: 'Die Prozedur wurde durch Soft-STOP / Abbruch beendet.',
  },
});

const FAULT_BY_ORDINAL = Object.freeze({
  0: { code: 'Fault.None', title: 'Kein Fault', detail: 'Kein latched FaultReason.' },
  1: {
    code: 'Fault.EStop',
    title: 'Not-Halt',
    detail: 'E-Stop ist aktiv oder war die Ursache des Faults.',
  },
  2: {
    code: 'Fault.ForceHardLimit',
    title: 'Kraft Hard-Limit',
    detail: 'Eine Achskraft hat das konfigurierte Hard-Limit überschritten.',
  },
  3: {
    code: 'Fault.ForceCollision',
    title: 'Kollisionskraft',
    detail: 'Phasenabhängiger Kollisionswächter hat ausgelöst.',
  },
  4: {
    code: 'Fault.ForceNonFinite',
    title: 'Kraft ungültig (NaN/Inf)',
    detail: 'Kraftsample war nicht endlich.',
  },
  5: {
    code: 'Fault.ForceInvalidSample',
    title: 'Kraftsample ungültig',
    detail: 'Host meldete forceSampleValid=false.',
  },
  6: {
    code: 'Fault.ForceStale',
    title: 'Kraftsample veraltet',
    detail: 'Kraftsample älter als sample_timeout_s.',
  },
  7: {
    code: 'Fault.CommunicationTimeout',
    title: 'Kommunikations-Timeout',
    detail: 'Host-Heartbeat während aktiver Guards zu alt (CmdNop).',
  },
  8: {
    code: 'Fault.ProcedureAbort',
    title: 'Prozedur-Abort (Fault)',
    detail: 'FaultReason ProcedureAbort (selten latched).',
  },
  9: {
    code: 'Fault.ProbeSearchNoContact',
    title: 'Einmessen ohne Kontakt',
    detail: 'Einmess-Bohrung: Timeout / Wand / Boden nicht gefunden.',
  },
  10: {
    code: 'Fault.ProbeApproachContact',
    title: 'ProbeApproach Kontakt (Legacy)',
    detail: 'Historales Messtaster-Ereignis; wird nicht mehr neu gelatcht.',
  },
});

const GUI_CODES = Object.freeze({
  'Gui.CommandRejected': {
    title: 'Befehl abgelehnt',
    detail: 'Ein HMI-Befehl wurde vom Server oder Controller abgelehnt.',
  },
  'Gui.ConnectionLost': {
    title: 'Verbindung getrennt',
    detail: 'Live-Status (WebSocket) zur Maschine ist unterbrochen.',
  },
  'Gui.ConnectionRestored': {
    title: 'Verbindung wiederhergestellt',
    detail: 'Live-Status zur Maschine ist wieder verbunden.',
  },
  'Gui.MachineOffline': {
    title: 'Maschine offline',
    detail: 'Der MachineClient meldet keine Live-Verbindung.',
  },
  'Gui.HomingError': {
    title: 'Homing-Fehler',
    detail: 'HomingState ist HomingError.',
  },
  'Gui.LimitWarning': {
    title: 'Endanschlag aktiv',
    detail: 'Mindestens eine Soft-/Limit-Richtung blockiert.',
  },
  'Gui.ProcedureAnomaly': {
    title: 'Prozedur-Anomalie',
    detail: 'Der Controller meldete eine Prozedur-Anomalie (Bestätigung nötig).',
  },
  'Comm.Nack': {
    title: 'Protokoll NACK',
    detail: 'Der Controller hat einen Befehl mit NACK beantwortet.',
  },
});

function lookupAbort(ordinal) {
  const n = Number(ordinal) || 0;
  return ABORT_BY_ORDINAL[n] || ABORT_BY_ORDINAL[1];
}

function lookupFault(ordinal) {
  const n = Number(ordinal) || 0;
  return FAULT_BY_ORDINAL[n] || FAULT_BY_ORDINAL[0];
}

function lookupGui(code) {
  return GUI_CODES[code] || { title: code, detail: code };
}

function resolveMessage({ code, abortReasonOrdinal, faultReasonOrdinal, title, detail }) {
  if (abortReasonOrdinal !== undefined && abortReasonOrdinal !== null && Number(abortReasonOrdinal) > 0) {
    const a = lookupAbort(abortReasonOrdinal);
    return {
      code: code || a.code,
      title: title || a.title,
      detail: detail || a.detail,
    };
  }
  if (faultReasonOrdinal !== undefined && faultReasonOrdinal !== null && Number(faultReasonOrdinal) > 0) {
    const f = lookupFault(faultReasonOrdinal);
    return {
      code: code || f.code,
      title: title || f.title,
      detail: detail || f.detail,
    };
  }
  if (code && GUI_CODES[code]) {
    const g = GUI_CODES[code];
    return { code, title: title || g.title, detail: detail || g.detail };
  }
  return {
    code: code || 'Gui.Unknown',
    title: title || 'Meldung',
    detail: detail || title || 'Keine Detailbeschreibung.',
  };
}

module.exports = {
  ABORT_BY_ORDINAL,
  FAULT_BY_ORDINAL,
  GUI_CODES,
  lookupAbort,
  lookupFault,
  lookupGui,
  resolveMessage,
};
