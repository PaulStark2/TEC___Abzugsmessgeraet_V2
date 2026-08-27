# Kalibrierung (MCS, Bauplatte, Messtaster)

## GUI-Funktionen unter „Kalibrieren“

| Funktion | Semantik |
|----------|----------|
| **MCS und Bauplatte/Messtaster definieren und kalibrieren** | Full: `CmdDefineMcs` → Commit (`persistMcs`) → Plate teach → M19 |
| **Bauplatte/Messtaster kalibrieren** | Geometry: Reference `CmdHome` bei Bedarf → Plate → M19; MCS unverändert |
| **Kraftmessdose kalibrieren** | Noch nicht implementiert |

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
| **MCS definieren** | Endanschläge + Latch `home_offset`; invalidiert Plate/Probe **im Core** |
| **MCS committen** | Persistenz von `home_offset` / `mcs_valid` in `calibration.json` |
| **Reference Homing** | Nur Pose an bestehendes MCS binden; nie MCS neu |
| **Plate Calibration** | Teach aktueller Machine-Pose als `plateOrigin` |
| **Probe Calibration** | M19: Install setzt dauerhaftes `probeCenter` + `toolOffset=0`; Procedure-OffsetOnly aktualisiert nur `toolOffset` |

## Invariante

> Einmal definiertes MCS bleibt bei normalem Homing unverändert. Nur die explizite Full-Kalibrierung darf das MCS neu definieren.
> `CmdHome` ohne gültiges MCS wird abgelehnt; die GUI führt zur Full Calibration (kein generischer Core-Reject).

## Protocol

| Command | ID | Wirkung |
|---------|-----|---------|
| `CmdHome` | `0x15` | Reference Home (Rejected ohne MCS) |
| `CmdDefineMcs` | `0x56` | MCS neu definieren (nur Core bis Commit) |
| `CmdRestoreMcs` | `0x57` | Persistiertes MCS laden (xyz = homePosition) |

## Connect

```text
load calibration.json → CmdRestoreMcs → SetPlate/SetProbe (wenn gültig)
```

Legacy-Dateien ohne MCS-Block: `mcs_valid = false` (kein erfundenes `homePosition`).

## Hand-Jog (Kalibrier-Wizard)

Bauplatte-/Messtaster-Schritte nutzen dieselbe Jog-UI wie der Handmodus
(`jogPanel` → `jogControls`): Einzelschritt / Dauerhalt und eintippbare
Schrittweite mit Raster `jog_min_step_mm` (Host-Parameter, siehe `gui/README.md` G7).
