# M19.0 — Probe calibration on the 3-axis Core

**Hardwarefreigabe: NEIN.** STM32 HAL, GPIO, ADC, UART drivers and real stepper
pulses are not part of this milestone. STM32 hosting follows later.

This document is the as-built description of the historical ABB touch-probe
protocol after transfer onto the Cartesian Core. ABB RAPID, joint motion,
`tooldata` / `wobjdata`, Rx/Ry tilt and a second probe geometry are not ported.

## Goal

1. Measure the geometric center of the **one** measuring disc (SimPlant / hardware).
2. Store that center according to calibration mode:

```text
Install (FullCalib / Geometry, CmdStartProbeCalib):
  probeCenter = measured
  toolOffset  = (0,0,0)

OffsetOnly (Procedure after ProbeApproach):
  probeCenter unchanged (durable plant reference)
  toolOffset  = storedProbeCenter − measured
```

Equivalently for motion:

```text
machine = plateOrigin + platePoint + toolOffset
```

`probe_offset_reference` is **legacy** and must not drive Install/OffsetOnly math.

The offset is **replaced** on OffsetOnly runs. It is never accumulated.
Failed, aborted or STOPped runs leave the previous valid `probe_center` and
`tool_offset` unchanged.

## Geometry

Defaults match the real hardware envelope:

| Part | Default | Notes |
|------|---------|--------|
| Measuring disc | Ø 8 mm × 3 mm | `probe_center` is the geometric mid-plane center |
| Spreader | Ø 2.5 mm, cylindrical height 15 mm | TCP is the **bottom face**; the cylinder extends ~15 mm in **+Z** |

```text
          +Z
           ↑
           │  zylindrischer Spreizerbereich
           │  Ø tool_diameter (2.5 mm)
           │  tool_cylindrical_height (15 mm)
           │
      ┌───────────┐
      │           │
      └───────────┘  ← TCP / untere Stirnfläche
```

```text
        Messscheibe
      ┌─────────────┐
      │             │
      └─────────────┘
          Ø 8 mm
          Dicke 3 mm
          probe_center = geometrischer Mittelpunkt
```

- Machine frame: X/Y in the plane, **Z+ up**.
- `head_diameter` is kept in config (default 0 mm) and **unused**.
- Lateral contact is two circles in XY:

```text
r_contact = measuring_head_diameter/2 + tool_diameter/2
          = 4 mm + 1.25 mm = 5.25 mm
```

The latched TCP at trigger is **not** the disc center. Conversion uses the
search direction (see `core/ProbeGeometry.hpp`). Z contact: disc center Z is
`tcp.z - thickness/2`.

XY contact **may** occur on the cylindrical shaft, not on the TCP face.
That is required: the XY work height is intentionally below `probe_center.z`.

## XY work height (do not change)

After the Z measurement, the **measured** disc-center Z is stored for the XY
descend only. The hub pose is not rewritten. Each lateral search then uses:

```text
XY search Z = measured probe_center.z − search_depth
```

Defaults: `search_depth = 5 mm`. For `probe_center.z = 0` the XY plane is
**z = −5 mm**. This is intentional for the real spreader. It is **not** the
disc mid-plane.

The ~15 mm constant-diameter shaft starting at TCP is long enough to contact
the 3 mm disc while the TCP sits 5 mm below the center.

## XY start offset and search timeout

`search_distance` is the ABB-style extra XY **start** clearance from the **hub**,
not from a stored disc center:

```text
d_start = search_distance + R_probe + R_tool
Start_xy = Hub_xy + direction * d_start
```

It is **not** a search destination and **not** the Phase-1 insert depth.

After the Z measurement, each XY direction descends to

```text
z_xy_start = measured_center_z − search_depth
```

`search_depth` must leave the tool cylinder overlapping the disc
(`search_depth < tool_cylindrical_height − thickness/2`).

The search itself is **trigger-driven**: the axis moves toward the hub until the
probe fires or a timeout (time and/or remaining travel to the hub) expires.
The hub axis is a last-resort bound, not a planned contact pose.

No trigger before timeout:

1. Search → Error (`FaultReason::ProbeSearchNoContact`)
2. ProbeCalibration → Error
3. no new `probe_center` / `tool_offset` is stored
4. previous valid calibration remains
5. GUI shows a timeout / no-contact message

`probe_xy_search_margin` remains in config for compatibility. It no longer
defines the planmässige Suchende.

## Insert depth vs search_distance

Phase-1 insert Z-dive and the optimization depth floor use
`force.insert_search_distance` (default 10 mm). Changing
`probe_xy_search_margin` or `probe.search_distance` does not change the
insert/test motion.

## Trigger architecture

The STM32 (later) or SimPlant (now) is the **authoritative** trigger source.
The Raspberry Pi must not reconstruct the contact pose from UART receive time.

```text
Spreizer berührt Messtaster
        ↓
STM32 erkennt Trigger
        ↓
STM32 latcht zugehörige TCP-/Encoderposition
        ↓
STM32 stellt Trigger-Event + Position bereit
        ↓
Core consumeProbeTrigger()
        ↓
ProbeCalibration übernimmt Messpunkt
```

Simulation:

```text
SimPlant erkennt geometrischen Kontakt
        ↓
SimPlant latcht TCP-Pose in Raw/Plant
        ↓
SimProtocolBridge wandelt nach Machine (raw − referenceOffset)
        ↓
HostInputs.probeTriggered + probeTriggerX/Y/Z (Maschinenframe)
        ↓
Machine::consumeProbeTrigger()
        ↓
ProbeCalibration übernimmt Messpunkt
```

`HostInputs.probeTriggerX/Y/Z` are machine-frame, the same as `Machine::x/y/z()`.
SimPlant remains a raw-world sensor; the conversion belongs at the plant→Core
boundary. The STM32 firmware adapter must present machine-frame trigger poses
(already machine-frame on the open-loop MCU path — no plant `referenceOffset`).

### Host contract (Sim **and** STM32) — mandatory

`Machine::setHostInputs` latches a probe event **only** when the host supplies
all of:

| Field | Role |
|-------|------|
| `probeTriggered` | Digital level (true = contact) |
| `probeTriggerPoseValid` | Host latched TCP at the edge |
| `probeTriggerX/Y/Z` | Finite machine-frame pose |
| `probeTriggerSequence` | Non-zero, new value each contact edge |

**Level alone is ignored.** If `probeTriggerPoseValid` is false, Core never sets
`probe_trigger_latched_`; `ProbeSearch` / Einmessprotokoll will not react even
when the GPIO is Low. This is intentional production hardening (no silent
fallback to “current TCP on a later tick”).

**Simulation:** `SimPlant` latches raw TCP + sequence; `SimProtocolBridge`
converts to machine frame (`raw − referenceOffset`).

**STM32 firmware:** `HostIo` reads NC GPIO only (`PA5`, idle High / contact Low,
`probeTriggered = !pinHigh`). `FirmwareHost` on the contact **rising edge**
(false→true of `probeTriggered`) stores `machine_.x/y/z()`, bumps sequence, and
keeps pose valid while contact remains. Poll rate = Core tick **1 kHz** (not
EXTI yet; EXTI can refine latency later without changing this contract).

`consumeProbeTrigger()` consumes a host-provided event. It does not create the
trigger. A later TCP move must not overwrite the latched pose.

Do **not** call `Machine::stop()` merely to end a search; that would abort the
whole process (M18).

## Measurement sequence

Non-blocking FSMs: `ProbeSearch` (SearchL replacement) and `ProbeCalibration`.

Hub pose **P** is the TCP at protocol start (jogged, or after ProbeApproach).
It is the only location reference. It is **not** an assumed disc center.
Every direction starts and ends at P. Transfer and search legs move **one axis
at a time** (never X+Y or X+Z together).

```text
Measure Z from hub P (Z decreases until trigger or timeout)
  → TCP hits disc top face; latch that pose immediately; hard brake
  → probe_center.z = trigger_tcp_z − measuring_head_thickness/2
  → return to P (hub Z unchanged)
  → for each of −Y, +Y, −X, +X:
       P → defined lateral offset at hub Z
       → descend to z_xy_start from measured Z + geometry
       → fast search toward hub until trigger or timeout → latch → hard brake
       → backoff → one fine search until trigger or timeout → latch → hard brake
       → clear to directional start → return to P
  → four TCP contacts → fitCircleCenterXY
  → Install: probeCenter = measured, toolOffset = 0
    OffsetOnly: toolOffset = storedProbeCenter − measured
  → setProbeCenter + setToolOffset only on success
```

XY work height = measured `probe_center.z − search_depth` (default 5 mm below
the **just measured** center).

Each direction: start from hub geometry → fast search → latch → backoff → one
fine search (`num_fine_searches`, default 1) → retreat via P.

Rx/Ry remain out of scope.

## Operating modes

### Manual — `CmdStartProbeCalib` (0x54)

Operator jogs TCP to hub **P** above the disc. ProbeApproach is skipped.
No assumed disc center is derived (`tcp.z − approach_distance` is not used).
FullCalib works without a stored `probe_center`. On Complete the process ends
(`Completed`).

### Automatic — `CmdStart` (0x13)

```text
ProbeApproach (Safe-Z → XY to probe_center → approach Z = hub P)
  → ProbeCalibration (hub = current TCP; probe_center not used as search target)
  → MoveSafePose
  → normal test/aging cycle
```

Safe-Z from M18 is mandatory for XY traverse. No 3D point-to-point.

**ProbeApproach coarse Z** is open-loop to `probe_center.z + approach_distance`
at `v_approach_probe`. During that descent the Core **monitors the Messtaster
latch**. Early contact → hard brake, `FaultReason::ProbeApproachContact`, process
**Aborted** (typical cause: stored `probe_center.z` too low vs. the real disc).
Stale triggers are consumed before the descent starts. Manual Install skips
ProbeApproach entirely (operator jogs the hub).

## SimPlant

Virtual disc **center** (`probe_center.x/y/z`) is independently configurable.
Disc Ø/thickness and spreader Ø/height follow `MachineConfig` while the
simulation host is ticking (`SimProtocolBridge::applyHostInputs`). Visual
stem and base in the 3D view are not used for collision.

Collision: vertical tool cylinder from TCP in +Z
(`tool_diameter` × `tool_cylindrical_height`) against the disc volume
(`measuring_head_diameter` × `measuring_head_thickness`).

Plant channel (simulation only, not M12):

```text
{"cmd":"setProbeDisc","x":40,"y":40,"z":10,"diameter":8,"thickness":3,
 "toolDiameter":2.5,"toolCylindricalHeight":15}
```

Manual `triggerProbe` pulses remain for debugging.

## Configuration (GUI / M12)

Writable via Parametereinstellungen. SafetyLimits are **not** GUI-editable.

| Parameter | Default |
|-----------|---------|
| `head_diameter` | 0 mm (unused) |
| `measuring_head_diameter` | 8 mm |
| `measuring_head_thickness` | 3 mm |
| `tool_diameter` | 2.5 mm |
| `tool_cylindrical_height` | 15 mm |
| `probe_xy_search_margin` | 2 mm |
| `insert_search_distance` | 10 mm (Phase-1 Z dive; not a probe XY limit) |
| `probe_search_speed` | 2 mm/s |
| `probe_search_distance` | 10 mm (ABB XY start clearance) |
| `probe_backoff` | 1 mm |
| `search_depth` | 5 mm (XY work height below measured center) |
| `approach_distance` | 10 mm |
| `fine_search_speed` | 1 mm/s |
| `num_fine_searches` | 1 |
| `probe_offset_reference_{x,y,z}` | (40, 40, 10) mm |

`MachineConfig` payload is **232 bytes** (was 220; adds `probe_center_x/y/z`
mirrored from `CalibrationSet` on Get). Codec: `communication/PayloadCodec.cpp`
and `gui/server/protocol/m12Codec.js`.

## Persistence

- Runtime source of truth: `CalibrationSet` in Core RAM.
- `tool_offset` is also written into `MachineConfig.calibration.tool_offset` so
  `CmdGetMachineConfig` returns the replaced value.
- After a successful **Install** (FullCalib) the GUI `POST /api/calibration/sync`
  with `{ persistProbeCenter: true }` stores `probe_center_*` from
  `MsgMachineConfig` and `tool_offset = 0`.
- After **OffsetOnly** sync with `persistProbeCenter: false` updates only
  `tool_offset_*` and must not rewrite `probeCenter`.
- Do **not** reconstruct `probeCenter` as `probe_offset_reference − tool_offset`.
- `probe_offset_reference` remains in config as legacy only.

## Failure modes

| Case | Result |
|------|--------|
| No trigger / timeout | Error, no new calib |
| STOP / Abort / Fault during search | Abort, previous calib kept |
| Probe contact during ProbeApproach coarse Z | `ProbeApproachContact`, abort, previous calib kept |
| Degenerate 4-point fit / unrealistic radius | Error, previous calib kept |
| Invalid config (NaN/Inf, out of SafetyLimits) | Rejected, previous config kept |
| Repeat successful calib | Offset replaced; no drift |

### Bench: kinematics OK, GUI says „abgebrochen“

Often the five search legs **look** fine, then `finishAndStore()` rejects the
result. Core expects the four XY contact TCPs to lie on a circle of radius

`expected = measuring_head_diameter/2 + tool_diameter/2` (default **5.25 mm**)

within `max(25 %, 1 mm)`. Otherwise → `ProbeCalibration` Error → process
**Aborted**, previous `probeCenter` kept — **no** `FaultReason` in that case.

Also check status-bar **Fault**:

| Fault | Meaning |
|-------|---------|
| `CommunicationTimeout` | Comm-watchdog during Automatic (`CmdNop` gap > `heartbeat_timeout_s`, default 0.3 s) |
| `ProbeSearchNoContact` | A search timed out without a latched contact |
| `ProbeApproachContact` | Messtaster fired during ProbeApproach coarse Z (hub target below real disc) |
| other / None + Aborted | Usually circle-fit / geometry reject at the end |

Checklist: start hub **above disc center**; real disc/tool diameters in
MachineConfig; XY `steps/mm` correct (Zortrax bench ≠ 200); open-loop lag can
shrink the measured radius vs. expected.

## Known limits / later STM32

- No Rx/Ry, no second probe, no automatic spreader-state sensing.
- Disc stem/base in the 3D view are visual only.
- STM32: **1 kHz poll latch** in `FirmwareHost` (see Host contract above).
  Optional later: EXTI edge for sub-millisecond pose stamp; same `HostInputs`
  fields.
- Nucleo note: `PA5` is also LD2 on many F446 Nucleo boards — disconnect the
  LED solder bridge if the probe shares PA5, or the High level is loaded down.
- Hardware bench validation continues under M19.x firmware bring-up.
