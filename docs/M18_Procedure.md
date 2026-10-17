von Agent1# M18 — Safety hardening, safe process motion, STM32 preparation

**M18 = noch keine Hardwarefreigabe.**

This milestone hardens the M17 procedure architecture. It does **not** replace Core
modules with a parallel stack. STM32 HAL, ADC, UART drivers, and real pneumatics
are **later than M19.0**.
([`docs/M19_ProbeCalibration.md`](M19_ProbeCalibration.md)), still without hardware
release.

```text
ProbeCalibration = M19.0 (3-axis protocol, no STM32 HAL)
STM32-HAL        = later milestone
```

The Raspberry Pi is **not** part of the real-time safety loop. Safety and motion
decisions live in Core (later on STM32). The Pi may be slow; Core never waits on it.

---

## Architecture

Unchanged layering:

```text
GUI (Node.js)  →  M12 frames  →  simulation_bridge / future STM32 host
                                      ↓
                              ProcedureController
                                      ↓
                         Machine / MotionPlanner / Axis
                                      ↓
                              HostInputs / HostOutputs
```

Core remains free of STM32 HAL, OS APIs, and Node.js. The host tick is
`Machine::tick(dt)` plus `ProcedureController::tick(dt)`. No file I/O, no network
wait, no heap-per-sample in the control path.

Logical process after M18:

```text
Homing → Safe-Z → XY to point → Z coarse → Ruheprobe → Z fine → PreClamp XY → Clamp
  → Ruheprobe → Phase 1 (constant-speed insert until Fz) → InsertionPoint / ExtractionPoint
  → Ruheprobe → Phase 2 (discrete Z correction)
  → Ruheprobe → Phase 3 (Test or Aging)
  → Ruheprobe → Unclamp → Safe-Z → next point → Safe-End
```

Jede Ruheprobe ist eine Aufnahme, nicht zwei an derselben Stelle. Details: `docs/Force_Calibration.md`.

---

## ProcedurePhase / MachineState

`ProcedurePhase` is the process SM (`ProcedureController`). `MachineState` is the
machine SM (`Machine`). They must not contradict after a safety event:

| Event | Motion | Process |
|-------|--------|---------|
| STOP / abort / force / comm timeout / E-Stop | `Machine::stop()` → Stopped or Fault | Aborted / Fault |
| Probe contact during ProbeApproach coarse Z | brake + `FaultReason::ProbeApproachContact` | Aborted |
| Successful run | Stopped at Safe-Z | Completed |

ProbeApproach Z descent is open-loop to `probe_center.z + approach_distance` but
**abort on Messtaster latch** (`ProbeApproachContact`). Details:
[`docs/M19_ProbeCalibration.md`](M19_ProbeCalibration.md).

There is **no automatic resume** after a safety fault. Heartbeat returning after a
timeout does not continue the process.

Pause remains forbidden during the actual test phases (M17 rule retained).

---

## Safety concept

| ID | Rule |
|----|------|
| S1 | No simultaneous long XY travel and Z descent to a test height. Approach is raise-Z → XY at Safe-Z → lower-Z. |
| S2 | Automatic start (`CmdStart`, probe calib start) requires `machine.isHomed()`. Homing itself and service `CmdMoveTo` do not. |
| S3 | Every abort path calls `machine.stop()` (unless already Fault). Axes must not keep moving. |
| S4 | Spreader has no end-position sensor. STOP/Fault freeze the last open/close command (`SafeDeenergized`). |
| S5 | Non-finite Fx/Fy/Fz → Fault. Stale force samples → Fault. |
| S6 | Core uses Newton only. The measuring range is a MachineConfig parameter, default ±30 N, typo brake 500 N. Until Phase 7 the STM32 uses a provisional linear map 0…4095 → ±`force_range_N`. The committed curve is `docs/Force_Calibration.md` (tension negative). |
| S7 | Host heartbeat watchdog while automatic process runs. Timeout → Fault + stop + spreader safe. |
| S8 | MachineConfig, TestConfiguration, PlateOrigin, Safe-Z, speeds, force params, manual spreader: NACK `InvalidState` while `ProcessState::Running`. |
| S9 | Phase 1 succeeds only if `Fz >= targetForce`. Search end / AtTarget / zMinBlocked is abort, not InsertionPoint. |
| S10 | XY force correction only in `PreClampXyCorrect`. Then off. Timeout / max attempts → abort. |
| S11 | Phase 2 is discrete `dZ = gain * (Fz - target)` with deadband, max step, iteration cap, sign-flip / depth limits. Abort → `stop()`. |
| S12 | Test mode records per-iteration force-path. Aging repeats insert/extract; force is safety only. |
| S13 | ForcePathRecorder: O(1), no heap per sample, overflow + drop counter, iteration identity. |
| S14 | ProbeCalibration placeholder: no Complete without `probeTriggered`. |

---

## Safe-Z

Dynamic height from the loaded test program (plate frame):

```text
safe_z_plate = max(program.point.z) + safe_z_buffer
safe_z_machine = plate_origin.z + safe_z_plate + tool_offset.z
```

- `MachineConfig.motion.safe_z_buffer` (default **30 mm**) is the only configurable clearance.
- There is **no** absolute machine-frame `safe_z` parameter.
- Without a loaded program, Safe-Z cannot be computed and automatic start is refused.
- Static: `start()` rejects unless every transformed machine-Z is **strictly `< safe_z_machine`** (true by construction when `safe_z_buffer > 0`).
- Runtime: XY traverse commands only while `atSafeZ()` (at `safe_z_machine`).
- Reusable legs: `MoveToSafeZ` → `MoveXY` (at Safe-Z) → `MoveToTargetZ`.
- Safe-End: XY to measured `home_offset.x/y`, **remain at Safe-Z** (do not dive to `home_offset.z`).

Buffer range: `0 < safe_z_buffer ≤ SafetyLimits::kMaxAbsSafeZ` (500).

See also `core/SafeZ.hpp` and `docs/Safe_Z.md`.

---

## Homing requirement

Automatic process start is refused when `!machine.isHomed()` (`InvalidState` on the
wire). After `home()` begins, `isHomed` is cleared until the Z+→X+→Y+ sequence
succeeds. Losing homing forbids automatic start again.

Service jogging (`CmdMoveTo`) remains available without homing. Homing does not
require a prior homed state.

---

## Force watchdog

Order in `Machine::tick()` (before motion):

1. Finite check (`std::isfinite` on Fx, Fy, Fz) **before** `fabs`.
2. `forceSampleValid == false` → `ForceInvalidSample`.
3. Unchanged `forceSampleSequence` ages `forceSampleAge`. Age ≥ `sample_timeout_s` → `ForceStale`.
4. Magnitude vs SafetyLimits / collision context.

NaN/Inf cannot pass through `fabs(NaN) >= limit`.

Host interface (STM32 later, not Core ADC):

```text
forceSampleValid
forceSampleSequence   // unchanged sequence ages
hostHeartbeatValid
hostHeartbeatSequence
probeTriggered
limit switches, e_stop
```

Default `sample_timeout_s = 0.5` (0.05…5 s).

### ADC scaling (host)

Committed contract: [`docs/Force_Calibration.md`](Force_Calibration.md)
(`F = β0 + β1·adc + β2·adc²`, tension negative). STM32 Phase 2.1 provisional map
until that curve is loaded:

```text
F[N] = -R + (counts / 4095) * 2R
R = MachineConfig.force.range_N   (default 30 N)
ADC 0 counts     → -R
ADC 4095         → +R
```

---

## Communication watchdog

Active only while an automatic process runs (`setCommunicationWatchdogEnabled`).

- Host increments `hostHeartbeatSequence` on a live heartbeat.
- Age ≥ `heartbeat_timeout_s` (default 0.3 s, range 0.05…2 s) → `FaultReason::CommunicationTimeout`.
- Motion stops, spreader enters SafeDeenergized (last open/close command frozen).
- A later heartbeat does **not** auto-resume.

Offline / manual operation can leave the watchdog disabled.

This timeout is a MachineConfig parameter (GUI-editable under Core limits). A later
safety-case split of “GUI timeout” vs “certified timeout” is not implemented in M18.

---

## Spreader safe state

No end-position feedback. Timing (`spreaderExtend` for `extend_time_s` /
`retract_time_s`) is unchanged for Clamp/Unclamp.

Core still has one command (`HostOutputs.spreaderExtend`). Hardware uses two
valves: STM32 **PC8 = open**, **PC12 = close**, always as a complementary pair.

| Mode | Electrical pair (STM32) | Latch `spreaderExtended` |
|------|-------------------------|--------------------------|
| Clamp | PC8 High, PC12 Low for extendTime | set true after timer |
| Unclamp | PC8 Low, PC12 High for retractTime | set false after timer |
| STOP / Fault / E-Stop | **freeze last pair** (`enterSafeState`) | **not rewritten** (unknown mechanical pose) |

`SpreaderState::SafeDeenergized` after safety. Hardware E-Stop still cuts air.
Do not treat `spreaderExtended` as a mechanical guarantee after a fault.

`CmdSetSpreader` during `ProcessState::Running` → `InvalidState`. Procedure Clamp still
uses `Machine::setSpreaderExtend` internally.

---

## Parameter structure

| Layer | Writable from GUI? |
|-------|--------------------|
| `SafetyLimits` | **No** (compile-time, Core rejects over-limit MachineConfig) |
| `MachineConfig` | Yes, when process not Running |
| `TestConfiguration` | Yes, when process not Running |
| Plate origin / calibration teach | Yes, when process not Running (`setProbeCenter` remains available during probe calib) |

GUI catalog ranges mirror `SafetyLimits` / `validateMachineConfig`. Invalid values are
rejected (no silent clamp). Unused catalog entries are marked **zukünftig**
(`force_filter`, `probe_backoff`).

Password for the parameter menu is GUI-only (`tec-admin` / `PARAMETER_MENU_PASSWORD`).
**Production password must be changed.** It is not STM32 safety.

---

## Coordinate systems

Prüfpunktdatei: `name, x, y, z` relative to the taught plate origin (axes parallel to
machine axes).

```text
machine = plateOrigin + platePoint + toolOffset
```

`toolOffset` default is `{0,0,0}`. It is an optional TCP/spreader offset, **not** a second
plate origin. Setting it changes the transformed machine pose on purpose; it must not be
used as a hidden substitute for plate teach.

---

## M12 extensions

Do not use byte offsets in the GUI. Use `gui/server/protocol/m12Codec.js`.

| Payload | Wire size |
|---------|-----------|
| `StatusReport` | **41 B** (was 39): + `fault_reason u8`, `spreader_state u8` |
| `MachineConfig` | **196 B** as of the M19.0 geometry correction (first M19.0 cut was 184 B, M18 was 142 B) |

Status flags (bit indices):

| Bit | Meaning |
|-----|---------|
| 0 | isHomed |
| 1 | limitWarning |
| 2 | communication watchdog enabled |
| 3 | force sample fresh (age < sample timeout) |
| 4 | plate origin valid |
| 5 | probe center valid |

`CmdStart` / `CmdStartProbeCalib` without homing → NACK `InvalidState`.
Config/spreader commands while Running → NACK `InvalidState`.

---

## ForcePath

Ring buffer, O(1) push, no heap per sample, no wait on Pi.

- Test mode: one logical curve per iteration (`sample.iteration`, `popSameIteration`).
- Aging: no recording (force still used for safety).
- Overflow: drop oldest, increment drop counter.
- Streaming is opportunistic; a slow Pi must not block the tick.

---

## ProbeCalibration interface

See [`docs/M19_ProbeCalibration.md`](M19_ProbeCalibration.md). M18 kept a
placeholder; M19.0 implements the non-blocking Z + ±X/±Y protocol.

Motion goes through Machine. The host latches pose at the trigger edge.
`Machine::consumeProbeTrigger()` consumes that event so a short pulse is not lost
between ticks. Search end without trigger → Error, **no** `setProbeCenter` /
`setToolOffset`.

---

## Host contract for later STM32 (not implemented in M19.0)

STM32 host shall supply: Fx/Fy/Fz (Newton), `forceSampleValid` / sequence, heartbeat,
`probeTriggered` plus latched trigger pose / sequence, limits, E-Stop.

STM32 host shall consume: stepper commands, spreader open/close pair, fixture, fault state.

Target: fixed tick, safety reaction in a few milliseconds. Core M18 has no blocking
operations on that path.

---

## Known hardware delimitations

- No STM32 HAL, ADC, GPIO, timer, UART, Flash/EEPROM.
- No real stepper pulse generation.
- No real E-Stop hardware loop.
- No real pneumatics / dual-valve retract.
- No real probe calibration protocol.
- Spreader mechanical pose after SafeDeenergized is **unknown** (GPIO pair frozen).
- SimPlant force remains 0; complete-run tests inject tensile-positive Fz.

**Hardwarefreigabe: NEIN.**
