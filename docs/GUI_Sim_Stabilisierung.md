# GUI Stabilisierung — Paketnotizen

## Paket 1 — Prüfprozedur Completion-Latch
- Fix: `seenRunningSinceStart` in `protocolProcedureAdapter.js`
- Test: `protocolProcedureAdapter.test.js`

## Paket 2 — Plate-Koordinaten
- `coordinateTransform.js` (+ Test)
- Sim: Maschinen-Overlay nur mit `plate_origin_valid`
- Tabellen als Plate-Koordinaten beschriftet

## Paket 3 — Homing
- Physisches Retract; `MachineConfig.calibration.home_offset` = Abstand R vom +Max (0 → 2 mm)
- Bei +Max: `referenceMachinePose` setzt die Achse auf MCS-Ursprung 0 (kein Raw-Teleport); nach Retract: Home = (−R,−R,−R)
- `CalibrationSet.home_offset` = aus R abgeleitete Home-Pose (nicht eingemessen)
- Details: [`docs/Homing.md`](Homing.md)
- Kein `limitWarning` während Homing
- GUI-Popup vor Prozedur-Start

## Paket 4 — Trajektorie
- `safeZPath.js`: Safe-Z → XY → Z Segmente (Core-Semantik)
- Traveled = TCP-Samples aus StatusReport

## Paket 5 — Simulation Geometrie
- Spreizer: Zylinder + TCP-Kugel Ø2 mm (Kegel entfernt)
- Prüfpunkte Ø≈2 mm
- Referenzmarker Home / plate_origin / probe_center (Core-only)

## Paket 6 — Labels
- Optional: Prüfpunktbeschriftungen (Nr. + Maschinenkoordinaten) und Trajektorienbeschriftungen (v + Richtung)
- Daten aus denselben `safeZPath`-Segmenten / Maschinenpunkten wie die Bahn

## Paket 7 — Layout
- Sim: linkes Panel scrollbar, rechtes Viewport sticky/fixiert

## Paket 8 — Parameterseite
- Kategorie-Tabs, breiteres Layout

## Paket 9 — Parameter SoT
- Sim-Felder für Ø/Dicke/Spreizer sind read-only (Anzeige aus MachineConfig)
- Plant-Kommando `setProbeDisc` ändert nur x/y/z; physikalische Größen kommen vom Core-Tick

## Paket 10 — Status-Konsistenz
- probe_center Marker nur bei Core `probeCenterValid`
- plate overlay nur bei Core+Store gültig
- Homing-Marker: `CalibrationSet.home_offset` (Latch Homing*→Homed), nicht live TCP-Pose

## Paket 11 — Overlay-Offset
- Sim-Gantry bleibt Plant/Raw; Overlay-Marker (Home/Plate/Punkte) sind Machine-Frame
- `referenceOffset` kommt als Core-Anzeigefeld neben `pose` aus Plant-`getState` (nicht aus asynchronem `plant − status`)
- Fallback: Latch aus Plant/Status, wenn das Feld fehlt
- Test: `gui/server/coord/workspaceReferenceOffset.test.js`
