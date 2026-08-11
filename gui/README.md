# GUI / HMI (Agent 2)

Web-basierte Bedienoberfläche für den automatisierten 3-Achs-Zugprüfstand.

Zielplattform: Raspberry Pi (siehe Projektspezifikation). Entwicklung und erster Test am PC.

## Meilenstein G1 — Scaffold

Dieses Verzeichnis enthält die technische Basis:

- Node.js-Server (natives `http`, keine Extra-Abhängigkeiten)
- Statische HMI unter `public/`
- Noch keine Mock-Maschine, kein WebSocket, keine Maschinensteuerung

## Start (PC)

Voraussetzung: Node.js LTS (empfohlen ≥ 18).

```bash
cd gui
npm install
npm start
```

Dann im Browser öffnen: [http://localhost:3000](http://localhost:3000)

`npm install` ist in G1 optional (keine Runtime-Dependencies), bereitet aber den üblichen Workflow für spätere Meilensteine vor.

## Architekturgrenzen

- Nur Arbeit unter `gui/`
- Keine Anbindung an `core/`, CMake, UART oder STM32 in frühen Meilensteinen
- Der Browser ist niemals Autorität über Machine-/Motion-State
