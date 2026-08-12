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

(`MACHINE_CLIENT=protocol` ist für G12 vorgesehen und wirft noch einen Fehler.)

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
| `#load-points` | Prüftstellen laden (Stub) |
| `#select-points` | Prüfstellen auswählen (Stub) |
| `#procedure` | Prüf-/Alterungsprozedur (Stub) |
| `#results` | Prüfergebnisse (Stub) |
| `#manual` | Handmodus (G3-Steuerung) |
| `#plate` | Bauplatte wechseln (Stub) |
| `#calibrate` | Kalibrieren (Stub) |

Tests: `node scripts/g3_smoke.js`, `node scripts/g4_nav_smoke.js` (Server muss laufen).

## Status-WebSocket-Format

Pfad: `/ws/status`

```json
{
  "type": "status",
  "machineState": "Idle | Active | Paused | Stopped | Fault",
  "motionState": "Idle | Moving | AtTarget",
  "x": 0.0,
  "y": 0.0,
  "z": 0.0
}
```

## Command-API

`POST /api/command` mit JSON-Body:

```json
{ "command": "moveTo", "x": 10, "y": 20, "z": 30 }
{ "command": "pause" }
{ "command": "resume" }
{ "command": "stop" }
```

Antwort:

```json
{ "ok": true, "status": { "machineState": "...", "motionState": "...", "x": 0, "y": 0, "z": 0 } }
```

oder `{ "ok": false, "error": "..." }`.

## Architekturgrenzen

- Browser ist niemals Autorität über Machine-/Motion-State und integriert keine Pose
- Mock bleibt für Offline; Sim-Pfad delegiert an Core
- Später: derselbe HMI-Vertrag hinter `ProtocolMachineClient` (G12) über echtes M12/M13-Protokoll
- Phase-B-HMI ab G4 (Navigation); Fachlogik ab G5+
