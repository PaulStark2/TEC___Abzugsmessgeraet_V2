# Kraft-Weg-Messauswertung

Diese Seite beschreibt, welche Messwerte während der Prüfprozedur entstehen, wie sie vom Controller zur GUI gelangen und wie daraus Spitzenkräfte, Diagramme und Exporte berechnet werden. Die Echtzeitentscheidungen bleiben im plattformunabhängigen Core. Die Auswertung (Mittelwert, Standardabweichung, Overlay, CSV/PDF) liegt bewusst auf dem Host (Raspberry Pi / GUI-Server), damit der Regelungstakt nicht auf Datei-I/O oder Statistik wartet.

Verwandte Dokumente: `docs/Force_Calibration.md` (Kennlinie und Vorzeichen), `docs/XY_Kraftregelung.md` (Count-Fenster und Filter), `docs/M17_Procedure.md` / `docs/M18_Procedure.md` (Prozedur und ForcePath-Stream).

## Zweck und Abgrenzung

| Thema | Ort | Bemerkung |
|--------|-----|-----------|
| Kraftregelung Ein-/Ausgliedern | Core `ProcedureController::handleTestOrAge` | Schwellwerte Sollkraft / kraftfrei |
| Speichern der Messpaare `(z, Fz)` | Core `ForcePathRecorder` | Ringpuffer, kein Heap pro Sample |
| Telemetrie | `MsgForcePathChunk` (`0xA2`) | opportunistisch, Tick blockiert nie |
| Statistik, Diagramm, Export | GUI-Server | nach Abschluss der Prozedur |

Alterung (`TestMode::Aging`) bewegt dieselbe Bahn, zeichnet aber **keine** Kraft-Weg-Kurve auf. Die Kraft dient dort nur der Sicherheit und den Anomalieprüfungen.

## Physikalischer und signaltechnischer Rahmen

### Wegauflösung

Die Z-Achse arbeitet open-loop mit typisch **200 steps/mm** (Schrittweite **0,005 mm**). Das ist die mechanische Untergrenze der Positionierung, nicht die sinnvolle Rasterung der Kraft-Weg-Kurve.

### Kraftauflösung

| Stufe | Größenordnung | Quelle |
|-------|----------------|--------|
| ADC | 12 Bit, 0…4095 | Firmware / `AdcScaler` |
| Provisorische Skala ±`range_N` (Default ±30 N) | ≈ **0,015 N/LSB** | linear, bis Kennlinie committed |
| 100-ms-Count-Fenster | Mittelung der Rohzählwerte | `docs/XY_Kraftregelung.md` |
| Tiefpass `force_filter` | Default τ = 0,05 s | `MachineConfig` |
| Praktisches Rauschen nach Fenster | ca. **±0,1 N** | Streuungsabschätzung in der Kraftregelungsdoku |
| Kraftpfad-Anzeige | Format **0,01 N** | GUI |

Zug ist negativ, Druck positiv (`docs/Force_Calibration.md`).

| Richtung | Spitzenkraft in der Auswertung |
|----------|--------------------------------|
| Eingliedern (Druck) | \(\max_i \lvert F_{z,i}\rvert\) |
| Ausgliedern (Zug) | \(\max\{-F_{z,i}: F_{z,i}<0\}\) — Restdruck am Sitz zählt nicht als Ausglieder-Spitze |

### Warum nicht jedes Millisekunden-Sample?

Auf dem STM32 läuft der Core mit **1 kHz**. Ohne Abstandsgate würde bei 2 mm/s ein Sample alle **0,002 mm** entstehen. Die Kraft korreliert räumlich eher mit dem Count-Fenster und dem Filter:

- 100 ms bei 2 mm/s → **0,2 mm**
- Gruppenlaufzeit des Fensters ≈ 50 ms → **≈ 0,1 mm**

Deshalb speichert der Core standardmäßig nur, wenn

```text
|z − z_last| ≥ force_path_dz_mm    (Default 0,01 mm)
```

oder beim ersten Sample einer neuen Kurve (Leg-/Iterationswechsel / Umkehrpunkt).

Parametrierbar über MachineConfig / GUI (Phase 3):

| Parameter | Default | Bedeutung |
|-----------|---------|-----------|
| `force_path_dz_mm` | 0,01 mm | Basis-Mindestweg zwischen Samples |
| `force_path_dz_fine_mm` | 0,005 mm | Feineres Raster in der Insert-Feinphase |
| `force_near_target_frac` | 0,8 | Ab `Fz ≥ frac · Sollkraft` Feinphase |
| `phase3_v_insert_fine` | 0,5 mm/s | Reduzierte Insert-Geschwindigkeit in der Feinphase |

Am Übergang Insert→Extract schreibt der Core ein gemeinsames Sitz-Sample für beide Beine (gleicher `(z, Fz)`), bevor die +Z-Ausfahrt startet.

Dichter als die Kraftbandbreite (~0,05–0,1 mm) zu speichern liefert unter den Filterbedingungen kaum unabhängige Kraftinformation, erhöht aber UART-Last und Ringpuffer-Druck. Streaming muss den Ring (256 Samples) weiterhin rechtzeitig leeren.

## Aufnahme im Core

### Wann wird aufgenommen?

Nur in `ProcedurePhase::TestOrAge` und nur wenn `TestConfiguration.mode == Test`.

Der Zyklus hat die Beine:

| `cycle_leg_` | Bewegung | Aufnahme |
|--------------|----------|----------|
| 0 | erstes Ausgliedern bis kraftfrei (nach Zug) | **nein** (Öffnungsleg, verzerrt die Statistik nicht) |
| 1 | Eingliedern (−Z, Druck `Fz > 0`) | ja, `leg = Insert (0)` |
| 2 | Ausgliedern (+Z, Zug `Fz < 0`) | ja, `leg = Extract (1)` |

Das Sample-Tag kommt beim Speichern aus `cycle_leg_` (nicht aus einem verzögerten Zustandsflag). Die Host-Auswertung korrigiert die Richtung anhand der Bahngeometrie (`Δz > 0` → Ausgliedern, `Δz < 0` → Eingliedern). Nicht über `max|Fz|`: Ausglieder-Kurven starten oft noch unter Restdruck, sonst würden sie fälschlich als Eingliedern gezählt.

Ausgliedern endet nicht beim ersten Durchfahren von 0 N (Druck→Zug), sondern erst nachdem Zugkraft (`Fz < −force_tolerance_N`) erkannt wurde und die Kraft erneut kraftfrei ist. Optional fährt `extract_after_free_mm` danach noch weiter (+Z) und die Messung läuft mit; `0` = Stopp am Kraftfrei-Punkt. Nachlauf und Rückfahrt zum Ausgliederpunkt nutzen `extract_after_free_v` (nicht `phase3_v_extract` / `phase3_v_insert`).

Nach abgeschlossener Iteration wird `iteration` erhöht und erneut Eingliedern gestartet.

### Ringpuffer

`ForcePathRecorder` (`core/ForcePathRecorder.hpp`):

- Kapazität **256** Samples
- O(1) `push` / `pop`
- Überlauf: ältestes Sample verwerfen, `dropped++`
- Jedes Sample trägt `iteration` und `leg`
- `popSameIterationAndLeg` bricht den Chunk ab, sobald Iteration oder Leg wechselt

Damit kann der Host getrennte Kurven für Ein- und Ausgliedern derselben Iteration bilden, ohne die Z-Richtung zu erraten.

## Protokoll: `MsgForcePathChunk` (0xA2)

Header (13 Byte) + `sample_count × 8` Byte:

| Feld | Typ | Bedeutung |
|------|-----|-----------|
| `point_index` | u8 | aktueller Programmpunkt |
| `iteration` | u16 | Iterationszähler |
| `sequence` | u32 | Chunk-Sequenz des Recorders |
| `sample_count` | u8 | 1…32 |
| `dropped` | u32 | kumulierte Ring-Drops |
| `leg` | u8 | 0 = Eingliedern, 1 = Ausgliedern |
| `z[i]`, `fz[i]` | float32 LE | Maschinen-Z [mm], Kraft [N] |

Streaming ist opportunistisch. Simulation und STM32-Firmware drainen den Recorder in `appendForcePathChunks` (bis zu acht Chunks pro Status-Push). Ein langsamer Pi darf den Tick nicht blockieren; fehlende Chunks erscheinen als Lücken bzw. erhöhtes `dropped`.

### Host-Pflicht (STM32)

Die Firmware muss `MsgForcePathChunk` mitsenden. Status allein reicht nicht: der Recorder füllt sich sonst und verwirft Samples. Umsetzung: `FirmwareHost::appendForcePathChunks` im Status-Push-Pfad (`HostIo`, typisch 50 Hz).

## Auswertung auf dem Host

Modul `gui/server/results/forcePathAnalysis.js`.

### Spitzenkraft einer Kurve

Eingliedern:

\[
F_{\mathrm{peak}}^{\mathrm{in}} = \max_i \lvert F_{z,i}\rvert
\]

Ausgliedern (nur Zugbereich):

\[
F_{\mathrm{peak}}^{\mathrm{ex}} = \max\{-F_{z,i}: F_{z,i}<0\}
\]

ohne Zugsamples bleibt die Spitze undefiniert (Kurve trotzdem im Diagramm).

### Mittelwert und Standardabweichung über Iterationen

Für die Menge der Spitzen \(\{F_{\mathrm{peak},k}\}_{k=1}^{n}\) je Prüfpunkt und je Richtung (Ein- bzw. Ausgliedern):

\[
\bar F = \frac{1}{n}\sum_{k=1}^{n} F_{\mathrm{peak},k}
\]

Stichproben-Standardabweichung (Bessel-Korrektur):

\[
s = \sqrt{\frac{1}{n-1}\sum_{k=1}^{n}(F_{\mathrm{peak},k}-\bar F)^2}
\quad (n \ge 2),\quad
s = \text{undefiniert bei } n < 2
\]

### Normierter Weg (gemeinsamer Hubrahmen)

Pro Prüfpunkt und Iteration teilen Ein- und Ausgliedern denselben Bezug \(z_{\mathrm{free}}\) (kraftfrei / Ausgliederpunkt):

\[
z_{\mathrm{rel},i} = z_i - z_{\mathrm{free}}
\]

- \(z_{\mathrm{rel}} = 0\): kraftfrei
- \(z_{\mathrm{rel}} < 0\): Richtung Sitz (Eingliederpunkt)

Damit liegen beide Beine übereinander. \(z_{\mathrm{free}}\) kommt bevorzugt vom ersten Insert-Sample, sonst vom letzten Extract-Sample. Die CSV enthält zusätzlich \(z\) absolut.

## GUI

Seite „Prüfergebnisse“ (`gui/public/js/pages/results.js`):

1. Prüfpunktwahl
2. Kraft-Weg-Diagramm mit **Chart.js 4** (lokal unter `/vendor/chart.js`, Zoom-Plugin + Hammer.js, kein CDN): dunkler Plot-Hintergrund, Overlay aller Kurven, Grid, formatierte Achsen, Tooltips, LTTB-Decimation, Zoom/Pan (Doppelklick = Reset). PDF-Export bleibt hell.
3. Ein-/Ausschalten einzelner Kurven (Checkboxen und Chart-Legende)
4. Kennwerte: Mittelwert und Standardabweichung; Einzelspitzen je Iteration hinter aufklappbarem „Einzelspitzen“ (scrollbar)
5. Umschaltbare, scrollbar begrenzte Tabelle der Messpaare; Klick markiert den Punkt im Diagramm und umgekehrt
6. Download als ZIP

Farblogik: Iteration = Hue; hell = Eingliedern (Druck), dunkel = Ausgliedern (Zug). Kurven-Sichtbarkeit nur über die kompakte scrollbar Toggle-Liste (keine Chart.js-Legende). Implementierung: `gui/public/js/pages/forcePathChart.js`.

Ergebnisse entstehen erst nach `ProcessState::Completed` oder `Aborted` (beobachteter Lauf). Vorher liefert `/api/results/current` 404.

Offline ohne Prüfstand: `POST /api/results/demo` (Button „Demo-Testdaten laden“ nur wenn keine Ergebnisse vorliegen) schreibt Fixture-Kurven in den In-Memory-Store. Kennzeichnung `backend: demo`. Abschalten:

- Button **„Demo-Daten löschen“** → `DELETE /api/results/current`
- Start einer echten Prüfung leert den Store automatisch
- Auf dem Pi dauerhaft sperren: `ALLOW_RESULTS_DEMO=0` (dann 403 auf den Demo-Endpoint)

## Export

`GET /api/results/export` liefert ein ZIP:

| Datei | Inhalt |
|-------|--------|
| `*_samples.csv` | Messpaare, Excel-DE: UTF-8-BOM, `;`, Dezimal `,`. Nach `PunktName`: Baujob, Platte, Auftrag, Zahn, Z_Boden_mm, Z_Anfahrt_mm |
| `*_summary.csv` | je Punkt/Richtung: dieselben Identitätsfelder, Anzahl, Mittel, StdAbw, Spaltenliste `Spitzen_N` |
| `*_<Punkt>.pdf` | Kraft-Weg mit Achsen `F [N]` / `z (relativ) [mm]`, Ticks, Gitter, Null-Linien |

PDF-Erzeugung mit `pdfkit` (Vektorpfade, ohne Browser-Engine). Farblogik identisch zur GUI.

## Verifikation (kurz)

- Codec-Roundtrip Chunk inkl. `leg` (C++ `protocol_tests`, JS `m12Codec.test.js`)
- Statistik-Unit-Test `forcePathAnalysis.test.js`
- Sim: Prüfmodus → Chunks → nicht-leere `analysis.points`
- Hardware: nach Flash Firmware Status-Push enthält `0xA2` während TestOrAge

## Annahmen und Grenzen

- Keine Encoder-Rückführung: Weg = kommandierte / integrierte Schrittposition.
- Ausglieder-Spitze = nur Zug (\(F_z < 0\)); Eingliedern bleibt max |Fz|.
- Leg 0 (Öffnen bis kraftfrei) geht nicht in die Auswertung ein.
- In-Memory-Ergebnisse: nur der letzte Lauf; kein persistentes Messarchiv.
