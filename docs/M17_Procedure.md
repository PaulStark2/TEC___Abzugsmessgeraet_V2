# M17 — Automated test / aging procedure

This document tracks the Core + M12 surface for the automated procedure.
GUI work consumes only fields that exist here. Do not invent byte offsets.

STM32 firmware hosting is **M18** (renumbered; previously M17).

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
| `0x50` | `CmdSetMachineConfig` | 126 B `MachineConfigPayload` |
| `0x51` | `CmdGetMachineConfig` | empty; reply `MsgMachineConfig` (`0xA3`, 126 B) |
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
Successful homing snaps the pose to `CalibrationSet.homeOffset` (from MachineConfig).

## M17.5 — ProbeCalibration (placeholder)

`core/ProbeCalibration.hpp` is the only probe protocol surface used by
`ProcedureController`. The current body is a **placeholder** (+X search, trigger or
AtTarget writes `ProbeCenter`). Historical multi-touch probing is not ported.

`CmdStartProbeCalib` (`0x54`, empty) starts this placeholder via
`TestExecutor::startProbeCalibration()`.

## M17.6 — ProcedureController

`MachineState` stays the operating/safety SM. Process steps live in `ProcedurePhase`
and are owned by **one** sequencer: `ProcedureController`. `TestExecutor` is a facade.

`CmdStart` runs the full cycle (not simple point-to-point hopping).

PAUSE is rejected (`InvalidState`) during Clamp / InsertInitial / OptimizeInsert /
TestOrAge / Unclamp. STOP always aborts to `Aborted`.

## M17.7–M17.9 — Insert, optimize, test/aging

- PreClamp: Fx/Fy hysteresis only (`Machine::setPreClampXyEnabled`).
- Phase 1: constant-velocity Z− until `Fz >= targetForce` (or search end / Z-min).
  `ExtractionPoint = InsertionPoint + insertionBuffer` (Z+).
- Phase 2: discrete `ΔZ = gain * (Fz - target)` between attempts. No PID. Abort after
  `maxOptimizationIterations`.
- Phase 3 Aging: extract ↔ insert, `iterations` times, no force control.
- Phase 3 Test: same motion; `ForcePathRecorder` ring (256) of `(z, fz)`. Overflow drops
  oldest and increments `dropped`. Never blocks `tick()`.

## M17.10 — Force-path stream and StatusReport

| ID | Name | Payload |
|----|------|---------|
| `0x54` | `CmdStartProbeCalib` | empty |
| `0xA2` | `MsgForcePathChunk` | header 12 B + `sampleCount * 8` B; max 32 samples |

Chunk fields: `pointIndex u8`, `iteration u16`, `sequence u32`, `sampleCount u8`,
`dropped u32`, then `(z f32, fz f32) * sampleCount`.

StatusReport is **39 bytes**: previous M16 layout plus trailing `procedurePhase u8`.

The controller streams chunks opportunistically. If the Raspberry Pi lags, samples may
be dropped; the realtime loop does not wait.

## M17.11 — GUI

- Menü **Parametereinstellungen** (Passwort, Standard `tec-admin` bzw. `PARAMETER_MENU_PASSWORD`).
- Katalog + Infofelder in `gui/server/config/parameterCatalog.js` (nicht im Core hardcodiert).
- Persistenz: `gui/server/data/machine-config.json` (Pi). Core hält die aktive Kopie nur im RAM.
- Beim Protocol-Connect wird die gespeicherte Config per `CmdSetMachineConfig` übertragen.
- TestConfiguration und PlateOrigin werden vor `CmdStart` gesendet.
- Messtaster-Button sendet `CmdStartProbeCalib` (Platzhalter).
- Kraft-Weg-Kurven aus `MsgForcePathChunk` in den Ergebnissen.

Kein EEPROM/Flash auf dem STM32 in M17. Keine STM32-HAL, kein ADC-Treiber.


