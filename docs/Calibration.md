# Kalibrierung (MCS, Bauplatte, Messtaster)

## GUI-Funktionen unter „Kalibrieren“

| Funktion | Semantik |
|----------|----------|
| **Geometrische Kalibrierung** | Eigenes Untermenü: MCS und Bauplatte/Messtaster neu, nur Bauplatte/Messtaster bei bestehendem MCS, oder **Kalibrierkennwerte** |
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
| **Probe Calibration** | M19: Install setzt dauerhaftes `probeCenter` + `toolOffset=0`; Procedure-OffsetOnly aktualisiert nur `toolOffset` |

## Bauplatten-Kalibrierung (Bohrungsantastung)

Nicht mehr: Jog → sofort speichern. Stattdessen:

1. Grob in die Bohrung joggen und eintauchen.
2. `CmdStartPlateBoreSearch` mit Punkt-ID (`1=KM`, `2=K1`, `3=K2`, `4=K3`).
3. Core führt dieselbe Zwei-Sehnen-Mittelpunktsuche wie PreClamp aus (`core/BoreSearch`), danach Bodenantastung −Z und Lift (`plate_z_lift_mm`, Default 2 mm).
4. GUI: „Kalibrierpunkt ausgerichtet! Soll der Kalibrierpunkt gespeichert werden?“ → Ja / Nein / Wiederholen.
5. Bei Ja für KM: `CmdSetPlateOrigin` mit dem Suchergebnis (nicht der Roh-Jog-Pose).

Suchparameter liegen in `MachineConfig.plate_calib` und sind **unabhängig** von den PreClamp-`force.*`-Parametern (Parametermenü „Bauplatten-Bohrungskalibrierung“).

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

## Invariante

> Einmal definiertes MCS (Ursprung an den +Max-Anschlägen) bleibt bei normalem Homing und bei Änderung von R unverändert. Nur die explizite Full-Kalibrierung darf das MCS neu definieren.
> `CmdHome` ohne gültiges MCS wird abgelehnt; die GUI führt zur Full Calibration (kein generischer Core-Reject).

## Protocol

| Command | ID | Wirkung |
|---------|-----|---------|
| `CmdHome` | `0x15` | Reference Home (Rejected ohne MCS) |
| `CmdSetPlateOrigin` | `0x53` | KM speichern (xyz) |
| `CmdStartProbeCalib` | `0x54` | Messtaster-Install |
| `CmdDefineMcs` | `0x56` | MCS neu definieren (nur Core bis Commit) |
| `CmdRestoreMcs` | `0x57` | Persistiertes MCS laden (xyz ignoriert; Home aus R) |
| `CmdStartPlateBoreSearch` | `0x5D` | Bohrungssuche + Floor; Payload u8 Punkt-ID |
| `CmdSetPlateYaw` | `0x5E` | `plate_yaw_rad` setzen |
| `MsgBoreCenter` | `0xA6` | Ergebnis (inkl. `cz`, `point_id`) |

## Connect

```text
load calibration.json → CmdRestoreMcs → SetPlate → SetPlateYaw → SetProbe (wenn gültig)
```

Legacy-Dateien ohne MCS-Block oder ohne `origin=positiveMaxSwitches`: `mcs_valid = false` (kein Identity-MCS).
Fehlendes `plateYawRad` → 0 (Translation-only, wie früher).

## Hand-Jog (Kalibrier-Wizard)

Bauplatte-/Messtaster-Schritte nutzen dieselbe Jog-UI wie der Handmodus
(`jogPanel` → `jogControls`): Einzelschritt / Dauerhalt und eintippbare
Schrittweite mit Raster `jog_min_step_mm` (Host-Parameter, siehe `gui/README.md` G7).

## Messtaster / Probe Calibration am STM32

Das Einmessprotokoll (`CmdStartProbeCalib` / FullCalib) braucht denselben
Host-Vertrag wie die Simulation — siehe
[M19_ProbeCalibration.md](M19_ProbeCalibration.md) Abschnitt **Host contract**.

Kurz:

- Hardware: **NC**, Ruhe High, Kontakt Low an `PA5`
- GPIO-Low allein reicht **nicht**: Firmware muss an der Kontaktflanke
  Maschinenpose + `probeTriggerSequence` in `HostInputs` legen
  (`FirmwareHost`, 1 kHz). Fehlt das, „erkennt“ Core den Taster nicht.
- Details und Nucleo-LD2-Hinweis: [firmware/README.md](../firmware/README.md)
- Wenn die Achsen „fertig“ wirken, die GUI aber **abgebrochen** meldet: oft
  **Kreis-Fit** am Ende (Radius ≠ Soll) oder **CommunicationTimeout** — siehe
  Failure-modes in [M19_ProbeCalibration.md](M19_ProbeCalibration.md); Statusleiste
  `Fault` / Phase beachten.
