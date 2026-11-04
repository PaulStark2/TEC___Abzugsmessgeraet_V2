# Homing und Maschinenkoordinatensystem (MCS)

## Frames

```text
Raw (Axis::position / rawX/Y/Z)
    ↓  machine = raw − reference_offset
Machine (MotionPlanner::x/y/z, Status, Kalibrierung)
    ↓  machine = plate_origin + plate_point + tool_offset
Plate (Programmpunkte)
```

**+Machine = +Raw** auf jeder Achse. Soft-Blocks bleiben isotrop: `limit*Min` blockiert −, `limit*Max` blockiert +.

| Achse | Homing-Schalter | Seek | Arbeitsraum vom Ursprung | Home nach Retract R |
|-------|-----------------|------|---------------------------|---------------------|
| **X** | `limitXMin` (physische Homing-Ecke) | **−X** | **+X** | **+R** |
| **Y** | `limitYMax` | **+Y** | **−Y** | **−R** |
| **Z** | `limitZMax` | **+Z** | **−Z** | **−R** |

Firmware: `kInvertDir[0]=true`; physischer XP-Pin → `limitXMin`, physischer XN-Pin → `limitXMax`.

| Variable | Frame | Bedeutung |
|----------|-------|-----------|
| `rawX/Y/Z` | Raw | Kontinuierliche Open-Loop-Integration; **kein Teleport** |
| `x/y/z` | Machine | `raw − offset` |
| `MachineConfig.calibration.home_offset` | Skalar **R** | Retract-Distanz vom Homing-Schalter (0 → 2 mm) |
| `CalibrationSet.home_offset` | Machine | Abgeleitet: `(+Rx, -Ry, -Rz)` — nicht eingemessen |
| `machine_coordinate_system_valid` | Flag | MCS wurde einmal definiert |
| `probe_center` / `plate_origin` / `tool_offset` | Machine | Geometrie; unverändert bei normalem `home()` |

**MCS-Ursprung** je Achse am Homing-Schalter: `machine = 0`.
**Homingpunkt** ist `(+R, -R, -R)`. Ein Wechsel von R ändert nur den Homingpunkt, nicht das MCS.

## Drei getrennte Operationen

### 1. MCS definieren — `defineMachineCoordinateSystem()` / `CmdDefineMcs`

Nur über die GUI-Funktion **„MCS und Bauplatte/Einmessbohrung definieren und kalibrieren“** (oder explizites Protocol).

`CmdDefineMcs` ändert nur den **Core**. Die committed `calibration.json` wird erst bei **persistMcs / Commit** ersetzt.
Abbruch vor Commit: Core wird aus dem committed Store zurückgespielt (`pushCommittedCalibration`).

```text
X: Seek −X bis limitXMin; Y/Z: Seek + bis limit*Max
→ Kriech vom Schalter weg bis Bit false
→ Kriech zum Schalter bis Bit true   ← steigende Flanke = Schalterbezug
→ referenceMachinePose: diese Achse machine := 0   (kein Raw-Teleport)
→ Retract: X → +R; Y/Z → −R
→ nach Z/X/Y: home_offset := (+Rx, -Ry, -Rz)
→ machine_coordinate_system_valid = true
→ plate_origin_valid / probe_center_valid = false
→ tool_offset = 0
```

Nur das **Endschalter-Bit** — keine Plant-Koordinaten.
Die Kriechgeschwindigkeit ist `max(0.1·v_homing, 0.2 mm/s)`.

### 2. Reference Homing — `home()` / `CmdHome`

**Normales Homing definiert niemals das MCS neu.**

Voraussetzung: `mcs_valid == true`, sonst Rejected.

Wenn in dieser Session schon einmal erfolgreich gehomed wurde (`session_machine_frame_trusted`;
überlebt Jog und Soft-STOP/`invalidateHoming`, anders als `isHomed` / `HomingState::Homed`):

```text
pro Achse Z → X → Y:
  Schnellanfahrt (v_homing_fast, a_homing_fast) zur Homingpose minus homing_approach_distance
  → langsame Blind-Suche (v_homing, a_homing) bis Homing-Schalter
  → reference: machine := 0 auf der aktiven Achse
  → Retract auf (+R, −R, −R) mit v_homing / a_homing
→ isHomed = true
```

Approach-Ziel je Achse: `home_axis − seek_sign · homing_approach_distance`
(X seek − → Approach bei `+R + d`; Y/Z seek + → Approach bei `−R − d`).
Bereits auf/jenseits der Approach-Pose → direkt langsame Suche.
Nach Power-On / nur `CmdRestoreMcs` / Safety-Fault / HomingError → Session-Trust false → Blind-Suche.
Soft-STOP invalidiert nur den Prozedur-Start (`NeedsHoming`), nicht die Schnellanfahrt.
`DefineMcs` nutzt die Schnellanfahrt nie.

Defaults: `v_homing_fast = 18 mm/s`, `a_homing_fast = 500 mm/s²`,
`homing_approach_distance = 50 mm`, `v_homing = 5 mm/s`
(in den Parametereinstellungen editierbar).

`probe_center`, `plate_origin`, `tool_offset` bleiben unverändert.
`home_offset` folgt dem aktuellen R (bereits bei `trySetMachineConfig` nachgezogen).

### 3. Restore nach Neustart — `restoreMachineCoordinateSystem()` / `CmdRestoreMcs`

Setzt `mcs_valid` **ohne Bewegung** und **ohne** Geometrie zu invalidieren.
Das Wire-Payload xyz wird **nicht** als Ursprung verwendet; Home wird aus dem aktuellen R abgeleitet.
`reference_offset` wird **nicht** persistiert; er entsteht wieder beim nächsten Reference-Home.

## Persistenz

Host speichert in `calibration.json`:

- `machineCoordinateSystem.valid`
- `machineCoordinateSystem.origin = "xMinHomeYzMaxHome"`
- `machineCoordinateSystem.homePosition` (Snapshot `(+R,-R,-R)`, nicht die MCS-Definition)
- plate / probe / tool (+ Valid-Flags)

Connect-Reihenfolge: Restore MCS → ggf. Plate/Probe push.

Legacy ohne `origin`, Identity-MCS oder `origin = "positiveMaxSwitches"` → `mcs_valid = false`. MCS und Geometrie neu kalibrieren.

## Simulation

- Gantry-TCP / Plant: **raw** (kontinuierlich, kein Teleport am Schalter).
- Statuszahlen: **Machine** (Home ≈ `(+2,-2,-2)`; X-Anschlag ≈ 0, Y/Z-Anschlag ≈ 0).
- Overlay: Machine + `reference_offset` → Raw-Szene.
- Plant: `limitXMin` bei `raw <= x_min` (X-Homing); `limit*Max` bei Y/Z-Homing.
- StatusFlag `McsValid` (Bit 6).
