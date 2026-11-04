# HMI-Meldungssystem (Fehler / Statusleiste)

## Zweck

Maschinen-HMI mit:

- kompakter **Meldungszeile** in allen Menüs (aktive, unquittierte Meldung)
- Untermenü **Fehler** (Tabelle, Detail, Quittierung)
- **Historie** bis 7 Tage auf dem GUI-Server (`gui/server/data/messages.jsonl`)

Soft-Aborts der Prozedur liefern einen stabilen **`AbortReason`**-Code im StatusReport
(`abort_reason` u16). Die GUI mappt Codes auf deutsche Titel/Details
(`gui/server/messages/messageCatalog.js`).

## Architektur

```text
Core AbortReason / FaultReason ──► StatusReport (UART/TCP)
                                      │
GUI Server statusIngest ─────────────► messageStore (JSONL, 7 Tage)
                                      │
                         ┌────────────┴────────────┐
                         ▼                         ▼
                  Meldungszeile (WS)         Menü Fehler (REST)
                  page messageBus           Ack / Historie
```

| Komponente | Pfad |
|------------|------|
| Store | `gui/server/messages/messageStore.js` |
| Catalog (DE) | `gui/server/messages/messageCatalog.js` |
| Status-Kanten | `gui/server/messages/statusIngest.js` |
| Client-Bus | `gui/public/js/messageBus.js` |
| Seite | `gui/public/js/pages/faults.js` |
| Core-Codes | `core/AbortReason.hpp` |

## API

- `GET /api/messages?active=1&limit=200`
- `GET /api/messages/:id`
- `POST /api/messages/ack` body `{ id }`
- `POST /api/messages/ack-all`
- WS `/ws/status`: zusätzlich Frames `{ type: 'message_event', summary, … }`

## Quittierung vs. Fault Clear

- **Quittieren** entfernt die Meldung nur aus der aktiven Statuszeile; Historie bleibt.
  Die Statuszeile ist danach leer (CSS: `[hidden]` muss `display:none` erzwingen).
- **Fault zurücksetzen** (`CmdClearFault`) ist die Maschinen-Recovery und unabhängig vom Ack;
  beim Verlassen von `machineState=Fault` werden aktive `Fault.*`-Meldungen automatisch deaktiviert.
- Beim Verlassen von `processState=Aborted` werden aktive `Abort.*`-Meldungen automatisch
  deaktiviert (nächster Start / Idle).

## StatusReport-Wire

`kStatusReportPayloadWireSize = 46` (+ `abort_reason` u16 + `current_iteration` u16 LE nach `fault_manual`).

Nach Firmware-Update muss die GUI denselben Codec nutzen (Host + STM32 gemeinsam flashen/deployen).

## Erweiterung

1. Neuen `AbortReason`-Wert in `core/AbortReason.hpp` + `abortReasonMessage`
2. Call-Site `abortProcess(AbortReason::…)`
3. DE-Eintrag in `messageCatalog.js` (`ABORT_BY_ORDINAL`)
4. Optional GUI-only Codes unter `GUI_CODES` + `messageBus.reportMessage`

## Deploy-Hinweis Firmware

Nach Änderung der StatusReport-Wire-Größe (44 B) müssen Host-GUI und STM32 denselben
Codec nutzen. Hex neu bauen und flashen: `build-firmware/tec_firmware.hex`
(siehe `firmware/README.md`). Simulation und GUI-Server teilen denselben JS-Codec
(`gui/server/protocol/m12Codec.js`).
