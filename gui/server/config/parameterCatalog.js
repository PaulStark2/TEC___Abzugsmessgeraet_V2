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
        info: 'Feine Z-Anfahrt auf den Prüfpunkt. Die Wand-zu-Wand-Fahrt nutzt vBoreSearch im Reiter Regelung.',
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
        key: 'a_manual',
        label: 'a (Handfahrt)',
        unit: 'mm/s²',
        minExclusive: 0,
        max: 500,
        info: 'Beschleunigung nur für Handfahrt, Jog und Kalibrieren. Automatik und Homing behalten ihre Profile. Senken, wenn der Schrittmotor unter Last Schritte verliert.',
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
        info: 'Abstand oberhalb von max(höchster Prüfpunkt, Einmessbohrung) beim Verfahren. Safe-Z (Bauplatte) = max(Punkt.z, probeCenter.z) + Puffer.',
      },
      {
        key: 'safe_z_calib_buffer',
        label: 'safeZCalibBuffer',
        unit: 'mm',
        minExclusive: 0,
        max: 500,
        info:
          'Freifahrt-Puffer für Teach-Anfahrt (Kalibrierpunkte KM/K1–K3 und Einmessbohrung). ' +
          'Safe-Z-Calib liegt um diesen Betrag oberhalb des höchsten gespeicherten Kalibrier-/Probe-Z.',
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
      {
        key: 'fault_manual_max_step_mm',
        label: 'Fault-Hand Max. Schritt',
        unit: 'mm',
        minExclusive: 0,
        max: 10,
        info:
          'Maximale Jog-Schrittweite im „Handmodus trotz Fault“. Nur HMI/Host — nicht auf dem M12-Wire.',
      },
    ],
  },
  {
    id: 'fault_manual',
    title: 'Fault-Handmodus',
    fields: [
      {
        key: 'fault_manual_v_max',
        label: 'Max. Geschwindigkeit',
        unit: 'mm/s',
        minExclusive: 0,
        max: 50,
        info: 'Obergrenze für Jog-Geschwindigkeit während Handmodus trotz Fault.',
      },
      {
        key: 'fault_manual_a',
        label: 'Beschleunigung',
        unit: 'mm/s²',
        minExclusive: 0,
        max: 500,
        info: 'Beschleunigung für Recovery-Jog. Muss > 0 sein.',
      },
      {
        key: 'fault_manual_hard_limit_x_N',
        label: 'Recovery Hard-Limit X',
        unit: 'N',
        minExclusive: 0,
        max: 500,
        info: 'Erhöhte |Fx|-Grenze im Fault-Handmodus. Darüber → wieder voller Fault-Freeze. Muss ≥ normales Hard-Limit X sein.',
      },
      {
        key: 'fault_manual_hard_limit_y_N',
        label: 'Recovery Hard-Limit Y',
        unit: 'N',
        minExclusive: 0,
        max: 500,
        info: 'Erhöhte |Fy|-Grenze im Fault-Handmodus. Darüber → wieder voller Fault-Freeze. Muss ≥ normales Hard-Limit Y sein.',
      },
      {
        key: 'fault_manual_hard_limit_N',
        label: 'Recovery Hard-Limit Z',
        unit: 'N',
        minExclusive: 0,
        max: 500,
        info: 'Erhöhte |Fz|-Grenze im Fault-Handmodus. Darüber → wieder voller Fault-Freeze. Muss ≥ normales Hard-Limit Z sein.',
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
      {
        key: 'insertion_buffer',
        label: 'insertionBuffer',
        unit: 'mm',
        min: 0,
        max: 50,
        info:
          'Feiner +Z-Hub nach dem Wieder-Einsetzen (AfterTest): ' +
          'Ziel = min(InsertionPoint + insertionBuffer, Programm-Z + approachDistance), danach Safe-Z. ' +
          '0 = kein Extra-Hub (direkt zur Safe-Z, sofern schon am InsertionPoint).',
      },
      {
        key: 'after_seat_retract_mm',
        label: 'afterSeatRetract',
        unit: 'mm',
        min: 0,
        max: 50,
        info:
          'Fester +Z-Rückhub nach SeatToForce von der Seat-Pose aus, bevor der zweite PreClamp startet. ' +
          'Ziel = insertion_z + afterSeatRetract (nicht Programm-Z + Standoff). ' +
          '0 = kein Rückhub; Tarieren und zweiter PreClamp bleiben auf der Seat-Pose.',
      },
    ],
  },
  {
    id: 'phase3',
    title: 'Phase 3',
    fields: [
      {
        key: 'extract_after_free_mm',
        label: 'extractAfterFree',
        unit: 'mm',
        min: 0,
        max: 50,
        info:
          'Nachlauf nach dem echten Ausgliederpunkt (Kraftfrei nach Zugkraft): noch so viele mm +Z ' +
          'weiterfahren und weitermessen. Der Ausgliederpunkt ist nicht der erste 0-N-Durchgang ' +
          '(Druck→Zug), sondern erst das erneute Absinken der Zugkraft auf 0 N. ' +
          '0 = sofort am Kraftfrei-Punkt stoppen.',
      },
      {
        key: 'extract_after_free_v',
        label: 'vExtractAfterFree',
        unit: 'mm/s',
        minExclusive: 0,
        max: 50,
        info:
          'Geschwindigkeit für den +Z-Nachlauf nach Kraftfrei und für die Rückfahrt ' +
          'über dieselbe Distanz zurück zum Ausgliederpunkt. ' +
          'Bis Kraftfrei gilt vExtract, unterhalb des Ausgliederpunkts vInsert. Default 1 mm/s.',
      },
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
        info: 'Geschwindigkeit beim Eingliedern (−Z) in den Prüf-/Alterungszyklen (bis zur Feinphase).',
      },
      {
        key: 'phase3_v_insert_fine',
        label: 'vInsertFine',
        unit: 'mm/s',
        minExclusive: 0,
        max: 50,
        info:
          'Reduzierte Einglieder-Geschwindigkeit nahe der Sollkraft. ' +
          'Aktiv ab forceNearTargetFrac × Prüfkraft. Default 0,5 mm/s.',
      },
      {
        key: 'force_near_target_frac',
        label: 'forceNearTargetFrac',
        unit: '—',
        minExclusive: 0,
        max: 1,
        info:
          'Anteil der Prüfkraft, ab dem die Feinphase startet (langsamere Fahrt + feineres Kraft-Weg-Raster). ' +
          'Beispiel 0,8 = ab 80 % der Sollkraft. Default 0,8.',
      },
      {
        key: 'force_path_dz_mm',
        label: 'forcePathDz',
        unit: 'mm',
        min: 0,
        max: 5,
        info:
          'Mindest-Wegabstand zwischen zwei gespeicherten Kraft-Weg-Messpunkten. ' +
          '0 = jedes Regelungstick. Kleiner = dichtere Kurve, mehr UART-/Pufferlast. Default 0,01 mm.',
      },
      {
        key: 'force_path_dz_fine_mm',
        label: 'forcePathDzFine',
        unit: 'mm',
        min: 0,
        max: 5,
        info:
          'Feineres Kraft-Weg-Raster in der Insert-Feinphase (nahe Sollkraft). ' +
          'Default 0,005 mm. Sollte ≤ forcePathDz sein.',
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
          speedKeys: [
            'phase3_v_extract',
            'phase3_v_insert',
            'phase3_v_insert_fine',
            'extract_after_free_v',
          ],
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
        info: 'Langsame Endschaltersuche und Retract beim Homing (Z → X → Y).',
      },
      {
        key: 'v_homing_fast',
        label: 'vHomingFast',
        unit: 'mm/s',
        minExclusive: 0,
        max: 50,
        info: 'Schnellanfahrt zur Homingpose minus Anfahrabstand. Reference-Home nach erstem erfolgreichen Homing dieser Session (auch nach Soft-STOP/Jog); DefineMcs, Cold-Start und Safety-Fault bleiben bei vHoming.',
      },
      {
        key: 'a_homing_fast',
        label: 'aHomingFast',
        unit: 'mm/s²',
        min: 0,
        max: 500,
        info:
          'Beschleunigung nur für die Schnellanfahrt (vHomingFast). ' +
          '0 = konstante Geschwindigkeit. Seek/Retract/Kriechen nutzen weiter aHoming. Default 500 mm/s².',
      },
      {
        key: 'homing_approach_distance',
        label: 'Anfahrabstand Homing',
        unit: 'mm',
        minExclusive: 0,
        max: 100,
        info: 'Abstand vor der abgeleiteten Homingpose, ab dem von Schnellanfahrt auf langsame Endschaltersuche gewechselt wird.',
      },
      {
        key: 'a_homing',
        label: 'aHoming',
        unit: 'mm/s²',
        min: 0,
        max: 500,
        info: 'Beschleunigung für langsame Homing-Seek/Retract/Kriechen. Endanschlag-Stopp ist hart (kein Corner-Blend).',
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
        key: 'force_range_N',
        label: 'Messbereich',
        unit: 'N',
        minExclusive: 0,
        max: 500,
        info: 'Betrag des Messbereichs für alle Achsen. Default 30 N. Kalibrierpunkte und die Eingliederkraft dürfen ihn nicht überschreiten. Darüber liegt nur die Notbremse von 500 N gegen Tippfehler.',
      },
      {
        key: 'force_hard_limit_x_N',
        label: 'Harte Sicherheitsgrenze X',
        unit: 'N',
        minExclusive: 0,
        max: 500,
        info: 'Fault, wenn |Fx| darüber liegt. Gilt immer, auch bei ausgeschalteter Phasenprüfung. Default 30 N. Muss kleiner oder gleich dem Messbereich sein.',
      },
      {
        key: 'force_hard_limit_y_N',
        label: 'Harte Sicherheitsgrenze Y',
        unit: 'N',
        minExclusive: 0,
        max: 500,
        info: 'Fault, wenn |Fy| darüber liegt. Gilt immer, auch bei ausgeschalteter Phasenprüfung. Default 30 N. Muss kleiner oder gleich dem Messbereich sein.',
      },
      {
        key: 'force_hard_limit_N',
        label: 'Harte Sicherheitsgrenze Z',
        unit: 'N',
        minExclusive: 0,
        max: 500,
        info: 'Fault, wenn |Fz| darüber liegt. Gilt immer, auch bei ausgeschalteter Phasenprüfung. Default 30 N. Muss kleiner oder gleich dem Messbereich sein.',
      },
      {
        key: 'force_calib_points',
        label: 'Kalibrierpunkte je Richtung',
        unit: '—',
        integer: true,
        min: 1,
        max: 16,
        info: 'Anzahl der Zug- und der Druckstufen im nächsten Kalibrierlauf. Default 5. Steht nur in der Parameterdatei, nicht im MachineConfig-Frame.',
      },
      {
        key: 'force_calib_step_g',
        label: 'Kalibrierschritt',
        unit: 'g',
        minExclusive: 200,
        info: 'Schrittweite der Sollmassen. Default 500 g. Muss größer als 200 g sein, weil das Eingabefenster ±100 g bleibt. Die Stufen sind 1×Schritt bis Anzahl×Schritt.',
      },
      {
        key: 'insert_search_distance',
        label: 'insertSearchDistance',
        unit: 'mm',
        minExclusive: 0,
        max: 100,
        info: 'Phase-1-Z-Tauchtiefe und untere Begrenzung der Einfügepunkt-Korrektur. Unabhängig von der Einmess-Bohrung (probe.*).',
      },
      {
        key: 'sample_timeout_s',
        label: 'forceSampleTimeout',
        unit: 's',
        min: 0.05,
        max: 5,
        info: 'Maximalalter eines Kraftsamples. Stehende oder ausbleibende Host-Werte lösen einen Safety-Fault aus.',
      },
    ],
  },
  {
    id: 'regelung',
    title: 'Regelung',
    fields: [
      {
        key: 'v_bore_search',
        label: 'vBoreSearch',
        unit: 'mm/s',
        minExclusive: 0,
        max: 50,
        info: 'Geschwindigkeit der Wand-zu-Wand-Fahrt und der Anfahrt auf die Sehnenmittelpunkte. Unabhängig von vFine. Beschleunigung und Eckenradius bleiben a und corner aus Verfahren/Prüfpunkte. Default 1 mm/s.',
      },
      {
        key: 'xy_tolerance_N',
        label: 'xyTolerance',
        unit: 'N',
        minExclusive: 0,
        max: 50,
        info: 'Wandschwelle der Bohrungssuche in PreClamp. Ein Punkt wird erst gespeichert, wenn die gesuchte Kraft (Fahrtrichtung = Vorzeichen) die Schwelle steigend überschreitet. Am Sehnenmittelpunkt gilt dieselbe Schwelle für |Fx| und |Fy|.',
      },
      {
        key: 'xy_search_limit',
        label: 'xySearchLimit',
        unit: 'mm',
        minExclusive: 0,
        max: 50,
        info: 'Weg einer Wandfahrt ab dem programmierten Punkt. Keine Wand innerhalb dieses Wegs bricht die Suche ab. Default 5 mm.',
      },
      {
        key: 'xy_correct_timeout_s',
        label: 'preClampTimeout',
        unit: 's',
        min: 0.05,
        max: 600,
        info: 'Zeitlimit der ganzen Sehnensuche inklusive optionaler Wiederholungen. Default 30 s, höchstens 600 s. Bei kleinem vBoreSearch oder boreRefinePasses > 0 groß genug wählen. Danach Abbruch.',
      },
      {
        key: 'bore_refine_passes',
        label: 'boreRefinePasses',
        unit: '—',
        integer: true,
        min: 0,
        max: 5,
        info: 'Zusätzliche komplette Zwei-Sehnen-Durchläufe vom gefundenen Mittelpunkt. 0 = einmal suchen (Default). Höchstens 5.',
      },
      {
        key: 'preclamp_confirm',
        label: 'preclampConfirm',
        unit: '0/1',
        integer: true,
        min: 0,
        max: 1,
        info: '1: bei Restkraft am Sehnenmittelpunkt anhalten und auf Weiter warten. 0: nur protokollieren und mit Clamp weitermachen.',
      },
      {
        key: 'force_filter',
        label: 'forceFilter',
        unit: 's',
        min: 0,
        max: 1,
        info: 'Zeitkonstante tau der Kraftglättung in Sekunden. 0 lässt Fx, Fy und Fz unverändert. Sonst gilt tau·dy/dt + y = u, diskret y ← y + (1 − exp(−dt/tau))·(u − y). Default 0,05 s. Der Watchdog sieht den ungefilterten Wert.',
      },
      {
        key: 'force_tolerance_N',
        label: 'forceTolerance',
        unit: 'N',
        minExclusive: 0,
        max: 50,
        info: 'Totzone |Fz| für kraftfrei und |Fz − Sollkraft| in der Prüfung.',
      },
    ],
  },
  {
    id: 'pruefung_anomalien',
    title: 'Prüfung / Anomalien',
    fields: [
      {
        key: 'coord_tolerance_mm',
        label: 'coordTolerance',
        unit: 'mm',
        minExclusive: 0,
        max: 50,
        info: 'Koordinaten-Toleranz für Positionsvergleiche in der Prüfung.',
      },
      {
        key: 'test_lateral_force_N',
        label: 'testLateralForce',
        unit: 'N',
        minExclusive: 0,
        max: 500,
        info: 'Laterale Kraftschwelle (Fx/Fy) während der Prüfung.',
      },
      {
        key: 'z_limit_pos_mm',
        label: 'zLimitPos',
        unit: 'mm',
        minExclusive: 0,
        max: 100,
        info: 'Z-Hüllkurve in +Z relativ zum Referenzpunkt [mm].',
      },
      {
        key: 'z_limit_neg_mm',
        label: 'zLimitNeg',
        unit: 'mm',
        minExclusive: 0,
        max: 100,
        info: 'Z-Hüllkurve in −Z relativ zum Referenzpunkt [mm].',
      },
      {
        key: 'anomaly_skip',
        label: 'anomalySkip',
        unit: '0/1',
        integer: true,
        min: 0,
        max: 1,
        info: '1: Anomalien nur protokollieren und fortfahren. 0: auf Bedienerentscheidung warten.',
      },
      {
        key: 'tool_offset_change_tolerance_mm',
        label: 'toolOffsetChangeTolerance',
        unit: 'mm',
        min: 0,
        max: 50,
        info:
          'Bei OffsetOnly-Einmessen: wenn |neuer − alter toolOffset| (3D) diese Toleranz überschreitet, ' +
          'gilt das als Anomalie (vermutlich Werkzeugwechsel). Default 0,5 mm.',
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
      { key: 'collision_approach_x_N', label: 'Kollision Anfahrt X', unit: 'N', minExclusive: 0, max: 500, info: 'Fault, wenn |Fx| in Approach darüber liegt. Darf den Messbereich nicht überschreiten.' },
      { key: 'collision_approach_y_N', label: 'Kollision Anfahrt Y', unit: 'N', minExclusive: 0, max: 500, info: 'Fault, wenn |Fy| in Approach darüber liegt. Darf den Messbereich nicht überschreiten.' },
      { key: 'collision_approach_N', label: 'Kollision Anfahrt Z', unit: 'N', minExclusive: 0, max: 500, info: 'Fault, wenn |Fz| in Approach darüber liegt. Darf den Messbereich nicht überschreiten.' },
      { key: 'collision_retract_x_N', label: 'Kollision Retract X', unit: 'N', minExclusive: 0, max: 500, info: 'Fault, wenn |Fx| in Retract darüber liegt. Darf den Messbereich nicht überschreiten.' },
      { key: 'collision_retract_y_N', label: 'Kollision Retract Y', unit: 'N', minExclusive: 0, max: 500, info: 'Fault, wenn |Fy| in Retract darüber liegt. Darf den Messbereich nicht überschreiten.' },
      { key: 'collision_retract_N', label: 'Kollision Retract Z', unit: 'N', minExclusive: 0, max: 500, info: 'Fault, wenn |Fz| in Retract darüber liegt. Darf den Messbereich nicht überschreiten.' },
      { key: 'collision_clamp_x_N', label: 'Kollision Klemmen X', unit: 'N', minExclusive: 0, max: 500, info: 'Fault, wenn |Fx| in Clamp darüber liegt. Darf den Messbereich nicht überschreiten.' },
      { key: 'collision_clamp_y_N', label: 'Kollision Klemmen Y', unit: 'N', minExclusive: 0, max: 500, info: 'Fault, wenn |Fy| in Clamp darüber liegt. Darf den Messbereich nicht überschreiten.' },
      { key: 'collision_clamp_N', label: 'Kollision Klemmen Z', unit: 'N', minExclusive: 0, max: 500, info: 'Fault, wenn |Fz| in Clamp darüber liegt. Darf den Messbereich nicht überschreiten.' },
      { key: 'collision_unclamp_x_N', label: 'Kollision Unclamp X', unit: 'N', minExclusive: 0, max: 500, info: 'Fault, wenn |Fx| in Unclamp darüber liegt. Darf den Messbereich nicht überschreiten.' },
      { key: 'collision_unclamp_y_N', label: 'Kollision Unclamp Y', unit: 'N', minExclusive: 0, max: 500, info: 'Fault, wenn |Fy| in Unclamp darüber liegt. Darf den Messbereich nicht überschreiten.' },
      { key: 'collision_unclamp_N', label: 'Kollision Unclamp Z', unit: 'N', minExclusive: 0, max: 500, info: 'Fault, wenn |Fz| in Unclamp darüber liegt. Darf den Messbereich nicht überschreiten.' },
      { key: 'collision_contact_x_N', label: 'Kollision Kontakt X', unit: 'N', minExclusive: 0, max: 500, info: 'Fault, wenn |Fx| in Contact / Phase 1 darüber liegt. Darf den Messbereich nicht überschreiten.' },
      { key: 'collision_contact_y_N', label: 'Kollision Kontakt Y', unit: 'N', minExclusive: 0, max: 500, info: 'Fault, wenn |Fy| in Contact / Phase 1 darüber liegt. Darf den Messbereich nicht überschreiten.' },
      { key: 'collision_contact_N', label: 'Kollision Kontakt Z', unit: 'N', minExclusive: 0, max: 500, info: 'Fault, wenn |Fz| in Contact / Phase 1 darüber liegt. Darf den Messbereich nicht überschreiten.' },
      {
        key: 'collision_measure_x_N',
        label: 'Kollision Prüfphase X',
        unit: '%',
        minExclusive: 100,
        max: 500,
        info:
          'Prozent der Eingliederungskraft (Prüfkraft). Gilt für die gesamte Prüfphase (Opening + Insert + Extract). Effektives Limit = Prüfkraft × % / 100. Muss > 100 % sein.',
      },
      {
        key: 'collision_measure_y_N',
        label: 'Kollision Prüfphase Y',
        unit: '%',
        minExclusive: 100,
        max: 500,
        info:
          'Prozent der Eingliederungskraft (Prüfkraft). Gilt für die gesamte Prüfphase (Opening + Insert + Extract). Effektives Limit = Prüfkraft × % / 100. Muss > 100 % sein.',
      },
      {
        key: 'collision_measure_N',
        label: 'Kollision Prüfphase Z',
        unit: '%',
        minExclusive: 100,
        max: 500,
        info:
          'Prozent der Eingliederungskraft (Prüfkraft). Gilt für die gesamte Prüfphase (Opening + Insert + Extract). Effektives Limit = Prüfkraft × % / 100. Muss > 100 % sein.',
      },
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
        info: 'Ventil „ausfahren“ für diese Zeit (STM32: PC8 High / PC12 Low). Keine Endlagerrückmeldung; spreaderExtended ist ein Timer-Latch, kein mechanischer Beweis.',
      },
      {
        key: 'spreader_retract_time_s',
        label: 'spreaderRetractTime',
        unit: 's',
        min: 0.01,
        max: 10,
        info: 'Ventil „einfahren“ für diese Zeit (STM32: PC12 High / PC8 Low). STOP/Fault friert das letzte Ventilpaar ein (SafeDeenergized), schaltet also nicht zwingend stromlos.',
      },
    ],
  },
  {
    id: 'probe',
    title: 'Einmess-Bohrung',
    fields: [
      {
        key: 'probe_v_approach',
        label: 'vApproach (probe)',
        unit: 'mm/s',
        minExclusive: 0,
        max: 50,
        info:
          'XY-Anfahrt und grobe Z-Fahrt (ProbeApproach, CalibApproach / Standoff über Kalibrier- und Einmesspunkten).',
      },
      {
        key: 'v_search',
        label: 'vSearch (probe bore)',
        unit: 'mm/s',
        minExclusive: 0,
        max: 50,
        info:
          'Tastgeschwindigkeit der Einmess-Bohrung und der Bauplatten-Kalibrierpunkte (Boden und XY-Wände). Unabhängig von vBoreSearch (PreClamp).',
      },
      {
        key: 'probe_wall_force_N',
        label: 'probeWallForce',
        unit: 'N',
        minExclusive: 0,
        max: 50,
        info: 'Kraftschwelle für Wandkontakt in der Einmess-Bohrung. Unabhängig von PreClamp (xyTolerance).',
      },
      {
        key: 'probe_floor_force_N',
        label: 'probeFloorForce',
        unit: 'N',
        minExclusive: 0,
        max: 50,
        info: 'Kraftschwelle für die Bodenantastung (−Z) vor der XY-Mittelpunktsuche (z_first).',
      },
      {
        key: 'probe_xy_search_limit',
        label: 'probeXySearchLimit',
        unit: 'mm',
        minExclusive: 0,
        max: 50,
        info: 'Maximaler XY-Suchweg ab dem Startpunkt der Einmess-Bohrung. Default 5 mm.',
      },
      {
        key: 'probe_z_search_limit',
        label: 'probeZSearchLimit',
        unit: 'mm',
        minExclusive: 0,
        max: 50,
        info: 'Maximaler −Z-Weg bei der Bodenantastung der Einmess-Bohrung. Muss größer als approachDistance sein (OffsetOnly vom Standoff). Default 20 mm.',
      },
      {
        key: 'probe_z_lift_mm',
        label: 'probeZLift',
        unit: 'mm',
        min: 0,
        max: 20,
        info: 'Hub in +Z nach Bodenantastung vor der XY-Zwei-Sehnen-Suche. Default 2 mm.',
      },
      {
        key: 'probe_bore_timeout_s',
        label: 'probeBoreTimeout',
        unit: 's',
        min: 0.05,
        max: 600,
        info: 'Zeitlimit der gesamten Einmess-Bohrung inkl. optionaler Refine-Durchläufe. Default 60 s.',
      },
      {
        key: 'probe_bore_refine_passes',
        label: 'probeBoreRefinePasses',
        unit: '—',
        integer: true,
        min: 0,
        max: 5,
        info: 'Zusätzliche Zwei-Sehnen-Durchläufe vom gefundenen Mittelpunkt. 0 = einmal suchen.',
      },
      {
        key: 'approach_distance',
        label: 'approachDistance',
        unit: 'mm',
        minExclusive: 0,
        max: 100,
        info:
          'Grob-Anfahrhöhe (Standoff) über Einmessbohrungs- bzw. Prüfpunkt-Z. Schnelle Z-Fahrt endet hier; ' +
          'danach Feinanfahrt (Prüfpunkt) bzw. z_first BoreSearch (Einmessen). Default 15 mm.',
      },
    ],
  },
  {
    id: 'rotation_calib',
    title: 'Rotationskalibrierung',
    fields: [
      {
        key: 'rotation_calib_enabled',
        label: 'rotationCalibEnabled',
        unit: '0/1',
        integer: true,
        min: 0,
        max: 1,
        info: '1: nach KM die Rotationskalibrierung K1–K2–K3 im Wizard ausführen. 0: überspringen. Nur GUI.',
      },
      {
        key: 'nom_edge_mm',
        label: 'nomEdgeLength',
        unit: 'mm',
        minExclusive: 0,
        max: 500,
        info: 'Soll-Länge der Kanten K1K2 und K2K3 (Schrittverlust-Prüfung). Default 55 mm. Nur GUI.',
      },
      {
        key: 'tol_length_warn_mm',
        label: 'tolLengthWarn',
        unit: 'mm',
        min: 0,
        max: 50,
        info: 'Längenabweichung ab der eine Warnung erscheint. Nur GUI.',
      },
      {
        key: 'tol_length_reject_mm',
        label: 'tolLengthReject',
        unit: 'mm',
        min: 0,
        max: 50,
        info: 'Längenabweichung ab der Übernehmen gesperrt wird (vermuteter Schrittverlust). Nur GUI.',
      },
      {
        key: 'tol_yaw_ignore_deg',
        label: 'tolYawIgnore',
        unit: '°',
        min: 0,
        max: 10,
        info: 'Unterhalb dieser Yaw-Abweichung wird plateYaw = 0 gespeichert (Rauschen). Nur GUI.',
      },
      {
        key: 'tol_yaw_apply_deg',
        label: 'tolYawApply',
        unit: '°',
        min: 0,
        max: 10,
        info: 'Ab dieser Yaw-Abweichung wird die Korrektur empfohlen. Nur GUI.',
      },
      {
        key: 'tol_ortho_warn_deg',
        label: 'tolOrthoWarn',
        unit: '°',
        min: 0,
        max: 10,
        info: 'Warnschwelle für Abweichung von 90° zwischen K1K2 und K2K3. Nur GUI.',
      },
      {
        key: 'tol_ortho_reject_deg',
        label: 'tolOrthoReject',
        unit: '°',
        min: 0,
        max: 10,
        info: 'Reject-Schwelle für Rechtwinkligkeit (keine stille Yaw-Korrektur). Nur GUI.',
      },
      {
        key: 'tol_parallel_warn_deg',
        label: 'tolParallelWarn',
        unit: '°',
        min: 0,
        max: 10,
        info: 'Warnschwelle für Inkonsistenz der beiden Kantenwinkel (|θx−θy|). Nur GUI.',
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
          'TCP-Korrektur X: machine = plateOrigin + Rz(yaw)*platePoint + toolOffset. ' +
          'Nur durch Einmess-Bohrung (FullCalib/Install oder OffsetOnly) setzbar — nicht manuell änderbar.',
      },
      {
        key: 'tool_offset_y',
        label: 'toolOffset.y',
        unit: 'mm',
        readOnly: true,
        info: 'TCP-Korrektur Y. Nur durch Einmess-Bohrung setzbar — nicht manuell änderbar.',
      },
      {
        key: 'tool_offset_z',
        label: 'toolOffset.z',
        unit: 'mm',
        readOnly: true,
        info: 'TCP-Korrektur Z. Nur durch Einmess-Bohrung setzbar — nicht manuell änderbar.',
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
    v_bore_search: 1,
    bore_refine_passes: 0,
    rotation_calib_enabled: 1,
    nom_edge_mm: 55,
    tol_length_warn_mm: 0.3,
    tol_length_reject_mm: 1.0,
    tol_yaw_ignore_deg: 0.05,
    tol_yaw_apply_deg: 0.2,
    tol_ortho_warn_deg: 0.2,
    tol_ortho_reject_deg: 1.0,
    tol_parallel_warn_deg: 0.2,
    v_unclamp: 1,
    v_homing: 5,
    v_homing_fast: 18,
    a_homing_fast: 500,
    force_path_dz_mm: 0.01,
    force_path_dz_fine_mm: 0.005,
    force_near_target_frac: 0.8,
    phase3_v_insert_fine: 0.5,
    homing_approach_distance: 50,
    a_testpoints: 0,
    a_manual: 500,
    corner_testpoints: 0,
    a_homing: 0,
    jog_min_step_mm: 0.1,
    fault_manual_max_step_mm: 1,
    fault_manual_v_max: 5,
    fault_manual_a: 500,
    fault_manual_hard_limit_x_N: 50,
    fault_manual_hard_limit_y_N: 50,
    fault_manual_hard_limit_N: 50,
    phase1_v_extract: 1,
    phase1_v_insert: 1,
    phase1_a: 0,
    phase1_corner: 0,
    phase3_v_extract: 2,
    phase3_v_insert: 2,
    phase3_a: 0,
    phase3_corner: 0,
    force_filter: 0.05,
    xy_tolerance_N: 0.5,
    xy_correction_step: 0.05,
    xy_max_correction: 2,
    insertion_buffer: 1,
    after_seat_retract_mm: 2,
    extract_after_free_mm: 0,
    extract_after_free_v: 1,
    insert_search_distance: 10,
    force_range_N: 30,
    force_hard_limit_x_N: 30,
    force_hard_limit_y_N: 30,
    force_hard_limit_N: 30,
    force_calib_points: 5,
    force_calib_step_g: 500,
    collision_approach_x_N: 20,
    collision_approach_y_N: 20,
    collision_approach_N: 20,
    collision_retract_x_N: 30,
    collision_retract_y_N: 30,
    collision_retract_N: 30,
    collision_clamp_x_N: 20,
    collision_clamp_y_N: 20,
    collision_clamp_N: 20,
    collision_unclamp_x_N: 20,
    collision_unclamp_y_N: 20,
    collision_unclamp_N: 20,
    collision_contact_x_N: 30,
    collision_contact_y_N: 30,
    collision_contact_N: 30,
    collision_measure_x_N: 150,
    collision_measure_y_N: 150,
    collision_measure_N: 150,
    spreader_extend_time_s: 0.5,
    spreader_retract_time_s: 0.5,
    force_tolerance_N: 0.5,
    coord_tolerance_mm: 0.5,
    test_lateral_force_N: 5,
    z_limit_pos_mm: 10,
    z_limit_neg_mm: 15,
    anomaly_skip: 0,
    probe_v_approach: 10,
    v_search: 1,
    probe_wall_force_N: 0.5,
    probe_floor_force_N: 0.5,
    probe_xy_search_limit: 5,
    probe_z_search_limit: 20,
    probe_z_lift_mm: 2,
    probe_bore_timeout_s: 60,
    probe_bore_refine_passes: 0,
    approach_distance: 15,
    home_offset_x: 0,
    home_offset_y: 0,
    home_offset_z: 0,
    tool_offset_x: 0,
    tool_offset_y: 0,
    tool_offset_z: 0,
    tool_offset_change_tolerance_mm: 0.5,
    safe_z_buffer: 30,
    safe_z_calib_buffer: 30,
    sample_timeout_s: 0.5,
    heartbeat_timeout_s: 0.3,
    xy_correct_timeout_s: 30,
    xy_search_limit: 5,
    preclamp_confirm: 1,
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
  const range = cfg.force_range_N;
  const hardKeys = ['force_hard_limit_x_N', 'force_hard_limit_y_N', 'force_hard_limit_N'];
  for (const key of hardKeys) {
    const hard = cfg[key];
    if (!(hard > 0) || hard > range) {
      return { ok: false, error: `${key} muss größer als 0 und kleiner oder gleich dem Messbereich sein` };
    }
  }
  const collisionKeys = [
    'collision_approach_x_N',
    'collision_approach_y_N',
    'collision_approach_N',
    'collision_retract_x_N',
    'collision_retract_y_N',
    'collision_retract_N',
    'collision_clamp_x_N',
    'collision_clamp_y_N',
    'collision_clamp_N',
    'collision_unclamp_x_N',
    'collision_unclamp_y_N',
    'collision_unclamp_N',
    'collision_contact_x_N',
    'collision_contact_y_N',
    'collision_contact_N',
  ];
  for (const key of collisionKeys) {
    if (cfg[key] > range) {
      return { ok: false, error: `${key} darf den Messbereich ${range} N nicht überschreiten` };
    }
  }
  const measurePctKeys = ['collision_measure_x_N', 'collision_measure_y_N', 'collision_measure_N'];
  for (const key of measurePctKeys) {
    if (!(cfg[key] > 100) || cfg[key] > 500) {
      return { ok: false, error: `${key} muss als Prozent der Prüfkraft > 100 und ≤ 500 sein` };
    }
  }
  const recoveryHard = [
    ['fault_manual_hard_limit_x_N', 'force_hard_limit_x_N'],
    ['fault_manual_hard_limit_y_N', 'force_hard_limit_y_N'],
    ['fault_manual_hard_limit_N', 'force_hard_limit_N'],
  ];
  for (const [recKey, hardKey] of recoveryHard) {
    if (cfg[recKey] < cfg[hardKey]) {
      return {
        ok: false,
        error: `${recKey} muss ≥ ${hardKey} sein (Recovery-Hard-Limits dürfen nicht unter den normalen liegen)`,
      };
    }
  }
  const points = cfg.force_calib_points;
  const step = cfg.force_calib_step_g;
  const heaviest = (points * step / 1000) * 9.80665;
  if (heaviest > range + 1e-6) {
    return {
      ok: false,
      error: `Die letzte Kalibrierstufe liegt bei ${heaviest.toFixed(2)} N und überschreitet den Messbereich ${range} N`,
    };
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
