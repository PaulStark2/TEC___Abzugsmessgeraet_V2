# Homing und Maschinenkoordinatensystem (MCS)

## Frames

```text
Raw (Axis::position / rawX/Y/Z)
    ↓  machine = raw − reference_offset
Machine (MotionPlanner::x/y/z, Status, Kalibrierung)
    ↓  machine = plate_origin + plate_point + tool_offset
Plate (Programmpunkte)
```

| Variable | Frame | Bedeutung |
|----------|-------|-----------|
| `rawX/Y/Z` | Raw | Kontinuierliche Open-Loop-Integration; **kein Teleport** |
| `x/y/z` | Machine | `raw − offset` |
| `MachineConfig.calibration.home_offset` | Skalar **R** | Nur Retract-Distanz vom +Max-Schalter |
| `CalibrationSet.home_offset` | Machine | Kalibrierter Homing-Punkt nach MCS-Definition |
| `machine_coordinate_system_valid` | Flag | MCS wurde einmal definiert |
| `probe_center` / `plate_origin` / `tool_offset` | Machine | Geometrie; unverändert bei normalem `home()` |

## Drei getrennte Operationen

### 1. MCS definieren — `defineMachineCoordinateSystem()` / `CmdDefineMcs`

Nur über die GUI-Funktion **„MCS und Bauplatte/Messtaster definieren und kalibrieren“** (oder explizites Protocol).

`CmdDefineMcs` ändert nur den **Core**. Die committed `calibration.json` wird erst bei **persistMcs / Commit** ersetzt.
Abbruch vor Commit: Core wird aus dem committed Store zurückgespielt (`pushCommittedCalibration`).

```text
Seek +Max → Stop (raw bleibt)
→ reference: machine := raw (Identity auf der Achse)
→ Retract um R
→ nach Z/X/Y: home_offset = aktuelle Machine-Pose
→ machine_coordinate_system_valid = true
→ plate_origin_valid / probe_center_valid = false
→ tool_offset = 0
```

### 2. Reference Homing — `home()` / `CmdHome`

**Normales Homing definiert niemals das MCS neu.**

Voraussetzung: `mcs_valid == true`, sonst Rejected.

```text
Seek +Max → Stop (raw bleibt)
→ reference: machine := home_offset + R   (gespeicherter Schalterpunkt)
→ Retract auf home_offset
→ isHomed = true
```

`probe_center`, `plate_origin`, `tool_offset`, `home_offset` bleiben unverändert.

Korrigiert Schrittverlust: Software-Pose wird am realen Schalter wieder an die kalibrierte Machine-Koordinate gebunden.

### 3. Restore nach Neustart — `restoreMachineCoordinateSystem()` / `CmdRestoreMcs`

Lädt persistiertes `home_offset` + `mcs_valid` **ohne Bewegung** und **ohne** Geometrie zu invalidieren.
`reference_offset` wird **nicht** persistiert; er entsteht wieder beim nächsten Reference-Home.

## Persistenz

Host speichert in `calibration.json`:

- `machineCoordinateSystem.valid`
- `machineCoordinateSystem.homePosition` (= `CalibrationSet.home_offset`)
- plate / probe / tool (+ Valid-Flags)

Connect-Reihenfolge: Restore MCS → ggf. Plate/Probe push.

Alte Dateien ohne MCS-Block → `mcs_valid = false` (kein Auto-Define).

## Simulation

- Gantry / Plant: **raw** (kontinuierlich, kein Teleport am Schalter).
- Statuszahlen: **Machine**.
- Plant-Limits gegen raw.
- StatusFlag `McsValid` (Bit 6).
