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

Optional:

```bash
set MACHINE_CLIENT=mock
set PORT=3000
npm start
```

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

- Nur Arbeit unter `gui/`
- Keine Anbindung an `core/`, CMake, UART oder STM32 in frühen Meilensteinen
- Der Browser ist niemals Autorität über Machine-/Motion-State
- Später: derselbe Vertrag hinter `ProtocolMachineClient` (G12)
- Phase-B-HMI (Menüs) ab G4
