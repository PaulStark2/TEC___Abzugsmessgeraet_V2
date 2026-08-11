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

(`MACHINE_CLIENT=protocol` ist für G12 vorgesehen und wirft in G2 noch einen Fehler.)

## Meilensteine

### G1 — Scaffold

- Node.js-Server (natives `http`)
- Statische HMI unter `public/`

### G2 — Mock Machine + Live-Status

- `MachineClient`-Vertrag + Factory (`gui/server/machineClient.js`)
- `MockMachineClient` (`gui/server/mock/mockMachineClient.js`)
- WebSocket-Statuskanal `ws://<host>/ws/status` (~5 Hz)
- HMI zeigt Live-`machineState`, `motionState`, `x/y/z`

Noch keine Steuercommands (kommen in G3).

## Status-WebSocket-Format

Pfad: `/ws/status`

JSON-Nachricht (ca. 5× pro Sekunde):

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

Semantik angelehnt an Core M5; Werte kommen ausschließlich vom Server/`MachineClient`, nicht aus Browser-Logik.

## Architekturgrenzen

- Nur Arbeit unter `gui/`
- Keine Anbindung an `core/`, CMake, UART oder STM32 in frühen Meilensteinen
- Der Browser ist niemals Autorität über Machine-/Motion-State
- Später: derselbe Vertrag hinter `ProtocolMachineClient` (G12)
