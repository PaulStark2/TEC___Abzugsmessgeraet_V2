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

## Zwei Vorgänge (gleiche Z→X→Y State Machine)

### A. `defineMachineCoordinateSystem()` / erster `home()` ohne MCS

Definiert das MCS **einmalig**:

```text
Seek +Max → Stop (raw bleibt)
→ reference: machine := raw (Identity auf der Achse)
→ Retract um R
→ nach Z/X/Y: home_offset = aktuelle Machine-Pose
→ machine_coordinate_system_valid = true
```

Plate/Probe-Validität wird bei bewusster MCS-(Neu-)Definition gelöscht (neu einmessen).

### B. Normales `home()` (MCS bereits gültig)

**Definiert das MCS nicht neu.**

```text
Seek +Max → Stop (raw bleibt)
→ reference: machine := home_offset + R   (gespeicherter Schalterpunkt)
→ Retract auf home_offset
→ isHomed = true
```

`probe_center`, `plate_origin`, `tool_offset`, `home_offset` bleiben unverändert.

Korrigiert Schrittverlust: Software-Pose wird am realen Schalter wieder an die kalibrierte Machine-Koordinate gebunden.

## Simulation

- Gantry / Plant: **raw** (kontinuierlich, kein Teleport am Schalter).
- Statuszahlen: **Machine**.
- Plant-Limits gegen raw.
