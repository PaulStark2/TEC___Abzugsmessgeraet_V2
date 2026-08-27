'use strict';

/**
 * GUI parameter metadata. Values themselves live in machine-config.json / Core RAM.
 * SafetyLimits are not listed here and cannot be edited from the HMI.
 * min/max mirror core/SafetyLimits.hpp and MachineConfig validation (reject, no clamp).
 */

const CORNER_INFO =
  'Eckenradius für Achsblendung innerhalb eines Multi-Axis-moveTo. 0 = voller Stopp zwischen Achsen. ' +
  'Bei Corner > 0 muss R ≥ v²/(2a) für die höchste beteiligte Geschwindigkeit gelten. ' +
  'Sim-Pfadvorschau: „Vorschau ohne Kurvenradius“.';

const GROUPS = [
  {
    id: 'verfahren',
    title: 'Verfahren/Prüfpunkte',
    fields: [
      {
        key: 'v_approach_testpoints',
        label: 'vApproach (test points)',
        unit: 'mm/s',
        minExclusive: 0,
        max: 50,
        info: 'Geschwindigkeit der Anfahrbewegungen zur Safe-Z-Höhe und zum Prüfpunkt (Core MotionPlanner).',
      },
      {
        key: 'v_fine_testpoints',
        label: 'vFine (test points)',
        unit: 'mm/s',
        minExclusive: 0,
        max: 50,
        info: 'Feine Z-Anfahrt und PreClamp-XY. Langsamere Prüfpunkt-Annäherung.',
      },
      {
        key: 'v_unclamp',
        label: 'vUnclamp',
        unit: 'mm/s',
        minExclusive: 0,
        max: 50,
        info: 'Geschwindigkeit beim Ausspannen (Unclamp). Die Prüfphasen nutzen die separaten Phase-1/2/3-Geschwindigkeiten.',
      },
      {
        key: 'a_testpoints',
        label: 'a (test points)',
        unit: 'mm/s²',
        min: 0,
        max: 500,
        info: 'Achsbeschleunigung für Prüfpunkt-/Safe-Z-Verfahren. 0 = konstante Geschwindigkeit.',
      },
      {
        key: 'corner_testpoints',
        label: 'corner (test points)',
        unit: 'mm',
        min: 0,
        max: 50,
        cornerWith: {
          accelKey: 'a_testpoints',
          speedKeys: ['v_approach_testpoints', 'v_fine_testpoints', 'v_unclamp'],
        },
        info: CORNER_INFO,
      },
      {
        key: 'safe_z_buffer',
        label: 'safeZBuffer',
        unit: 'mm',
        minExclusive: 0,
        max: 500,
        info: 'Abstand oberhalb von max(höchster Prüfpunkt, Messtaster) beim Verfahren. Safe-Z (Bauplatte) = max(Punkt.z, Messtaster.z) + Puffer.',
      },
      {
        key: 'jog_min_step_mm',
        label: 'jogMinStep',
        unit: 'mm',
        minExclusive: 0,
        max: 50,
        info:
          'Kleinste erlaubte Hand-Jog-Schrittweite (Hardware-/Treiber-Auflösung). ' +
          'Im Handmodus und Kalibrier-Wizard sind nur Vielfache dieses Werts einstellbar. ' +
          'Nur HMI/Host (machine-config.json) — nicht Teil des M12 MachineConfig-Wire.',
      },
    ],
  },
  {
    id: 'phase1',
    title: 'Phase 1',
    fields: [
      {
        key: 'phase1_v_extract',
        label: 'vExtract',
        unit: 'mm/s',
        minExclusive: 0,
        max: 50,
        info: 'Geschwindigkeit beim Ausgliedern (+Z) in Phase 1 (Referenzbestimmung).',
      },
      {
        key: 'phase1_v_insert',
        label: 'vInsert',
        unit: 'mm/s',
        minExclusive: 0,
        max: 50,
        info: 'Geschwindigkeit beim Eingliedern (−Z) in Phase 1.',
      },
      {
        key: 'phase1_a',
        label: 'acceleration',
        unit: 'mm/s²',
        min: 0,
        max: 500,
        info: 'Beschleunigung für Phase-1-Bahnen. 0 = konstante Geschwindigkeit.',
      },
      {
        key: 'phase1_corner',
        label: 'corner',
        unit: 'mm',
        min: 0,
        max: 50,
        cornerWith: {
          accelKey: 'phase1_a',
          speedKeys: ['phase1_v_extract', 'phase1_v_insert'],
        },
        info: CORNER_INFO,
      },
    ],
  },
  {
    id: 'phase2',
    title: 'Phase 2',
    fields: [
      {
        key: 'phase2_v_extract',
        label: 'vExtract',
        unit: 'mm/s',
        minExclusive: 0,
        max: 50,
        info: 'Geschwindigkeit beim Ausgliedern (+Z) in Phase 2 (Optimierung).',
      },
      {
        key: 'phase2_v_insert',
        label: 'vInsert',
        unit: 'mm/s',
        minExclusive: 0,
        max: 50,
        info: 'Geschwindigkeit beim Eingliedern (−Z) in Phase 2. Typisch höher als Phase 1.',
      },
      {
        key: 'phase2_a',
        label: 'acceleration',
        unit: 'mm/s²',
        min: 0,
        max: 500,
        info: 'Beschleunigung für Phase-2-Bahnen. 0 = konstante Geschwindigkeit.',
      },
      {
        key: 'phase2_corner',
        label: 'corner',
        unit: 'mm',
        min: 0,
        max: 50,
        cornerWith: {
          accelKey: 'phase2_a',
          speedKeys: ['phase2_v_extract', 'phase2_v_insert'],
        },
        info: CORNER_INFO,
      },
      {
        key: 'force_tolerance_N',
        label: 'forceTolerance',
        unit: 'N',
        minExclusive: 0,
        max: 50,
        info: 'Totzone |Fz − Sollkraft| für die iterative Einfügepunkt-Korrektur.',
      },
      {
        key: 'z_correction_gain',
        label: 'zCorrectionGain',
        unit: 'mm/N',
        minExclusive: 0,
        info: 'ΔZ = K · (Fz − Sollkraft). Zu große Kraft → +Z weg vom Prüfkörper. Keine PID-Regelung.',
      },
      {
        key: 'z_correction_max',
        label: 'zCorrectionMax',
        unit: 'mm',
        minExclusive: 0,
        max: 20,
        info: 'Betragsgrenze der Z-Korrektur pro Versuch.',
      },
      {
        key: 'minimum_correction',
        label: 'minimumCorrection',
        unit: 'mm',
        minExclusive: 0,
        info: 'Kleinere Korrekturen werden ignoriert.',
      },
      {
        key: 'max_optimization_iterations',
        label: 'maxOptimizationIterations',
        unit: '—',
        integer: true,
        min: 1,
        max: 10000,
        info: 'Abbruch, wenn die Sollkraft nach dieser Anzahl Versuche nicht in der Totzone liegt.',
      },
    ],
  },
  {
    id: 'phase3',
    title: 'Phase 3',
    fields: [
      {
        key: 'phase3_v_extract',
        label: 'vExtract',
        unit: 'mm/s',
        minExclusive: 0,
        max: 50,
        info: 'Geschwindigkeit beim Ausgliedern (+Z) in den Prüf-/Alterungszyklen.',
      },
      {
        key: 'phase3_v_insert',
        label: 'vInsert',
        unit: 'mm/s',
        minExclusive: 0,
        max: 50,
        info: 'Geschwindigkeit beim Eingliedern (−Z) in den Prüf-/Alterungszyklen.',
      },
      {
        key: 'phase3_a',
        label: 'acceleration',
        unit: 'mm/s²',
        min: 0,
        max: 500,
        info: 'Beschleunigung für Phase-3-Bahnen. 0 = konstante Geschwindigkeit.',
      },
      {
        key: 'phase3_corner',
        label: 'corner',
        unit: 'mm',
        min: 0,
        max: 50,
        cornerWith: {
          accelKey: 'phase3_a',
          speedKeys: ['phase3_v_extract', 'phase3_v_insert'],
        },
        info: CORNER_INFO,
      },
    ],
  },
  {
    id: 'homing',
    title: 'Homing',
    fields: [
      {
        key: 'v_homing',
        label: 'vHoming',
        unit: 'mm/s',
        minExclusive: 0,
        max: 50,
        info: 'Homing-Geschwindigkeit der Core-Referenzfahrt Z+ → X+ → Y+.',
      },
      {
        key: 'a_homing',
        label: 'aHoming',
        unit: 'mm/s²',
        min: 0,
        max: 500,
        info: 'Beschleunigung für Homing-Seek/Retract. Endanschlag-Stopp ist hart (kein Corner-Blend).',
      },
      {
        key: 'home_offset_x',
        label: 'homeOffset.x',
        unit: 'mm',
        info: 'Abstand vom X+-Endanschlag nach Homing. 0 → Default 2 mm. Die resultierende Pose wird als Home gemerkt (kein Snap auf Ursprung).',
      },
      {
        key: 'home_offset_y',
        label: 'homeOffset.y',
        unit: 'mm',
        info: 'Abstand vom Y+-Endanschlag nach Homing. 0 → Default 2 mm.',
      },
      {
        key: 'home_offset_z',
        label: 'homeOffset.z',
        unit: 'mm',
        info: 'Abstand vom Z+-Endanschlag nach Homing. 0 → Default 2 mm. Safe-Z bleibt ein eigener Parameter.',
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
        min: 0,
        max: 1,
        future: true,
        info: 'Zukünftiger Parameter. 0 = ungefiltert. Werte (0,1] sind für eine spätere Filterstufe vorgesehen und wirken aktuell nicht auf den Core-Tick.',
      },
      {
        key: 'xy_tolerance_N',
        label: 'xyTolerance',
        unit: 'N',
        minExclusive: 0,
        max: 50,
        info: 'Totzone für |Fx| und |Fy| ausschließlich in PreClampXyCorrect.',
      },
      {
        key: 'xy_correction_step',
        label: 'xyCorrectionStep',
        unit: 'mm',
        minExclusive: 0,
        info: 'Schrittweite der XY-Korrektur vor dem Einspannen.',
      },
      {
        key: 'xy_max_correction',
        label: 'xyMaxCorrection',
        unit: 'mm',
        minExclusive: 0,
        max: 50,
        info: 'Maximale XY-Abweichung vom angefahrenen Prüfpunkt während PreClamp.',
      },
      {
        key: 'insertion_buffer',
        label: 'insertionBuffer',
        unit: 'mm',
        min: 0,
        max: 50,
        info: 'Abstand in +Z (Ausgliedern) vom InsertionPoint zum ExtractionPoint nach erfolgreicher Phase 1.',
      },
      {
        key: 'insert_search_distance',
        label: 'insertSearchDistance',
        unit: 'mm',
        minExclusive: 0,
        max: 100,
        info: 'Phase-1-Z-Tauchtiefe und untere Begrenzung der Einfügepunkt-Korrektur. Unabhängig von der XY-Kalibrier-Startweite (probe_search_distance).',
      },
      {
        key: 'sample_timeout_s',
        label: 'forceSampleTimeout',
        unit: 's',
        min: 0.05,
        max: 5,
        info: 'Maximalalter eines Kraftsamples. Stehende oder ausbleibende Host-Werte lösen einen Safety-Fault aus.',
      },
      {
        key: 'xy_correct_timeout_s',
        label: 'preClampTimeout',
        unit: 's',
        min: 0.05,
        max: 30,
        info: 'Timeout der PreClamp-XY-Korrektur. Danach Abort/Fault statt endloser Korrektur.',
      },
    ],
  },
  {
    id: 'communication',
    title: 'Host communication',
    fields: [
      {
        key: 'heartbeat_timeout_s',
        label: 'heartbeatTimeout',
        unit: 's',
        min: 0.05,
        max: 2,
        info: 'Watchdog-Timeout seit dem letzten Host-Heartbeat während eines automatischen Prozesses. Der Pi ist nicht Teil des Echtzeit-Sicherheitskreises; Timeout → STOP/Safe State. GUI-Wert, später Safety-seitig getrennt dokumentieren.',
      },
    ],
  },
  {
    id: 'collision',
    title: 'Collision',
    fields: [
      { key: 'collision_approach_N', label: 'collisionApproach', unit: 'N', minExclusive: 0, max: 50, info: 'Phasengrenze Approach (unterhalb der festen SafetyLimits ±50 N).' },
      { key: 'collision_retract_N', label: 'collisionRetract', unit: 'N', minExclusive: 0, max: 50, info: 'Phasengrenze Retract.' },
      { key: 'collision_clamp_N', label: 'collisionClamp', unit: 'N', minExclusive: 0, max: 50, info: 'Phasengrenze Clamp.' },
      { key: 'collision_unclamp_N', label: 'collisionUnclamp', unit: 'N', minExclusive: 0, max: 50, info: 'Phasengrenze Unclamp.' },
      { key: 'collision_contact_N', label: 'collisionContact', unit: 'N', minExclusive: 0, max: 50, info: 'Phasengrenze Contact / Eingliedern (Phase 1).' },
      { key: 'collision_measure_N', label: 'collisionMeasure', unit: 'N', minExclusive: 0, max: 50, info: 'Phasengrenze Measure / Prüfung (Phase 3).' },
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
        min: 0.01,
        max: 10,
        info: 'Ventil „ausfahren“ für diese Zeit. Keine Endlagerrückmeldung; spreaderExtended ist ein Timer-Latch, kein mechanischer Beweis.',
      },
      {
        key: 'spreader_retract_time_s',
        label: 'spreaderRetractTime',
        unit: 's',
        min: 0.01,
        max: 10,
        info: 'Ventil „einfahren“ für diese Zeit. STOP/Fault schaltet das Ventil stromlos (SafeDeenergized).',
      },
    ],
  },
  {
    id: 'probe',
    title: 'Einmessung M19',
    fields: [
      {
        key: 'v_approach_probe',
        label: 'vApproach (probe)',
        unit: 'mm/s',
        minExclusive: 0,
        max: 50,
        info: 'Anfahrt / Ausrichten / Absenken zum Suchstart der M19-Einmessung.',
      },
      {
        key: 'v_transfer_probe',
        label: 'vTransfer (probe)',
        unit: 'mm/s',
        minExclusive: 0,
        max: 50,
        info: 'Transfergeschwindigkeit: Backoff, Retreat, Hub-Transfers während M19.',
      },
      {
        key: 'v_search',
        label: 'vSearch (coarse)',
        unit: 'mm/s',
        minExclusive: 0,
        max: 50,
        info: 'Schnelle Suchgeschwindigkeit bis zum Messtaster-Trigger.',
      },
      {
        key: 'v_search_fine',
        label: 'vSearch (fine)',
        unit: 'mm/s',
        minExclusive: 0,
        max: 50,
        info: 'Langsame Suchgeschwindigkeit der Feinmessungen.',
      },
      {
        key: 'a_probe_transfer',
        label: 'aTransfer (probe)',
        unit: 'mm/s²',
        min: 0,
        max: 500,
        info: 'Beschleunigung für Probe-Transferbahnen. 0 = konstante Geschwindigkeit.',
      },
      {
        key: 'corner_probe_transfer',
        label: 'cornerTransfer (probe)',
        unit: 'mm',
        min: 0,
        max: 50,
        cornerWith: {
          accelKey: 'a_probe_transfer',
          speedKeys: ['v_approach_probe', 'v_transfer_probe'],
        },
        info: CORNER_INFO,
      },
      {
        key: 'a_probe_search',
        label: 'aSearch (probe)',
        unit: 'mm/s²',
        min: 0,
        max: 500,
        info: 'Trapezoid-Beschleunigung während der Suche (ohne Corner-Blend).',
      },
      {
        key: 'a_probe_brake',
        label: 'aBrake (probe)',
        unit: 'mm/s²',
        min: 0,
        max: 500,
        info: 'Notbrems-Beschleunigung bei Messtaster-Trigger.',
      },
      {
        key: 'probe_search_distance',
        label: 'searchDistance',
        unit: 'mm',
        minExclusive: 0,
        max: 100,
        info: 'Zusätzlicher seitlicher Freiraum der XY-Kalibriersuche (ABB-Startoffset). Nicht die Phase-1-Tauchtiefe.',
      },
      {
        key: 'probe_backoff',
        label: 'backoff',
        unit: 'mm',
        min: 0,
        max: 100,
        info: 'Rückzug nach einem Trigger vor der Feinmessung.',
      },
      {
        key: 'search_depth',
        label: 'searchDepth',
        unit: 'mm',
        minExclusive: 0,
        max: 50,
        info: 'XY-Arbeitshöhe = gemessenes Probe-Zentrum Z minus diesen Wert. Bewusst unterhalb der Scheibenmittelebene (Default 5 mm).',
      },
      {
        key: 'approach_distance',
        label: 'approachDistance',
        unit: 'mm',
        minExclusive: 0,
        max: 100,
        info:
          'Grob-Anfahrhöhe (Standoff) über Messtaster- bzw. Prüfpunkt-Z. Schnelle Z-Fahrt endet hier; ' +
          'danach Feinanfahrt (Prüfpunkt) bzw. M19-Einmessprotokoll (Messtaster). Default 15 mm.',
      },
      {
        key: 'num_fine_searches',
        label: 'numFineSearches',
        unit: '—',
        integer: true,
        min: 0,
        max: 10000,
        info: 'Anzahl langsamer Feinmessungen nach der Grobsuche. Default 1 (ein Feintasten). 0 = nur Grobsuche.',
      },
      {
        key: 'head_diameter',
        label: 'headDiameter',
        unit: 'mm',
        min: 0,
        max: 50,
        info: 'Reserviert (historisch ABB). Im aktuellen Einmessalgorithmus ungenutzt. Default 0.',
      },
      {
        key: 'measuring_head_diameter',
        label: 'measuringHeadDiameter',
        unit: 'mm',
        minExclusive: 0,
        max: 50,
        info: 'Durchmesser der fest eingebauten Messscheibe.',
      },
      {
        key: 'measuring_head_thickness',
        label: 'measuringHeadThickness',
        unit: 'mm',
        minExclusive: 0,
        max: 20,
        info: 'Dicke der Messscheibe. probe_center liegt in der Mittelebene.',
      },
      {
        key: 'tool_diameter',
        label: 'toolDiameter',
        unit: 'mm',
        minExclusive: 0,
        max: 50,
        info: 'Durchmesser des Spreizers. TCP ist die untere Stirnfläche. Der zylindrische Bereich hat konstanten Durchmesser.',
      },
      {
        key: 'tool_cylindrical_height',
        label: 'toolCylindricalHeight',
        unit: 'mm',
        minExclusive: 0,
        max: 100,
        info: 'Höhe des Spreizerzylinders ab TCP in +Z. XY-Kontakt darf über diesen Schaft erfolgen (Default 15 mm).',
      },
      {
        key: 'probe_xy_search_margin',
        label: 'probeXySearchMargin',
        unit: 'mm',
        min: 0,
        max: 50,
        info: 'Zusätzlicher inward-Suchweg über den Probe-Radius hinaus. Hartes XY-Limit: innerer TCP-Abstand zur Probe-Achse = R_probe − margin (Werkzeugradius wird nicht addiert). Muss kleiner als der Probe-Radius bleiben.',
      },
      {
        key: 'probe_offset_reference_x',
        label: 'probeOffsetReference.x',
        unit: 'mm',
        min: -500,
        max: 500,
        info: 'LEGACY: frühere Offset-Referenz (Default 40,40,10). Nicht die reale Messtasterposition. Wird von M19 Install/OffsetOnly nicht mehr verwendet.',
      },
      {
        key: 'probe_offset_reference_y',
        label: 'probeOffsetReference.y',
        unit: 'mm',
        min: -500,
        max: 500,
        info: 'LEGACY: frühere Offset-Referenz (Default 40,40,10). Nicht die reale Messtasterposition. Wird von M19 Install/OffsetOnly nicht mehr verwendet.',
      },
      {
        key: 'probe_offset_reference_z',
        label: 'probeOffsetReference.z',
        unit: 'mm',
        min: -500,
        max: 500,
        info: 'LEGACY: frühere Offset-Referenz (Default 40,40,10). Nicht die reale Messtasterposition. Wird von M19 Install/OffsetOnly nicht mehr verwendet.',
      },
    ],
  },
  {
    id: 'calibration',
    title: 'Calibration offsets',
    fields: [
      {
        key: 'tool_offset_x',
        label: 'toolOffset.x',
        unit: 'mm',
        readOnly: true,
        info:
          'TCP-Korrektur X: machine = plateOrigin + platePoint + toolOffset. ' +
          'Nur durch Messtaster-Einmessung (FullCalib/Install oder OffsetOnly) setzbar — nicht manuell änderbar.',
      },
      {
        key: 'tool_offset_y',
        label: 'toolOffset.y',
        unit: 'mm',
        readOnly: true,
        info: 'TCP-Korrektur Y. Nur durch Messtaster-Einmessung setzbar — nicht manuell änderbar.',
      },
      {
        key: 'tool_offset_z',
        label: 'toolOffset.z',
        unit: 'mm',
        readOnly: true,
        info: 'TCP-Korrektur Z. Nur durch Messtaster-Einmessung setzbar — nicht manuell änderbar.',
      },
    ],
  },
];

/** Keys that must not be changed via the parameter menu (probe calibration only). */
const READ_ONLY_KEYS = GROUPS.flatMap((g) => g.fields)
  .filter((f) => f.readOnly)
  .map((f) => f.key);

function defaultMachineConfigPayload() {
  return {
    v_approach_testpoints: 10,
    v_fine_testpoints: 2,
    v_unclamp: 1,
    v_homing: 5,
    a_testpoints: 0,
    corner_testpoints: 0,
    a_homing: 0,
    jog_min_step_mm: 0.1,
    phase1_v_extract: 1,
    phase1_v_insert: 1,
    phase1_a: 0,
    phase1_corner: 0,
    phase2_v_extract: 2,
    phase2_v_insert: 3,
    phase2_a: 0,
    phase2_corner: 0,
    phase3_v_extract: 2,
    phase3_v_insert: 2,
    phase3_a: 0,
    phase3_corner: 0,
    force_filter: 0,
    xy_tolerance_N: 0.5,
    xy_correction_step: 0.05,
    xy_max_correction: 2,
    insertion_buffer: 1,
    insert_search_distance: 10,
    collision_approach_N: 20,
    collision_retract_N: 50,
    collision_clamp_N: 20,
    collision_unclamp_N: 20,
    collision_contact_N: 50,
    collision_measure_N: 50,
    spreader_extend_time_s: 0.5,
    spreader_retract_time_s: 0.5,
    force_tolerance_N: 0.5,
    z_correction_gain: 0.1,
    z_correction_max: 1,
    minimum_correction: 0.01,
    max_optimization_iterations: 10,
    v_approach_probe: 10,
    v_transfer_probe: 10,
    v_search: 2,
    v_search_fine: 1,
    a_probe_transfer: 0,
    corner_probe_transfer: 0,
    a_probe_search: 0,
    a_probe_brake: 500,
    probe_search_distance: 10,
    probe_backoff: 1,
    search_depth: 5,
    approach_distance: 15,
    num_fine_searches: 1,
    head_diameter: 0,
    measuring_head_diameter: 8,
    measuring_head_thickness: 3,
    tool_diameter: 2.5,
    tool_cylindrical_height: 15,
    probe_xy_search_margin: 2,
    probe_offset_reference_x: 40,
    probe_offset_reference_y: 40,
    probe_offset_reference_z: 10,
    home_offset_x: 0,
    home_offset_y: 0,
    home_offset_z: 0,
    tool_offset_x: 0,
    tool_offset_y: 0,
    tool_offset_z: 0,
    safe_z_buffer: 30,
    sample_timeout_s: 0.5,
    heartbeat_timeout_s: 0.3,
    xy_correct_timeout_s: 2,
  };
}

/**
 * Client/server hint when corner > 0 but a≤0 or R < v²/(2a).
 * Reads cornerWith metadata from GROUPS (or a supplied groups array).
 */
function evaluateCornerConsistencyHints(cfg, groups = GROUPS) {
  const hints = [];
  if (!cfg || typeof cfg !== 'object') {
    return hints;
  }
  for (const group of groups) {
    for (const field of group.fields || []) {
      if (!field.cornerWith) {
        continue;
      }
      const corner = Number(cfg[field.key]);
      if (!(corner > 0)) {
        continue;
      }
      const a = Number(cfg[field.cornerWith.accelKey]);
      const speeds = (field.cornerWith.speedKeys || [])
        .map((key) => Number(cfg[key]))
        .filter((v) => Number.isFinite(v) && v > 0);
      const vmax = speeds.length ? Math.max(...speeds) : 0;
      if (!(a > 0)) {
        const minA = corner > 0 && vmax > 0 ? (vmax * vmax) / (2 * corner) : null;
        hints.push({
          key: field.key,
          label: field.label,
          message:
            minA !== null
              ? `${field.label}: Bei Corner > 0 muss a > 0 sein. Empfohlen: a ≥ ${minA.toFixed(3)} mm/s² oder R erhöhen / v senken.`
              : `${field.label}: Bei Corner > 0 muss a > 0 sein (sonst kein Blend).`,
        });
        continue;
      }
      if (!(vmax > 0)) {
        continue;
      }
      const minR = (vmax * vmax) / (2 * a);
      if (corner + 1e-9 < minR) {
        const minA = (vmax * vmax) / (2 * corner);
        const maxV = Math.sqrt(2 * a * corner);
        hints.push({
          key: field.key,
          label: field.label,
          message:
            `${field.label}: R=${corner} < v²/(2a)=${minR.toFixed(3)} mm. ` +
            `Empfehlung: R ≥ ${minR.toFixed(3)} mm, oder a ≥ ${minA.toFixed(3)} mm/s², oder v ≤ ${maxV.toFixed(3)} mm/s.`,
        });
      }
    }
  }
  return hints;
}

function validateMachineConfigPayload(cfg) {
  if (!cfg || typeof cfg !== 'object') {
    return { ok: false, error: 'MachineConfig fehlt' };
  }
  const defaults = defaultMachineConfigPayload();
  for (const key of Object.keys(defaults)) {
    const value = cfg[key];
    if (typeof value !== 'number' || !Number.isFinite(value)) {
      return { ok: false, error: `${key} muss eine endliche Zahl sein` };
    }
  }
  for (const group of GROUPS) {
    for (const field of group.fields) {
      const value = cfg[field.key];
      if (field.integer && !Number.isInteger(value)) {
        return { ok: false, error: `${field.key} muss ganzzahlig sein` };
      }
      if (field.minExclusive !== undefined && !(value > field.minExclusive)) {
        return { ok: false, error: `${field.key} muss > ${field.minExclusive} sein` };
      }
      if (field.min !== undefined && value < field.min) {
        return { ok: false, error: `${field.key} muss ≥ ${field.min} sein` };
      }
      if (field.max !== undefined && value > field.max) {
        return { ok: false, error: `${field.key} muss ≤ ${field.max} sein (SafetyLimits)` };
      }
    }
  }
  return { ok: true };
}

module.exports = {
  GROUPS,
  READ_ONLY_KEYS,
  defaultMachineConfigPayload,
  validateMachineConfigPayload,
  evaluateCornerConsistencyHints,
};
