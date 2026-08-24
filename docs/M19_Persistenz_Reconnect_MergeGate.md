# M19 Persistenz/Reconnect — Merge-Gate M19

## Ziel
Nach erfolgreicher M19-Kalibrierung muss `probe_center` auch nach TCP-Reconnect (Session-Aufbau) korrekt rehydriert werden und anschließend darf `CmdStart` nur dann akzeptieren, wenn `probe_center_valid == true` ist.

## Was war das Problem (P0)
1. `probe_center` sollte über `CmdSetProbeCenter` in den Core rehydriert werden können.
2. `CmdStart` sollte bei fehlendem `probe_center` nicht mehr pauschal als `ProgramEmpty`, sondern als `InvalidState` gemeldet werden.

## Produktionspfad (Verifikation im Code)
### Persistenzquelle
- `calibrationStore.load()` liest `gui/server/data/calibration.json`.
- Default bei fehlender Datei/ungültigem JSON:
  - `probeCenterValid: false`
  - `probeCenter: null`

### Session-Aufbau / onSessionReady
Ort: `gui/server/index.js`

Aufrufreihenfolge:
1. `machineClient.setMachineConfig(cfg)` (Fehler wird geloggt, wenn `ok == false`)
2. optional `machineClient.setPlateOrigin(...)` (nur wenn `plateOriginValid && plateOrigin` vorhanden)
3. optional `machineClient.setProbeCenter(...)` (nur wenn `probeCenterValid && probeCenter` vorhanden)

### Kommando → Bridge → Core
Pfad:
`setProbeCenter(...)` (JS)
→ `CmdSetProbeCenter` (`0x55`)
→ `simulation_bridge/CommandDispatcher.cpp`
→ `Machine::setProbeCenter(...)`
→ `CalibrationSet.probe_center_valid = true`
und die Koordinaten werden aus dem Payload übernommen.

Wichtig: Im Core gibt es kein separates, GUI-gesetztes Validitäts-Flag. `probe_center_valid` ist allein durch den erfolgreichen `setProbeCenter`-Pfad bestimmt.

## Zustandskonsistenz/Invarianten
Nach erfolgreicher Rehydrierung muss gelten:
- `probe_center_valid == true`
- `probe_center` entspricht dem persisted Wert aus `calibration.json`

Bei fehlender/ungültiger Persistenz muss gelten:
- `probe_center_valid == false`
- es darf kein alter oder Default-Wert fälschlich als gültig markiert werden

## Fehlerpfad bei Rehydrierung
Ort: `gui/server/index.js`

- `machineClient.setProbeCenter(...)` kann fehlschlagen (`{ ok: false }`) oder Exception werfen.
- Wir loggen jetzt explizit den Fehlerfall analog zu `setMachineConfig`:
  - `[CALIB] probe_center push on connect failed: ...`
- Dadurch darf die GUI nicht stillschweigend von einer erfolgreichen Core-Rehydrierung ausgehen.

## CmdStart — Ack-Semantik
Ort: `simulation_bridge/CommandDispatcher.cpp`

`CmdStart` meldet:
- `program_point_count == 0` → `ProgramEmpty`
- `!plate_origin_valid` → `InvalidState`
- `!isHomed()` → `InvalidState`
- `!probe_center_valid` → `InvalidState`
- Prozess bereits `Running` → `InvalidState`
- Wenn `executor_.start() == false`:
  - keine Rückgabe von `ProgramEmpty`, sondern `InvalidState` (diagnostische Korrektheit)

## Minimaler Integrationstest (Reconnect Auto-Start)
Ort: `tests/bridge_command_tests.cpp`

Neu ergänzt:
1. Frischer Core (ohne M19-ProbeCalibration)
2. Homing + PlateOrigin setzen
3. `CmdSetProbeCenter` mit festen Koordinaten (Payload wie `MOVE_TO`) senden
4. Dispatcher/Bridge-Ack prüfen und sicherstellen, dass `probe_center_valid == true` ist
5. Programm laden und bei dynamischem Safe-Z (`max(point.z)+safe_z_buffer`) per Tick bereitstellen
6. `CmdStart` senden → `AckResultCode::Ok`
7. Assertions:
   - `probe_center_valid == true`
   - `probe_center == stored_center`

Damit wird der Lückennachweis geschlossen:
`CmdSetProbeCenter` reicht nicht nur zum Rehydrieren, sondern belegt auch den Auto-Start danach.

## Tests (lokal ausgeführt)
- `ctest --output-on-failure` → **37/37** passed
- `npm run test:protocol` → **PASS**

## Architektur-Invarianten (Regression-Schutz)
- `probe_offset_reference` bleibt ausschließlich Offset-Mathematik (`tool_offset = probe_offset_reference - measured_probe`).
  Keine Kopplung an Such-/Bewegungslogik als Zielkoordinate.
- SimPlant `ProbeDisc.center` gelangt nicht als produktiver Pfad in den Core:
  `probe_center` wird nur durch M19 (ProbeCalibration) oder durch `CmdSetProbeCenter` (Rehydrierung) gesetzt.

