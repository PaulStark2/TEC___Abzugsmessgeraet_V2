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

Zug ist negativ, Druck positiv (`docs/Force_Calibration.md`). Die Spitzenkraft der Auswertung ist dennoch **max |Fz|**, damit das Ergebnis vorzeichenunabhängig und mit Zug- und Druckkontakten vergleichbar bleibt.

### Warum nicht jedes Millisekunden-Sample?

Auf dem STM32 läuft der Core mit **1 kHz**. Ohne Abstandsgate würde bei 2 mm/s ein Sample alle **0,002 mm** entstehen. Die Kraft korreliert räumlich eher mit dem Count-Fenster und dem Filter:

- 100 ms bei 2 mm/s → **0,2 mm**
- Gruppenlaufzeit des Fensters ≈ 50 ms → **≈ 0,1 mm**

Deshalb speichert der Core standardmäßig nur, wenn

```text
|z − z_last| ≥ force_path_dz_mm    (Default 0,1 mm)
```

oder beim ersten Sample einer neuen Kurve (Leg-/Iterationswechsel). Der Parameter liegt in `MachineConfig::Force::force_path_dz_mm` (Core). Er ist bewusst noch nicht Teil des M12-`MachineConfig`-Wire-Layouts (386 Byte), damit ältere GUIs die Konfiguration weiter setzen können; die Default-Rasterung gilt hostseitig im Core.

Dichter als **0,05 mm** zu speichern liefert unter den genannten Filterbedingungen kaum unabhängige Kraftinformation, erhöht aber UART-Last und Ringpuffer-Druck.

## Aufnahme im Core

### Wann wird aufgenommen?

Nur in `ProcedurePhase::TestOrAge` und nur wenn `TestConfiguration.mode == Test`.

Der Zyklus hat die Beine:

| `cycle_leg_` | Bewegung | Aufnahme |
|--------------|----------|----------|
| 0 | erstes Ausgliedern bis kraftfrei | **nein** (Öffnungsleg, verzerrt die Statistik nicht) |
| 1 | Eingliedern (Kraftfrei → Sollkraft) | ja, `leg = Insert (0)` |
| 2 | Ausgliedern (Sollkraft → Kraftfrei) | ja, `leg = Extract (1)` |

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

Für die Samplefolge einer Kurve \((z_i, F_{z,i})\):

\[
F_{\mathrm{peak}} = \max_i \lvert F_{z,i}\rvert
\]

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

### Relativer Weg

Für die Anzeige und den PDF-Vergleich gilt

\[
z_{\mathrm{rel},i} = z_i - z_0
\]

mit \(z_0\) = erstes Sample der jeweiligen Kurve. Die CSV enthält zusätzlich \(z\) absolut.

## GUI

Seite „Prüfergebnisse“ (`gui/public/js/pages/results.js`):

1. Prüfpunktwahl
2. Kraft-Weg-Diagramm: alle Kurven überlagert; Farbe = Iteration (Hue); hell = Eingliedern, dunkel = Ausgliedern
3. Ein-/Ausschalten einzelner Kurven
4. Kennwerte: Mittelwert und Standardabweichung der Spitzenkräfte für Ein- und Ausgliedern
5. Umschaltbare Tabelle der Messpaare; Klick markiert den Punkt im Diagramm
6. Download als ZIP

Ergebnisse entstehen erst nach `ProcessState::Completed` oder `Aborted` (beobachteter Lauf). Vorher liefert `/api/results/current` 404.

## Export

`GET /api/results/export` liefert ein ZIP:

| Datei | Inhalt |
|-------|--------|
| `*_samples.csv` | alle Messpaare: Punkt, Iteration, Leg, \(z\), \(z_{\mathrm{rel}}\), \(F_z\) |
| `*_summary.csv` | je Punkt und Leg: Anzahl, Mittel, StdAbw der Spitzen |
| `*_<Punkt>.pdf` | Diagramm-Overlay, Auftragsname, Punktbezeichnung, Kennwerte |

PDF-Erzeugung mit `pdfkit` (Vektorpfade, ohne Browser-Engine). Farblogik identisch zur GUI.

## Verifikation (kurz)

- Codec-Roundtrip Chunk inkl. `leg` (C++ `protocol_tests`, JS `m12Codec.test.js`)
- Statistik-Unit-Test `forcePathAnalysis.test.js`
- Sim: Prüfmodus → Chunks → nicht-leere `analysis.points`
- Hardware: nach Flash Firmware Status-Push enthält `0xA2` während TestOrAge

## Annahmen und Grenzen

- Keine Encoder-Rückführung: Weg = kommandierte / integrierte Schrittposition.
- Spitze = max |Fz|; eine vorzeichenbehaftete „nur Zug“-Definition wäre eine Konfigurationsoption, nicht der Default.
- Leg 0 (Öffnen bis kraftfrei) geht nicht in die Auswertung ein.
- In-Memory-Ergebnisse: nur der letzte Lauf; kein persistentes Messarchiv.
