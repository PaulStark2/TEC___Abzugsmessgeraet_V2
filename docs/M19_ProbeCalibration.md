# M19 — Probe calibration (kraftbasierte Einmess-Bohrung)

> **Archive note:** The historical ABB / Messtaster touch-probe protocol
> (disc geometry, dual trigger, circle-fit, GPIO latch) is **obsolete**.
> This document describes the as-built **bore replacement**: Core measures
> `probe_center` via force-based `BoreSearch` (Z-first) with the Spreizer open.
> GPIO `PA5` / `probeTriggered` is unused by Core Einmessen.

## Goal

Measure the geometric mid-point of the **Einmess-Bohrung** and store it per mode:

```text
Install (FullCalib / Geometry, CmdStartProbeCalib):
  probeCenter = measured
  toolOffset  = (0,0,0)

OffsetOnly (Procedure after ProbeApproach):
  probeCenter unchanged (durable plant reference)
  toolOffset  = storedProbeCenter − measured
```

Motion contract (unchanged):

```text
machine.xy = plateOrigin.xy + Rz(plateYaw) * platePoint.xy + toolOffset.xy
machine.z  = plateOrigin.z  + platePoint.z + toolOffset.z
```

`plateYaw` defaults to 0. Plate teach (KM / optional K1–K3) uses the **same**
`z_first` Einmess protocol and `MachineConfig.probe` params, reporting via
`MsgBoreCenter` instead of writing `probeCenter`; see [Calibration.md](Calibration.md).

The offset is **replaced** on OffsetOnly, never accumulated. Failed, aborted or
STOPped runs leave the previous valid `probe_center` / `tool_offset` unchanged.

On successful OffsetOnly the GUI compares `|new − old|` (3D) to
`tool_offset_change_tolerance_mm` (default 0.5 mm, Parametermenü, GUI-only).
If the delta exceeds the tolerance, a non-blocking **Werkzeugoffset-Anomalie**
is shown; the new offset is still applied. Install clears the anomaly flag.

## Geometry / search

No Messtaster disc, no circle-fit, no `ProbeSearch` trigger latch.

Core uses `ProbeCalibration` → `BoreSearch` with **`z_first = true`**:

```text
Hub P = TCP at protocol start (jogged, or after ProbeApproach), above the bore
  → Floor (−Z until floor_force or z_search_limit / timeout)
  → Lift (+Z by z_lift_mm)
  → XY two-chord rising-edge wall search (same algorithm as PreClamp / plate teach)
  → center (cx, cy, cz) → Install or OffsetOnly store
```

- Machine frame: X/Y in the plane, **Z+ up**.
- Collision context during search: `Contact` (intentional wall/floor forces).
- Search profile: `probe.v_search` with `motion.a_testpoints` / `corner_testpoints`.
- Transfer legs remain one axis at a time (Safe-Z / ProbeApproach).

## Spreizer

The Spreizer **must be open** before and during Einmessen (tool damage risk).
`ProcedureController` calls `Machine::setSpreaderExtend(true)` and waits until
`spreaderExtended` before starting `ProbeCalibration` (manual Install and
automatic ProbeApproach / OffsetOnly).

## Operating modes

### Manual — `CmdStartProbeCalib` (0x54)

Operator jogs TCP to hub **P** above the bore. ProbeApproach is skipped.
Install works without a prior stored `probe_center`. On Complete → `Completed`.

### Automatic — `CmdStart` (0x13)

```text
ProbeApproach
  Safe-Z → XY to (probe_center − tool_offset) @ v_approach
  → coarse Z = target.z + approach_distance @ v_approach (open-loop)
  → OffsetOnly BoreSearch (hub = current TCP)
→ MoveSafePose → test / aging cycle
```

**ProbeApproach target (Formel 2):** `target = probe_center − tool_offset`
(last successful measured TCP in the bore). After Install, `tool_offset` is
zero, so the first automatic approach uses raw `probe_center`.

Coarse Z does **not** monitor a Messtaster. `FaultReason::ProbeApproachContact`
remains in the enum for legacy wire compatibility and is **unused** by Core.

Safe-Z from M18 is mandatory for XY traverse. No 3D point-to-point.

## Configuration (`MachineConfig::Probe`)

Writable via Parametereinstellungen / M12. Shared by Install / OffsetOnly and
plate teach. **Independent** of PreClamp `force.*` search params.

| Field | Default | Role |
|-------|---------|------|
| `v_approach` | 10 mm/s | ProbeApproach XY / coarse Z |
| `v_search` | 1 mm/s | Bore wall / floor search |
| `wall_force_N` | 0.5 N | XY rising-edge threshold |
| `floor_force_N` | 0.5 N | Z floor threshold (z_first) |
| `xy_search_limit` | 5 mm | Max wall travel from origin |
| `z_search_limit` | 10 mm | Max floor travel |
| `z_lift_mm` | 2 mm | Lift after floor before XY |
| `timeout_s` | 60 s | Whole search timeout |
| `refine_passes` | 0 | Extra full two-chord passes from found center |
| `approach_distance` | 15 mm | Coarse Z standoff above stored center |

`MachineConfig` wire size is **376** bytes (`point_z_clearance_mm` appended after the
previous 372-byte payload).
Codec: `communication/PayloadCodec.cpp`, `gui/server/protocol/m12Codec.js`;
constant `kMachineConfigPayloadWireSize` in `ProtocolSpec.hpp`.

Phase-1 insert depth remains `force.insert_search_distance` (not a probe param).

## Persistence

- Runtime source of truth: `CalibrationSet` in Core RAM.
- `tool_offset` is also written into `MachineConfig.calibration.tool_offset` so
  `CmdGetMachineConfig` returns the replaced value.
- After successful **Install** the GUI `POST /api/calibration/sync` with
  `{ persistProbeCenter: true }` stores `probe_center_*` and `tool_offset = 0`.
- After **OffsetOnly** sync with `persistProbeCenter: false` updates only
  `tool_offset_*` and must not rewrite `probeCenter`.

## Failure modes

| Case | Result |
|------|--------|
| Bore timeout / wall not found / floor not found | `FaultReason::ProbeSearchNoContact`, abort, previous calib kept |
| STOP / Abort / Fault during search | Abort, previous calib kept |
| OffsetOnly without valid `probe_center` | Start rejected / Error |
| Invalid config (NaN/Inf, out of SafetyLimits) | Rejected, previous config kept |
| Repeat successful calib | Offset replaced; no drift |

`ProbeApproachContact` is legacy and unused (no touch-probe on coarse Z).

### Teach abort / Soft-STOP retry

Standalone M19 (`CmdStartProbeCalib`) and plate bore teach aborts keep
`HomingState::Homed` and recover to `Idle` via a no-op `moveTo`. Soft-STOP during
teach does **not** require re-homing before „Wiederholen“. Automatic procedure
aborts still call `invalidateHoming()` as before.

## Known limits

- No Rx/Ry, no automatic Spreizer-state sensing (commanded extend only).
- Core Einmessen does not use GPIO `PA5` / `HostInputs.probeTriggered`.
- Hardware bench validation continues under firmware bring-up.
