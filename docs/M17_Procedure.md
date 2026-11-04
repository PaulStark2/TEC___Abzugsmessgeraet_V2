# M17 — Automated test / aging procedure

This document tracks the Core + M12 surface for the automated procedure.
GUI work consumes only fields that exist here. Do not invent byte offsets.

STM32 firmware hosting follows after M19.0. M18 is Core/Safety hardening
([`docs/M18_Procedure.md`](M18_Procedure.md)). Probe calibration is documented in
[`docs/M19_ProbeCalibration.md`](M19_ProbeCalibration.md).

## M17.1 — Configuration types

Hard limits live in `core/SafetyLimits.hpp` (`constexpr`). They are **not** part of
any M12 config payload and cannot be changed from the GUI.

Writable parameters:

| Type | File | Role |
|------|------|------|
| `MachineConfig` | `core/MachineConfig.hpp` | Motion (incl. safe_z_calib_buffer), force (incl. anomaly params), collision, spreader, probe, home/tool offset |
| `TestConfiguration` | `core/TestConfiguration.hpp` | `Test` / `Aging`, `targetForce`, `iterations`, `activePointIndex` |
| `CalibrationSet` | `core/CalibrationSet.hpp` | Taught `plateOrigin`, `probeCenter`, `toolOffset`, `homeOffset` |

Invalid incoming values are **rejected** (`ConfigValidationError`); the previous valid
copy is kept. There is no silent clamp.

Z kinematics (fixed, not a GUI parameter): **Z+ up**, **Z− toward specimen**.

## M17.2 — M12 config commands

Communication stays Core-free. Core mapping lives in
`simulation_bridge/ConfigMapping.hpp`.

| ID | Name | Payload |
|----|------|---------|
| `0x50` | `CmdSetMachineConfig` | 196 B `MachineConfigPayload` (M19.0 geometry correction; was 184 B, 142 B in M18, 126 B in M17) |
| `0x51` | `CmdGetMachineConfig` | empty; reply `MsgMachineConfig` (`0xA3`, 196 B) |
| `0x52` | `CmdSetTestConfiguration` | 8 B: mode u8, targetForce f32, iterations u16, activePointIndex u8 |
| `0x53` | `CmdSetPlateOrigin` | 12 B `x,y,z` machine pose |

Values that fail `validateMachineConfig` / `validateTestConfiguration` are NACKed
(`InvalidPayload`, `detail` = `ConfigValidationError`). Previous RAM copy kept.

Use JS helpers in `gui/server/protocol/m12Codec.js` (`encodeMachineConfigPayload`, …).
Do not hard-code offsets in the HMI.

## M17.3 — Force watchdog and spreader timer

`evaluateForceWatchdog()` runs in `Machine::tick()` on **raw** `HostInputs.force` (Newton),
before motion. Order: `SafetyLimits` hard magnitude → `MachineConfig` threshold for the
active `CollisionContext`. Trip → latched `Fault`.

`SpreaderTimer`: valve command is immediate; `spreaderExtended` follows
`spreaderExtendTime` / `spreaderRetractTime`. Fixture remains an instant stub.

## M17.4 — Frames

`machine = plateOrigin + platePoint + toolOffset` (`core/CoordinateTransform.hpp`).
`TestExecutor::start()` and `CmdStart` require a valid `plateOrigin`.
Successful homing re-references each axis at the +max switch (`referenceMachinePose` to
machine 0, no raw teleport) then retracts to the derived home (−R). See
[`docs/Homing.md`](Homing.md).

## M17.5 — ProbeCalibration

Historical note: M17 shipped a +X placeholder. **M19.0** replaces it with the
full Z + four-point protocol. See [`docs/M19_ProbeCalibration.md`](M19_ProbeCalibration.md).

`CmdStartProbeCalib` (`0x54`, empty) starts the **manual** protocol via
`TestExecutor::startProbeCalibration()` (no ProbeApproach). `CmdStart` runs
ProbeApproach then the same protocol before MoveSafePose.

`CmdStartTestOrAge` (`0x63`, empty) starts a **hand-mode** SeatToForce + TestOrAge
loop from the current pose via `TestExecutor::startTestOrAgeManual()` (no program,
no PreClamp). Send `CmdSetTestConfiguration` first. Abort/complete keep Homed.

## M17.6 — ProcedureController

`MachineState` stays the operating/safety SM. Process steps live in `ProcedurePhase`
and are owned by **one** sequencer: `ProcedureController`. `TestExecutor` is a facade.

`CmdStart` runs the full cycle (not simple point-to-point hopping).

PAUSE is rejected (`InvalidState`) during Clamp / InsertInitial / TestOrAge /
Unclamp / ProbeRemeasure / ForceTare. STOP always aborts to `Aborted`.
Anomaly waits use internal `pauseHolding` plus `CmdProcedureDecision`.

## M17.7–M17.9 — Seat, test/aging, unclamp

Per point (after initial OffsetOnly at run start):

```text
PointApproach → PreClamp → Clamp → SeatToForce → Unclamp AfterSeat
  → ForceTare → PreClamp → Clamp → TestOrAge → Unclamp AfterTest → ProbeRemeasure → NextPoint
```

- PreClamp: bore-center search via `BoreSearch` (two chords). Twice per point.
- SeatToForce (`InsertInitial`): constant-velocity Z− until `Fz >= targetForce`, then Unclamp AfterSeat (open at seat, fixed retract, then ForceTare before second PreClamp). Second Clamp goes straight to TestOrAge (no post-Clamp tare).
- TestOrAge: extract arms on Zug (`Fz < -force.force_tolerance_N`), then kraftfrei (`|Fz| <= force_tolerance_N`); optional `extract_after_free_mm` +Z after kraftfrei at `extract_after_free_v` (0 mm = stop there). Insert returns that offset to kraftfrei at the same `extract_after_free_v`, then dives at `phase3_v_insert`. Then `iterations` of force-threshold insert/extract; Test mode records force-path; anomalies for coord drift, lateral force, Z travel limits.
- Unclamp AfterTest: if still above kraftfrei by `extract_after_free_mm`, return to kraftfrei at `extract_after_free_v`, then re-seat to target force, open, fine then coarse to Safe-Z.
- ProbeRemeasure: OffsetOnly hang check vs `tool_offset_change_tolerance_mm`.
- Phase 2 (`OptimizeInsert`) removed.

Coarse approach stand-off for ProbeApproach and PointApproach uses
`probe.approach_distance` (default 15 mm) above the target Z; fine PointApproach uses
`v_slow`.
## M17.10 — Force-path stream and StatusReport

| ID | Name | Payload |
|----|------|---------|
| `0x54` | `CmdStartProbeCalib` | empty |
| `0x63` | `CmdStartTestOrAge` | empty (hand-mode Seat + iterations) |
| `0xA2` | `MsgForcePathChunk` | header 13 B (incl. `leg`) + `sampleCount * 8` B; max 32 samples; see [`Measurement_Evaluation.md`](Measurement_Evaluation.md) |

Chunk fields: `pointIndex u8`, `iteration u16`, `sequence u32`, `sampleCount u8`,
`dropped u32`, then `(z f32, fz f32) * sampleCount`.

StatusReport is **41 bytes** as of M18 (M17 was 39 bytes plus trailing `procedurePhase`;
M18 adds `faultReason` and `spreaderState`). See [`docs/M18_Procedure.md`](M18_Procedure.md).

The controller streams chunks opportunistically. If the Raspberry Pi lags, samples may
be dropped; the realtime loop does not wait.

## M17.11 — GUI

- Menü **Parametereinstellungen** (Passwort, Standard `tec-admin` bzw. `PARAMETER_MENU_PASSWORD`).
- Katalog + Infofelder in `gui/server/config/parameterCatalog.js` (nicht im Core hardcodiert).
- Persistenz: `gui/server/data/machine-config.json` (Pi). Core hält die aktive Kopie nur im RAM.
- Host-only Felder (nicht im M12-Wire): z. B. `jog_min_step_mm` — bleiben auf Disk; Encode sendet nur Wire-Felder.
- Beim Protocol-Connect wird die gespeicherte Config per `CmdSetMachineConfig` übertragen.
- TestConfiguration und PlateOrigin werden vor `CmdStart` gesendet.
- Messtaster-Button sendet `CmdStartProbeCalib` (manuelle Einmessung, M19.0).
- Kraft-Weg-Auswertung aus `MsgForcePathChunk` in den Ergebnissen (Ein-/Ausgliedern, Spitzenstatistik, CSV/PDF): [`Measurement_Evaluation.md`](Measurement_Evaluation.md).

Kein EEPROM/Flash auf dem STM32 in M17. Keine STM32-HAL, kein ADC-Treiber.


