'use strict';

/**
 * GUI parameter metadata. Values themselves live in machine-config.json / Core RAM.
 * SafetyLimits are not listed here and cannot be edited from the HMI.
 */

const GROUPS = [
  {
    id: 'motion',
    title: 'Motion',
    fields: [
      {
        key: 'v_approach',
        label: 'vApproach',
        unit: 'mm/s',
        info: 'Geschwindigkeit der Anfahrbewegungen zur Safe-Pose und zum Prüfpunkt.',
      },
      {
        key: 'v_slow',
        label: 'vSlow',
        unit: 'mm/s',
        info: 'Geschwindigkeit der langsamen Anfahr- und Messbewegungen. Eine niedrigere Geschwindigkeit erhöht die zeitliche Auflösung der Kraftmessung und reduziert das Risiko eines Überschwingens.',
      },
      {
        key: 'v_measure',
        label: 'vMeasure',
        unit: 'mm/s',
        info: 'Geschwindigkeit beim langsamen Eingliedern (Phase 1) und beim Ausgliedern.',
      },
      {
        key: 'v_homing',
        label: 'vHoming',
        unit: 'mm/s',
        info: 'Homing-Geschwindigkeit (Core-Referenzfahrt Z+ → X+ → Y+).',
      },
      {
        key: 'acceleration',
        label: 'acceleration',
        unit: 'mm/s²',
        info: 'Achsbeschleunigung. 0 = konstante Geschwindigkeit (bestehendes Core-Verhalten).',
      },
    ],
  },
  {
    id: 'force',
    title: 'Force',
    fields: [
      {
        key: 'force_filter',
        label: 'forceFilter',
        unit: '—',
        info: '0 = ungefiltert. Werte (0,1] sind für eine spätere Filterstufe vorgesehen.',
      },
      {
        key: 'xy_tolerance_N',
        label: 'xyTolerance',
        unit: 'N',
        info: 'Totzone für |Fx| und |Fy| in PreClampXyCorrect.',
      },
      {
        key: 'xy_correction_step',
        label: 'xyCorrectionStep',
        unit: 'mm',
        info: 'Schrittweite der XY-Korrektur vor dem Einspannen.',
      },
      {
        key: 'xy_max_correction',
        label: 'xyMaxCorrection',
        unit: 'mm',
        info: 'Maximale XY-Abweichung vom angefahrenen Prüfpunkt.',
      },
      {
        key: 'insertion_buffer',
        label: 'insertionBuffer',
        unit: 'mm',
        info: 'Abstand in +Z vom InsertionPoint zum ExtractionPoint.',
      },
    ],
  },
  {
    id: 'collision',
    title: 'Collision',
    fields: [
      { key: 'collision_approach_N', label: 'Approach', unit: 'N', info: 'Phasengrenze Approach (unterhalb der festen SafetyLimits).' },
      { key: 'collision_retract_N', label: 'Retract', unit: 'N', info: 'Phasengrenze Retract.' },
      { key: 'collision_clamp_N', label: 'Clamp', unit: 'N', info: 'Phasengrenze Clamp.' },
      { key: 'collision_unclamp_N', label: 'Unclamp', unit: 'N', info: 'Phasengrenze Unclamp.' },
      { key: 'collision_contact_N', label: 'Contact', unit: 'N', info: 'Phasengrenze Contact / Eingliedern.' },
      { key: 'collision_measure_N', label: 'Measure', unit: 'N', info: 'Phasengrenze Measure / Prüfung.' },
    ],
  },
  {
    id: 'spreader',
    title: 'Spreizer',
    fields: [
      {
        key: 'spreader_extend_time_s',
        label: 'spreaderExtendTime',
        unit: 's',
        info: 'Wartezeit nach Ventil „ausfahren“, bis spreaderExtended gesetzt wird (keine Endlagerrückmeldung).',
      },
      {
        key: 'spreader_retract_time_s',
        label: 'spreaderRetractTime',
        unit: 's',
        info: 'Wartezeit nach Ventil „einfahren“.',
      },
    ],
  },
  {
    id: 'phase2',
    title: 'Phase 2',
    fields: [
      { key: 'force_tolerance_N', label: 'forceTolerance', unit: 'N', info: 'Totzone |Fz − Sollkraft| für die iterative Einfügepunkt-Korrektur.' },
      { key: 'z_correction_gain', label: 'zCorrectionGain', unit: 'mm/N', info: 'ΔZ = K · ΔF zwischen den Versuchen. Keine kontinuierliche PID-Regelung.' },
      { key: 'z_correction_max', label: 'zCorrectionMax', unit: 'mm', info: 'Betragsgrenze der Z-Korrektur pro Versuch.' },
      { key: 'minimum_correction', label: 'minimumCorrection', unit: 'mm', info: 'Kleinere Korrekturen werden ignoriert.' },
      { key: 'max_optimization_iterations', label: 'maxOptimizationIterations', unit: '—', integer: true, info: 'Abbruch, wenn die Sollkraft nach dieser Anzahl Versuche nicht in der Totzone liegt.' },
    ],
  },
  {
    id: 'probe',
    title: 'Probe Calibration',
    fields: [
      { key: 'probe_search_speed', label: 'searchSpeed', unit: 'mm/s', info: 'Platzhalter für das spätere historische Einmessprotokoll.' },
      { key: 'probe_search_distance', label: 'searchDistance', unit: 'mm', info: 'Suchweg des aktuellen ProbeCalibration-Platzhalters.' },
      { key: 'probe_backoff', label: 'backoff', unit: 'mm', info: 'Vorgesehen für das echte Einmessprotokoll (aktuell ungenutzt).' },
    ],
  },
  {
    id: 'calibration',
    title: 'Calibration offsets',
    fields: [
      { key: 'home_offset_x', label: 'homeOffset.x', unit: 'mm', info: 'Pose nach erfolgreichem Homing.' },
      { key: 'home_offset_y', label: 'homeOffset.y', unit: 'mm', info: 'Pose nach erfolgreichem Homing.' },
      { key: 'home_offset_z', label: 'homeOffset.z', unit: 'mm', info: 'Pose nach erfolgreichem Homing.' },
      { key: 'tool_offset_x', label: 'toolOffset.x', unit: 'mm', info: 'Fester Offset Messtasterspitze → Spreizer-TCP.' },
      { key: 'tool_offset_y', label: 'toolOffset.y', unit: 'mm', info: 'Fester Offset Messtasterspitze → Spreizer-TCP.' },
      { key: 'tool_offset_z', label: 'toolOffset.z', unit: 'mm', info: 'Fester Offset Messtasterspitze → Spreizer-TCP.' },
    ],
  },
];

function defaultMachineConfigPayload() {
  return {
    v_approach: 10,
    v_slow: 2,
    v_measure: 1,
    v_homing: 5,
    acceleration: 0,
    force_filter: 0,
    xy_tolerance_N: 0.5,
    xy_correction_step: 0.05,
    xy_max_correction: 2,
    insertion_buffer: 1,
    collision_approach_N: 20,
    collision_retract_N: 20,
    collision_clamp_N: 20,
    collision_unclamp_N: 20,
    collision_contact_N: 40,
    collision_measure_N: 40,
    spreader_extend_time_s: 0.5,
    spreader_retract_time_s: 0.5,
    force_tolerance_N: 0.5,
    z_correction_gain: 0.1,
    z_correction_max: 1,
    minimum_correction: 0.01,
    max_optimization_iterations: 10,
    probe_search_speed: 2,
    probe_search_distance: 10,
    probe_backoff: 1,
    home_offset_x: 0,
    home_offset_y: 0,
    home_offset_z: 0,
    tool_offset_x: 0,
    tool_offset_y: 0,
    tool_offset_z: 0,
  };
}

module.exports = {
  GROUPS,
  defaultMachineConfigPayload,
};
