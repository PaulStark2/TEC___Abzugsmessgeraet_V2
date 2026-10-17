# Kalibrierung (MCS, Bauplatte, Messtaster)

## GUI-Funktionen unter „Kalibrieren“

| Funktion | Semantik |
|----------|----------|
| **Geometrische Kalibrierung** | Eigenes Untermenü: MCS und Bauplatte/Messtaster neu, nur Bauplatte/Messtaster bei bestehendem MCS, oder **Kalibrierkennwerte** |
| **Kalibrierkennwerte (Geometrie)** | Anzeige der gespeicherten MCS-/Plate-/Probe-/ToolOffset-Posen plus Live-TCP zum Abgleich |
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
| **MCS definieren** | +Max-Anschläge als Ursprung (0,0,0); Home = (−R,−R,−R); invalidiert Plate/Probe **im Core** |
| **MCS committen** | Persistenz von `valid` / `origin=positiveMaxSwitches` / abgeleitetem `homePosition` |
| **Reference Homing** | Nur Pose an bestehendes MCS binden (Schalter = 0); nie MCS neu |
| **Plate Calibration** | Teach aktueller Machine-Pose als `plateOrigin` |
| **Probe Calibration** | M19: Install setzt dauerhaftes `probeCenter` + `toolOffset=0`; Procedure-OffsetOnly aktualisiert nur `toolOffset` |

## Invariante

> Einmal definiertes MCS (Ursprung an den +Max-Anschlägen) bleibt bei normalem Homing und bei Änderung von R unverändert. Nur die explizite Full-Kalibrierung darf das MCS neu definieren.
> `CmdHome` ohne gültiges MCS wird abgelehnt; die GUI führt zur Full Calibration (kein generischer Core-Reject).

## Protocol

| Command | ID | Wirkung |
|---------|-----|---------|
| `CmdHome` | `0x15` | Reference Home (Rejected ohne MCS) |
| `CmdDefineMcs` | `0x56` | MCS neu definieren (nur Core bis Commit) |
| `CmdRestoreMcs` | `0x57` | Persistiertes MCS laden (xyz ignoriert; Home aus R) |

## Connect

```text
load calibration.json → CmdRestoreMcs → SetPlate/SetProbe (wenn gültig)
```

Legacy-Dateien ohne MCS-Block oder ohne `origin=positiveMaxSwitches`: `mcs_valid = false` (kein Identity-MCS).

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
