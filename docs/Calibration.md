# Kalibrierung (MCS, Bauplatte, Einmessbohrung)

## GUI-Funktionen unter „Kalibrieren“

| Funktion | Semantik |
|----------|----------|
| **Geometrische Kalibrierung** | Eigenes Untermenü: MCS und Bauplatte/Einmessbohrung neu, nur Bauplatte/Einmessbohrung bei bestehendem MCS, oder **Kalibrierkennwerte** |
| **Kalibrierkennwerte (Geometrie)** | Anzeige der gespeicherten MCS-/Plate-/Yaw-/K1–K3-/Probe-/ToolOffset-Posen plus Live-TCP zum Abgleich |
| **Kraftkalibrierung der Kraftmessdose** | Eigenes Untermenü: Kalibrierprozess starten oder Kalibrierkennwerte |
| **Kalibrierprozess starten** | Z-Regression, siehe [Force_Calibration.md](Force_Calibration.md) |
| **Kalibrierkennwerte (Kraft)** | Gespeicherte Kennlinie, Gleichung und Auswahlbedingungen, ohne Aktorsteuerung |

## Define / Commit / Abort

```text
CmdDefineMcs          → nur Core (Disk unverändert)
persistMcs / Commit   → neues MCS auf Disk, alte Geometrie verworfen
Abort vor Commit      → Disk unverändert; Core ← pushCommittedCalibration()
Abort nach Commit     → neues MCS bleibt; Geometrie unvollständig
```

## Begriffe

| Begriff | Bedeutung |
|---------|-----------|
| **MCS definieren** | +Max-Anschläge als Ursprung (0,0,0); Home = (−R,−R,−R); invalidiert Plate/Probe/Yaw **im Core** |
| **MCS committen** | Persistenz von `valid` / `origin=positiveMaxSwitches` / abgeleitetem `homePosition` |
| **Reference Homing** | Nur Pose an bestehendes MCS binden (Schalter = 0); nie MCS neu |
| **KM** | Kalibrierpunkt Bauplattenmitte = `plateOrigin` nach Bohrungsantastung |
| **K1 / K2 / K3** | Optionale Rotationskalibrierung: L mit 55 mm Kanten, Winkel bei K2 |
| **plateYaw** | Z-Drehung des Plattenrahmens relativ zur Maschine |
| **Probe Calibration** | M19: Install setzt dauerhaftes `probeCenter` + `toolOffset=0`; Procedure-OffsetOnly aktualisiert nur `toolOffset` (kraftbasierte Einmess-Bohrung, siehe [M19_ProbeCalibration.md](M19_ProbeCalibration.md)) |

## Bauplatten-Kalibrierung (Einmessprotokoll)

Nicht mehr: Jog → sofort speichern / eintauchen + XY-first-Suche. Stattdessen:

1. Optional: **Alten Kalibrierpunkt anfahren** (`CmdApproachCalibPoint`) — Raise auf Safe-Z-Calib → XY → Lower auf `target.z + approach_distance` (siehe [Safe_Z.md](Safe_Z.md)).
2. Position ggf. per Jog nachkorrigieren (Hub **über** der Bohrung, nicht eingetaucht).
3. **Einmessprotokoll starten** → `CmdStartPlateBoreSearch` mit Punkt-ID (`1=KM`, `2=K1`, `3=K2`, `4=K3`).
4. Core: Spreizer offen → Ruheprobe → `BoreSearch` mit **`z_first`** und Parametern aus `MachineConfig.probe` (Floor → Lift → XY Zwei-Sehnen). Ergebnis-Z = Bodenkontakt.
5. GUI: „Kalibrierpunkt ausgerichtet! Soll der Kalibrierpunkt gespeichert werden?“ → Ja / Nein / Wiederholen.
6. Bei Ja für KM: `CmdSetPlateOrigin` mit dem Suchergebnis (nicht der Roh-Jog-Pose).

PreClamp bleibt bei eigenen `force.*`-Parametern und XY-only (kein Floor).

### Rotationskalibrierung K1–K2–K3

Optional (`rotation_calib_enabled`, GUI-only). Konvention in Plattenkoordinaten:

```text
K2 → K1 = (0, +55 mm)   // parallel zur Platten-+Y
K2 → K3 = (+55 mm, 0)   // parallel zur Platten-+X
```

Nach Erfassung aller drei Punkte wertet die GUI Längen, Yaw, Rechtwinkligkeit und Kanten-Inkonsistenz aus (`gui/server/coord/plateRotation.js`, Spiegel `core/PlateRotation.hpp`):

```text
θ_x = atan2(V23.y, V23.x)      // K2→K3 vs. Maschinen-+X
θ_y = atan2(−V12.x, V12.y)     // K2→K1 vs. Maschinen-+Y (CCW)
θ   = 0.5 · (θ_x + θ_y)
```

Mehrstufige Toleranzen (Warn / Reject / Yaw-Ignore / Yaw-Apply) sind im Parametermenü und am Auswertungsdialog editierbar.

```text
machine.xy = plateOrigin.xy + Rz(plateYaw) · platePoint.xy + toolOffset.xy
machine.z  = plateOrigin.z  + platePoint.z + toolOffset.z
```

Z-Differenzen zwischen den Punkten dienen nur als Verkippungs-Hinweis (kein Rx/Ry).

## Einmessbohrung (Probe Calibration)

Kraftbasierte Bohrungssuche (`BoreSearch` z_first), kein Messtaster. Details:
[M19_ProbeCalibration.md](M19_ProbeCalibration.md).

1. Spreizer **offen** (`setSpreaderExtend` vor der Suche) — gilt für geometrische Kalibrierung und Procedure-OffsetOnly.
2. Hub oberhalb der Einmessbohrung: optional `CmdApproachCalibPoint` (`point_id=5`) bei gültigem `probeCenter`, sonst Jog; Automatic weiterhin ProbeApproach (Program-Safe-Z).
3. Floor → Lift → XY Zwei-Sehnen → Mittelpunkt.
4. `CmdStartProbeCalib` (`0x54`) = **Install** (`probeCenter` + `toolOffset=0`).

Parameter: `MachineConfig.probe` — gemeinsam mit Plate-Teach; unabhängig von PreClamp `force.*`.

## Invariante

> Einmal definiertes MCS (Ursprung an den +Max-Anschlägen) bleibt bei normalem Homing und bei Änderung von R unverändert. Nur die explizite Full-Kalibrierung darf das MCS neu definieren.
> `CmdHome` ohne gültiges MCS wird abgelehnt; die GUI führt zur Full Calibration (kein generischer Core-Reject).

## Protocol

| Command | ID | Wirkung |
|---------|-----|---------|
| `CmdHome` | `0x15` | Reference Home (Rejected ohne MCS) |
| `CmdSetPlateOrigin` | `0x53` | KM speichern (xyz) |
| `CmdStartProbeCalib` | `0x54` | Einmessbohrung-Install |
| `CmdDefineMcs` | `0x56` | MCS neu definieren (nur Core bis Commit) |
| `CmdRestoreMcs` | `0x57` | Persistiertes MCS laden (xyz ignoriert; Home aus R) |
| `CmdStartPlateBoreSearch` | `0x5D` | Plate-Teach Einmessprotokoll (`z_first`); Payload u8 Punkt-ID |
| `CmdSetPlateYaw` | `0x5E` | `plate_yaw_rad` setzen |
| `CmdApproachCalibPoint` | `0x61` | Alten Punkt anfahren; u8 `1=KM`…`4=K3`, `5=probe` |
| `CmdSetRotationPoints` | `0x62` | K1–K3 in Core (9×f32); Connect-Push |
| `MsgBoreCenter` | `0xA6` | Ergebnis (inkl. `cz`, `point_id`) |

## Connect

```text
load calibration.json → CmdRestoreMcs → SetPlate → SetPlateYaw
  → SetRotationPoints (wenn gültig) → SetProbe (wenn gültig)
```

Legacy-Dateien ohne MCS-Block oder ohne `origin=positiveMaxSwitches`: `mcs_valid = false` (kein Identity-MCS).
Fehlendes `plateYawRad` → 0 (Translation-only, wie früher).

## Hand-Jog (Kalibrier-Wizard)

Bauplatte-/Einmessbohrung-Schritte nutzen dieselbe Jog-UI wie der Handmodus
(`jogPanel` → `jogControls`): Einzelschritt / Dauerhalt und eintippbare
Schrittweite mit Raster `jog_min_step_mm` (Host-Parameter, siehe `gui/README.md` G7).

## Soft-STOP / Abbruch im Teach

Standalone Install und Plate-Bore-Teach behalten `Homed` — Wiederholen ohne
erneutes Homing. Failure-modes: [M19_ProbeCalibration.md](M19_ProbeCalibration.md).
Core Einmessen nutzt keinen Messtaster-GPIO (`PA5` / `probeTriggered`).
