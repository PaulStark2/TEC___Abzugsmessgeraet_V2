# GUI / HMI (Agent 2)

Web-basierte Bedienoberfläche für den automatisierten 3-Achs-Zugprüfstand.

Zielplattform: Raspberry Pi (siehe Projektspezifikation). Entwicklung und erster Test am PC.

## Start (PC)

Voraussetzung: Node.js LTS (empfohlen ≥ 18).

```bash
cd gui
npm install
npm start
```

Browser: [http://localhost:3000](http://localhost:3000)

Simulations-UI (eigenständige Seite, M15):
[http://localhost:3000/simulation](http://localhost:3000/simulation)

## Development Runtime

GUI und Simulation/Controller-Host haben **unabhängige** Lebenszyklen (M14.2).

| Command | Wirkung |
|---------|---------|
| `npm run dev:start-gui` | Nur GUI (`MACHINE_CLIENT=protocol`, Port 3000) |
| `npm run dev:start-simulation` | Nur `simulation_protocol` (Port 8766 + Plant-Kanal 8767) |
| `npm run dev:stop-gui` | Nur Projekt-GUI beenden |
| `npm run dev:stop-simulation` | Nur Projekt-`simulation_protocol` beenden |
| `npm run dev:start-all` | Convenience: Simulation, dann GUI |
| `npm run dev:stop-all` | Beide Projektprozesse beenden |

Aliase: `dev:start` → `start-all`, `dev:stop` → `stop-all`.

```bash
cd gui
npm run dev:start-simulation   # Controller/Host ohne GUI
npm run dev:start-gui          # HMI ohne erzwungenen Host (dann offline)
npm run dev:stop-gui           # Simulation bleibt laufen
npm run dev:stop-simulation    # GUI bleibt laufen (offline)
```

| Port | Dienst |
|------|--------|
| **3000** | Node GUI (`server/index.js`) |
| **8766** | M12 `simulation_protocol` (Core-Host) |
| **8767** | Simulations-Plant-Kanal (nur PC-Simulation, **kein** M12) |

**Sicherheit:** Es werden **keine** fremden Node-Prozesse pauschal beendet. Nur Prozesse, die eindeutig zu diesem Repository gehören. PID-Dateien unter `gui/.dev-runtime/` sind Hilfsmittel und werden vor dem Kill erneut geprüft.

Architektur-Nachweis (ohne Browser):

```bash
npm run proof:architecture
```

Siehe auch [`docs/M14_2_Architecture.md`](../docs/M14_2_Architecture.md).

Simulations-Smoke-Test (M15, ohne Browser, eigene Ports):

```bash
npm run smoke:m15
```

Siehe auch [`docs/M15_Simulation_UI.md`](../docs/M15_Simulation_UI.md).

Bei `EADDRINUSE`:

```bash
npm run dev:stop-all
```

### Manueller Fallback

Nur GUI (wie bisher):

```bash
npm start
```

Host manuell:

```bash
.\build\simulation_protocol\simulation_protocol.exe
```

Dann GUI mit Env wie unter M14.

Optional Mock (Offline, JS-Kinematik nur im Mock):

```bash
set MACHINE_CLIENT=mock
set PORT=3000
npm start
```

### G3.5 — Core-Sim (M8.5)

Temporärer Pre-Protocol-Transport (TCP/JSON) — **nicht** das spätere M12/M13-Kommunikationsprotokoll.

1. Core-Host bauen und starten (eigener Prozess):

```bash
cmake --build build --target simulation_interactive
.\build\simulation_interactive\simulation_interactive.exe
```

Optional Port: `simulation_interactive.exe 8765`

2. GUI gegen den Host:

```bash
cd gui
set MACHINE_CLIENT=sim
set SIM_HOST=127.0.0.1
set SIM_PORT=8765
npm start
```

Three.js wird lokal per npm eingebunden und unter `/vendor/three/` ausgeliefert (kein CDN).

#### Manueller Gate-Test (3D-Viewport)

1. MoveTo `(10,0,0)` dann `(20,0,0)` — TCP nur entlang X  
2. MoveTo `(20,10,0)` — entlang Y  
3. MoveTo `(20,10,10)` — entlang Z  
4. MoveTo `(30,20,20)` — kombinierte XYZ-Bewegung  
5. Pause während Bewegung — Pose friert  
6. Resume — Core setzt fort  
7. Stop — `Stopped`; Resume wirkungslos; neues MoveTo erlaubt  

Koordinatensystem: Core `(x,y,z)` → Three.js `(x,y,z)` 1:1, Kamera **Z-up**.

(`MACHINE_CLIENT=protocol` → M14 ProtocolMachineClient, siehe unten.)

### M14 — ProtocolMachineClient (M12 Binary TCP)

GUI spricht mit `simulation_protocol` über das **M12-Binärprotokoll** (nicht JSON).

| Mode | Env | Transport | Port | Host |
|------|-----|-----------|------|------|
| `mock` | `MACHINE_CLIENT=mock` | In-process JS | — | MockMachineClient |
| `sim` | `MACHINE_CLIENT=sim` | TCP newline JSON (M8.5) | **8765** | `simulation_interactive` |
| `protocol` | `MACHINE_CLIENT=protocol` | TCP M12 binary (M14) | **8766** | `simulation_protocol` |

1. Core-Host bauen und starten:

```bash
cmake --build build --target simulation_protocol
.\build\simulation_protocol\simulation_protocol.exe
```

Optional Port: `simulation_protocol.exe 8766`

2. GUI:

```bash
cd gui
set MACHINE_CLIENT=protocol
set PROTOCOL_HOST=127.0.0.1
set PROTOCOL_PORT=8766
npm start
```

Weitere Env-Vars:

| Variable | Default | Bedeutung |
|----------|---------|-----------|
| `PROTOCOL_HOST` | `127.0.0.1` | TCP-Host |
| `PROTOCOL_PORT` | `8766` | TCP-Port |
| `PROTOCOL_ACK_TIMEOUT_MS` | `3000` | Wartezeit auf RSP_ACK/NACK |

**Single-Client:** `simulation_protocol` akzeptiert einen Client; ein neuer Connect ersetzt den vorherigen.

**Disconnect / Offline:** Wenn `simulation_protocol` beendet wird oder die TCP-Verbindung abreißt:

- `status.connected === false`
- mutierende Commands (`moveTo`, `jog`, `pause`, …) liefern `{ ok: false, error: 'Machine connection unavailable' }`
- Pose bleibt auf dem letzten bekannten Wert (keine lokale Simulation)
- Statusleiste zeigt „Maschine offline“
- nach Neustart des Hosts reconnectet der Client automatisch; Commands sind erst wieder erlaubt, wenn TCP steht **und** ein Status empfangen wurde

**Wichtig bei manueller Abnahme (Port-Konflikte):**

Wenn `npm start` mit `EADDRINUSE :::3000` fehlschlägt, läuft noch ein **alter** GUI-Prozess. Der Browser spricht dann weiter mit diesem alten Prozess — nicht mit dem neu gestarteten.

**Empfohlen:** `npm run dev:stop` bzw. den Workflow unter [Development Runtime](#development-runtime).

Ebenso: Wenn nach dem „Beenden“ von `simulation_protocol` weiterhin Moves funktionieren, prüfe ob noch ein **zweiter** Host auf 8766 läuft (z. B. Restprozess aus früheren Tests):

```powershell
netstat -ano | findstr ":3000"
netstat -ano | findstr ":8766"
Get-CimInstance Win32_Process -Filter "Name = 'simulation_protocol.exe'" | Select ProcessId,CommandLine
Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'server/index.js' } | Select ProcessId,CommandLine
```

Diagnose gegen laufende GUI:

```bash
npm run diagnose:g14
# erwartet u.a. GET /api/machine → machineClientMode=protocol
```

Offline-Beweis (eigener Ephemeral-Port, ohne 3000/8766):

```bash
npm run proof:offline
```

**Unterschiede zu Mock:**

- `setSpeed` → `{ ok: false, error: 'Not supported over protocol' }` (kein M12-Command)
- `home` → Workaround `moveTo(0,0,0)` (kein echtes Homing in Core)
- `jog` → `moveTo` relativ zur aktuellen Pose
- Kein `tick()` in Node.js — Host treibt die Simulation

Tests:

```bash
npm run test:protocol
# mit laufendem simulation_protocol + GUI (MACHINE_CLIENT=protocol):
npm run smoke:g14
```

Siehe auch [`docs/M13_Sim_Protocol.md`](../docs/M13_Sim_Protocol.md).

## Meilensteine

### G1 — Scaffold

- Node.js-Server (natives `http`)
- Statische HMI unter `public/`

### G2 — Mock Machine + Live-Status

- `MachineClient`-Vertrag + Factory
- `MockMachineClient`
- WebSocket `/ws/status` (~5 Hz)
- Live-Anzeige von `machineState`, `motionState`, `x/y/z`

### G3 — Maschinensteuerung (M5-äquivalent)

- Commands: `moveTo`, `pause`, `resume`, `stop`
- HTTP `POST /api/command`
- HMI-Eingaben für Zielpose + Pause/Resume/STOP
- Soft-STOP ≠ Hardware-E-Stop
- Browser sendet nur Commands; Mock bleibt Zustandsautorität

### G3.5 — SimMachineClient + Three.js TCP-View (M8.5 Gate)

- `MACHINE_CLIENT=sim` → TCP zu `simulation_interactive`
- Bewegung nur im C++ `Machine` / MotionPlanner / Axis
- Three.js 3D-Viewport (npm lokal): Achsen, Ursprung, TCP-Punkt, Linie Ursprung→TCP
- TCP/JSON = **temporärer Pre-Protocol-Simulations-Transport** (ersetzt später durch M12/M13)

### G4 — HMI-Struktur und Navigation

- Persistente App-Shell: Header, globale Statusleiste, 3D-Viewport
- Hash-Router (`#menu`, `#manual`, …) ohne SPA-Framework
- Platzhalter-Seiten für alle Prüfstandsbereiche (G5+)
- G3-Steuerung auf `#manual`; globaler Soft-STOP in der Statusleiste
- Fx/Fy/Fz in Statusleiste vorbereitet (`—` bis Server liefert Werte)

Navigation:

| Hash | Seite |
|------|--------|
| `#menu` | Hauptmenü (Default) |
| `#load-points` | Prüftstellen laden (G5) |
| `#select-points` | Prüfstellen auswählen (G6) |
| `#procedure` | Prüf-/Alterungsprozedur (G8) |
| `#results` | Prüfergebnisse (G9) |
| `#manual` | Handmodus (G3 + G7) |
| `#plate` | Bauplatte wechseln (Stub G11) |
| `#calibrate` | Kalibrieren (Stub G12) |

Tests: `node scripts/g3_smoke.js`, `node scripts/g4_nav_smoke.js` (Server muss laufen).

### G5 — Prüftstellen laden

- `POST /api/program/upload` — Validierung + RAM-Store
- `GET /api/program/current` — geladenes Programm oder 404
- Tests: `g5_validate_testprogram.js`, `g5_program_store_tests.js`, `g5_upload_smoke.js`

### G6 — Prüfstellen auswählen

- Datenquelle: `GET /api/program/current`
- Server-Store: `selectionStore` (RAM, bestätigte Auswahl für G8)
- APIs: `GET/PUT/DELETE /api/program/selection`, `POST /api/program/selection/confirm`
- Neuer Program-Upload setzt Auswahl zurück
- UI `#select-points`: Tabelle, Checkboxen, Alles auswählen, Bestätigen; 3D nur Platzhalter
- Tests: `g6_selection_tests.js`, `g6_select_smoke.js` (Server ohne geladenes Programm starten)

### G7 — Handmodus

- Mock-Commands: `setSpeed`, `jog`, `pneumaticOpen`, `pneumaticClose`, `home` (+ bestehende G3-Commands)
- UI `#manual` und Kalibrier-Wizard (`jogPanel`): gemeinsame Jog-Steuerung
  - Modus **Einzelschritt**: Klick = ein `jog` mit eintippbarer Schrittweite (mm)
  - Modus **Dauerhalt**: Pointer halten = `moveTo` weit entlang der Achse, Loslassen = `stop`
  - Schrittweite ≥ `jog_min_step_mm` und Vielfaches davon (Raster); Server prüft dasselbe
- Parameter `jog_min_step_mm`: nur HMI/Host (`machine-config.json` + Katalog), **nicht** im M12-Wire
- Status erweitert um `manualSpeed`, `pneumaticExtended`, `homingInProgress`
- Test: `g7_manual_smoke.js`; Raster-Helper: `server/config/jogStep.test.js`

### G8 — Prüf-/Alterungsprozedur

- UI `#procedure`: Modus, Eingliederungskraft, Iterationen, Fortschritt, Start/Pause/Resume/Stop
- APIs: `GET /api/procedure/status`, `POST /api/procedure/start|pause|resume|stop`
- **Mock:** vollständige HMI-Simulation (Iterationen, Kräfte, Completion → Results)
- **Protocol:** `CmdLoadProgram` + `CmdStart` an Core-TestExecutor; Fortschritt aus StatusReport
- Modus/Kraft/Iterationen beim Protocol-Pfad: GUI-Parameter, Core führt sie **nicht** aus (transparent gekennzeichnet)
- Keine Completed→start()-Iterationsschleife in der GUI
- `MACHINE_CLIENT=sim`: Prozedur nicht unterstützt (Hinweis in UI/API)
- Tests: `g8_procedure_tests.js`, `g8_procedure_smoke.js`, `g8_protocol_procedure_smoke.js`

### G9 — Prüfergebnisse

- UI `#results`: Tabelle + CSV-Export-Stub (`GET /api/results/export`)
- `GET /api/results/current`
- Mock: volle simulierte Zeilen; Protocol: nur beobachtete Controller-Daten (`dataCompleteness: partial`)
- Test: `g9_results_smoke.js`

### G10 — GUI-Verbesserungen

HMI-Nacharbeit vor der nächsten größeren Prüfprozedur-Arbeit. Keine Controller-Logik im Browser.

- Fortschritt: `pointsDone / totalPoints` (letzter Punkt `n/n`), Abschlussbanner „Prozedur abgeschlossen“
- `DELETE /api/program/current`: Programm + Auswahl löschen; 409 bei laufender/pausierter Prozedur; Ergebnisse bleiben
- „Prüfstellen laden“: alle Punkte, scrollbare Tabelle (`Geladene Prüfstellen (N)`)
- Fault-Banner + Button `Fault zurücksetzen` (`clearFault` / `CmdClearFault`); Mock simuliert keinen erfolgreichen Reset
- Endanschlag-Warnungen aus `status.limits` (WARNING, nicht FAULT)
- Homing: Button `HOME`, Status aus Controller (`homingState` / Fallback); keine Sequenz in der GUI
- Anzeige-Mapping: OFFLINE, IDLE, RUNNING, PAUSED, WARNING, FAULT, HOMING, HOMED, STOPPED (Rohwerte bleiben sichtbar)
- Test: `npm run smoke:g10`

### G11 — Bauplatte wechseln (offen)

- UI `#plate`: Platzhalter; Spannsystem folgt später

### G12 — Kalibrieren (offen)

- UI `#calibrate`: PlateOrigin teach + manuelle Messtaster-Einmessung (M19.0)

### M15 — Simulations-UI und virtuelle Portalmaschine

- Eigenständige Seite `/simulation` (kein Hash-Bereich der Maschinen-GUI); HMI unverändert
- Hierarchische 3D-Portalmaschine (`js/sim/gantryView.js`): X-Portal → Y-Schlitten → Z-Säule → TCP
- Pose ausschließlich aus dem Controller-Status (`/ws/status` → `gantryView.setPose()`), keine JS-Kinematik
- Virtuelle Außenwelt in C++ (`simulation_plant/SimPlant`): positionsabhängige Max-Endschalter, `probeTriggered`-Impuls, E-Stop-Eingang, `fx = fy = fz = 0`
- Reaktion auf Endschalter/E-Stop entscheidet allein die Core-Safety-Logik (latched `Fault`)
- Separater Plant-Kanal auf Port 8767 (TCP/JSON-Zeilen), **keine** M12-Erweiterung
- APIs: `GET /api/simulation/state`, `POST /api/simulation/limits|probe|probe-disc|estop`
- Tests: `simplant_tests`, `plantchannel_tests` (CTest), `smoke:m15`

## Simulations-API (M15)

Nur virtuelle Sensorik der PC-Simulation — Maschinenbefehle laufen weiter über `/api/command`.

```json
GET  /api/simulation/state
POST /api/simulation/limits   { "xMax": 190, "yMax": 140, "zMax": 110 }
POST /api/simulation/probe    { "pulseMs": 100 }
POST /api/simulation/probe-disc { "x": 40, "y": 40, "z": 10, "diameter": 8, "thickness": 3, "toolDiameter": 2.5, "toolCylindricalHeight": 15 }
POST /api/simulation/estop    { "active": true }
```

Antwort: `{ "ok": true, "state": { ... }, "pose": { "x": 0, "y": 0, "z": 0 } }`.
Ist der Simulations-Host gestoppt, antwortet der Proxy mit HTTP 503 und einer
verständlichen Verbindungsmeldung.

## Status-WebSocket-Format

Pfad: `/ws/status`

```json
{
  "type": "status",
  "machineState": "Idle | Active | Paused | Stopped | Fault",
  "motionState": "Idle | Moving | AtTarget",
  "x": 0.0,
  "y": 0.0,
  "z": 0.0,
  "manualSpeed": 15,
  "pneumaticExtended": false,
  "homingInProgress": false
}
```

## Program-API (G5/G6)

`POST /api/program/upload` — Testprogramm JSON  
`GET /api/program/current` — aktuelles Programm  
`DELETE /api/program/current` — Programm und Auswahl löschen (G10; 409 wenn Prozedur läuft/pausiert)  
`GET /api/program/selection` — Auswahl-Entwurf/bestätigt  
`PUT /api/program/selection` — `{ "selectedSequenceNumbers": [1,2] }` oder `{ "selectAll": true }`  
`POST /api/program/selection/confirm` — Auswahl bestätigen (min. 1 Punkt)  
`DELETE /api/program/selection` — Entwurf löschen  

## Command-API

`POST /api/command` mit JSON-Body:

```json
{ "command": "moveTo", "x": 10, "y": 20, "z": 30 }
{ "command": "pause" }
{ "command": "resume" }
{ "command": "stop" }
{ "command": "setSpeed", "speed": 15 }
{ "command": "jog", "axis": "X", "direction": 1, "step": 1 }
{ "command": "pneumaticOpen" }
{ "command": "pneumaticClose" }
{ "command": "home" }
```

Antwort:

```json
{ "ok": true, "status": { "machineState": "...", "motionState": "...", "x": 0, "y": 0, "z": 0 } }
```

oder `{ "ok": false, "error": "..." }`.

## Architekturgrenzen

- Browser ist niemals Autorität über Machine-/Motion-State und integriert keine Pose
- Mock bleibt für Offline; Sim-Pfad delegiert an Core
- Später: derselbe HMI-Vertrag hinter `ProtocolMachineClient` (G13) über echtes M12/M13-Protokoll
- Phase-B-HMI ab G4 (Navigation); Fachlogik ab G5+
