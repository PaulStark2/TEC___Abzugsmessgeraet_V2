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
| `MachineConfig` | `core/MachineConfig.hpp` | Motion, force, collision contexts, spreader times, phase-2, probe placeholders, home/tool offset |
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
Successful homing re-references each axis at the +max switch (`setCurrentPose` to
+retract R) then retracts to the machine home (axis = 0). See
[`docs/Homing.md`](Homing.md).

## M17.5 — ProbeCalibration

Historical note: M17 shipped a +X placeholder. **M19.0** replaces it with the
full Z + four-point protocol. See [`docs/M19_ProbeCalibration.md`](M19_ProbeCalibration.md).

`CmdStartProbeCalib` (`0x54`, empty) starts the **manual** protocol via
`TestExecutor::startProbeCalibration()` (no ProbeApproach). `CmdStart` runs
ProbeApproach then the same protocol before MoveSafePose.

## M17.6 — ProcedureController

`MachineState` stays the operating/safety SM. Process steps live in `ProcedurePhase`
and are owned by **one** sequencer: `ProcedureController`. `TestExecutor` is a facade.

`CmdStart` runs the full cycle (not simple point-to-point hopping).

PAUSE is rejected (`InvalidState`) during Clamp / InsertInitial / OptimizeInsert /
TestOrAge / Unclamp. STOP always aborts to `Aborted`.

## M17.7–M17.9 — Insert, optimize, test/aging

- PreClamp: Fx/Fy hysteresis only (`Machine::setPreClampXyEnabled`).
- Phase 1 (`InsertInitial`):
  1. If `|Fz| > force_tolerance_N`, first extract (Z+) until force-free
     (`|Fz| <= phase2.force_tolerance_N`), using `insert_search_distance` as search
     stroke.
  2. Then constant-velocity Z− until `Fz >= targetForce` (or search end / Z-min).
  3. `ExtractionPoint = InsertionPoint + insertionBuffer` (Z+, computed).
- Phase 2: discrete `ΔZ = gain * (Fz - target)` between attempts. No PID. Abort after
  `maxOptimizationIterations`.
- Phase 3 Aging: extract ↔ insert, `iterations` times, no force control.
- Phase 3 Test: same motion; `ForcePathRecorder` ring (256) of `(z, fz)`. Overflow drops
  oldest and increments `dropped`. Never blocks `tick()`.

Coarse approach stand-off for ProbeApproach and PointApproach uses
`probe.approach_distance` (default 15 mm) above the target Z; fine PointApproach uses
`v_slow`.
## M17.10 — Force-path stream and StatusReport

| ID | Name | Payload |
|----|------|---------|
| `0x54` | `CmdStartProbeCalib` | empty |
| `0xA2` | `MsgForcePathChunk` | header 12 B + `sampleCount * 8` B; max 32 samples |

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
- Kraft-Weg-Kurven aus `MsgForcePathChunk` in den Ergebnissen.

Kein EEPROM/Flash auf dem STM32 in M17. Keine STM32-HAL, kein ADC-Treiber.


