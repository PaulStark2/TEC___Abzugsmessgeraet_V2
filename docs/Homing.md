# Homing und Maschinenkoordinatensystem (MCS)

## Frames

```text
Raw (Axis::position / rawX/Y/Z)
    ↓  machine = raw − reference_offset
Machine (MotionPlanner::x/y/z, Status, Kalibrierung)
    ↓  machine = plate_origin + plate_point + tool_offset
Plate (Programmpunkte)
```

Achsen sind parallel und gleichsinnig: **+Machine = +Raw = Richtung +Max-Schalter**.
Der Arbeitsraum liegt daher in **negativen** Maschinenkoordinaten.

| Variable | Frame | Bedeutung |
|----------|-------|-----------|
| `rawX/Y/Z` | Raw | Kontinuierliche Open-Loop-Integration; **kein Teleport** |
| `x/y/z` | Machine | `raw − offset` |
| `MachineConfig.calibration.home_offset` | Skalar **R** | Nur Retract-Distanz vom +Max-Schalter (0 → 2 mm) |
| `CalibrationSet.home_offset` | Machine | Abgeleitet: `(-Rx, -Ry, -Rz)` — nicht eingemessen |
| `machine_coordinate_system_valid` | Flag | MCS wurde einmal definiert (Ursprung an den +Max-Anschlägen) |
| `probe_center` / `plate_origin` / `tool_offset` | Machine | Geometrie; unverändert bei normalem `home()` |

**MCS-Ursprung** ist der mechanische +Max-Anschlag: `machine = (0, 0, 0)`.
**Homingpunkt** ist `(-R, -R, -R)`. Ein Wechsel von R in den Parametern ändert nur den Homingpunkt, nicht das MCS.

## Drei getrennte Operationen

### 1. MCS definieren — `defineMachineCoordinateSystem()` / `CmdDefineMcs`

Nur über die GUI-Funktion **„MCS und Bauplatte/Messtaster definieren und kalibrieren“** (oder explizites Protocol).

`CmdDefineMcs` ändert nur den **Core**. Die committed `calibration.json` wird erst bei **persistMcs / Commit** ersetzt.
Abbruch vor Commit: Core wird aus dem committed Store zurückgespielt (`pushCommittedCalibration`).

```text
Seek +Max (schnell) bis limit*Max == true
→ Kriech zurück (−Achse) bis limit*Max == false
→ Kriech vor (+Achse) bis limit*Max == true   ← steigende Flanke = Schalterbezug
→ referenceMachinePose: diese Achse machine := 0   (kein Raw-Teleport)
→ Retract um R nach −R
→ nach Z/X/Y: home_offset := (-Rx, -Ry, -Rz) aus Config
→ machine_coordinate_system_valid = true
→ plate_origin_valid / probe_center_valid = false
→ tool_offset = 0
```

Nur das **Endschalter-Bit** — keine Plant-`+max`-Koordinaten.
Die Kriechgeschwindigkeit ist `max(0.1·v_homing, 0.2 mm/s)`. Der Latch erfolgt an der steigenden Kriech-Flanke; Restfehler ≈ ein Kriechschritt sitzt im `reference_offset`, nicht in den Home-Zahlen.

### 2. Reference Homing — `home()` / `CmdHome`

**Normales Homing definiert niemals das MCS neu.**

Voraussetzung: `mcs_valid == true`, sonst Rejected.

```text
Seek +Max → Stop (raw bleibt)
→ reference: machine := 0 auf der aktiven Achse
→ Retract auf (-R, …) aus aktueller Config
→ isHomed = true
```

`probe_center`, `plate_origin`, `tool_offset` bleiben unverändert.
`home_offset` folgt dem aktuellen R (bereits bei `trySetMachineConfig` nachgezogen).

Korrigiert Schrittverlust: Software-Pose wird am realen Schalter wieder an MCS-Ursprung 0 gebunden.

### 3. Restore nach Neustart — `restoreMachineCoordinateSystem()` / `CmdRestoreMcs`

Setzt `mcs_valid` **ohne Bewegung** und **ohne** Geometrie zu invalidieren.
Das Wire-Payload xyz wird **nicht** als Ursprung verwendet; Home wird aus dem aktuellen R abgeleitet.
`reference_offset` wird **nicht** persistiert; er entsteht wieder beim nächsten Reference-Home.

## Persistenz

Host speichert in `calibration.json`:

- `machineCoordinateSystem.valid`
- `machineCoordinateSystem.origin = "positiveMaxSwitches"`
- `machineCoordinateSystem.homePosition` (abgeleitetes Snapshot `(-R,-R,-R)`, nicht die MCS-Definition)
- plate / probe / tool (+ Valid-Flags)

Connect-Reihenfolge: Restore MCS → ggf. Plate/Probe push.

Legacy-Dateien ohne `origin` (Identity-MCS, Home ≈ Plant-Max − R) → `mcs_valid = false`. MCS und Geometrie müssen neu kalibriert werden.

## Simulation

- Gantry-TCP / Plant: **raw** (kontinuierlich, kein Teleport am Schalter).
- Statuszahlen: **Machine** (Home ≈ −2, Anschlag ≈ 0). Der Zahlensprung am Latch ist die Frame-Definition.
- Overlay (Home/Plate/Probe/Bahn): Machine + `reference_offset` → Raw-Szene.
- Plant-Limits gegen raw → nur **`limit*Max` Bits** an den Core (keine numerische +Max-Ebene).
- StatusFlag `McsValid` (Bit 6).
- DefineMcs-Kriechsuche reagiert ausschließlich auf diese Bits.
