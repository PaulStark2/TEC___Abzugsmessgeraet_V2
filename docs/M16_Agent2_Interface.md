# M16 — Schnittstellen für Agent 2 (Maschinen-GUI)

Diese Felder und Commands stammen **ausschließlich** aus der C++/M12-Implementierung
(`communication/ProtocolTypes.hpp`, `PayloadCodec.cpp`, `protocolTypes.js`, `m12Codec.js`).
Keine GUI-seitigen Annahmen über Byte-Offsets oder Enum-Werte verwenden — immer die
JS-Helfer in `gui/server/protocol/` nutzen.

## StatusReport (M12, 38 Bytes)

| Offset | Feld | JS (`statusReportToGuiStatus`) |
|--------|------|--------------------------------|
| 0 | `machineState` | `machineState` (String) |
| 1 | `motionState` | `motionState` |
| 2 | `processState` | `processState` |
| 27 | `limits` | `limits` (Bitmask, 6 Bits) |
| 35 | `homingState` | `homingState` |
| 36 | `blockedDirections` | `blockedDirections` |
| 37 | `flags` bit0 | `isHomed` |
| 37 | `flags` bit1 | `limitWarning` (auch abgeleitet aus limits/blocked) |

### Enum `ProtocolMachineState`

| Wert | Name |
|------|------|
| 0 | Idle |
| 1 | Active |
| 2 | Paused |
| 3 | Stopped |
| 4 | Fault |
| 5 | Homing |

### Enum `ProtocolHomingState`

| Wert | Name |
|------|------|
| 0 | None |
| 1 | HomingZ |
| 2 | HomingX |
| 3 | HomingY |
| 4 | Homed |
| 5 | HomingError |

### Limit-Bitmask (Bytes 27 und 36)

| Bit | Richtung |
|-----|----------|
| 0 | X- |
| 1 | X+ |
| 2 | Y- |
| 3 | Y+ |
| 4 | Z- |
| 5 | Z+ |

Byte 27 = Schalter physisch aktiv (HostInputs). Byte 36 = vom Core gesperrte Bewegungsrichtung.

## Commands (`POST /api/command`)

| Command | M12 | Hinweis |
|---------|-----|---------|
| `home` | `CmdHome` (0x15) | Core-Homing Z+→X+→Y+ |
| `clearFault` / `resetFault` | `CmdClearFault` (0x14) | Core prüft E-Stop und Fz |
| `stop`, `moveTo`, … | unverändert | — |

Antwort: `{ ok, status?, error? }` — `status` enthält die neuen Felder über den WebSocket-Pfad.

## Endanschläge (Soft Limits)

Endanschläge lösen **keinen** globalen `Fault` mehr aus. Anzeige:

- `limitWarning === true` → Warnzustand (Schalter aktiv oder Richtung gesperrt)
- `machineState === 'Fault'` nur bei E-Stop / Fz-Safety

## Tests vor GUI-Integration

```bash
ctest --test-dir build -R "limit_blocking|homing|safety|protocol"
cd gui && npm run test:protocol
```
