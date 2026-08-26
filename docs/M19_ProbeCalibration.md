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

After the Z measurement, `reference.z` becomes the measured disc-center Z.
Each lateral search then uses:

```text
XY search Z = measured probe_center.z − search_depth
```

Defaults: `search_depth = 5 mm`. For `probe_center.z = 0` the XY plane is
**z = −5 mm**. This is intentional for the real spreader. It is **not** the
disc mid-plane.

The ~15 mm constant-diameter shaft starting at TCP is long enough to contact
the 3 mm disc while the TCP sits 5 mm below the center.

## XY search limit

`search_distance` remains the ABB-style extra XY **start** clearance:

```text
xy_start_offset = search_distance + R_probe + R_tool
```

It is **not** the maximum search travel and **not** the Phase-1 insert depth.

The hard XY motion bound is the innermost commanded |TCP_xy − assumed probe axis|.
The TCP must not be driven to the probe axis.

Named quantity (tool radius is **not** added):

```text
max_xy_search_radius = measuring_head_diameter/2 + probe_xy_search_margin
                     = 4 mm + 2 mm = 6 mm
```

Using that 6 mm value as the inner TCP radius would stop the search at 6 mm
from the axis and miss geometric contact at 5.25 mm. The commanded inner
radius is therefore the complementary inward stop:

```text
xy_search_inner_radius = max(0, R_probe − probe_xy_search_margin)
                       = max(0, 4 mm − 2 mm) = 2 mm
```

Meaning: the TCP may travel inward until it is `probe_xy_search_margin` past
the disc radius toward the axis. Default path: start at ~15.25 mm from the
axis, stop at 2 mm. Contact at 5.25 mm lies on that path. Reaching the axis
(`r = 0`) is not commanded.

`probe_xy_search_margin` is a process/calibration parameter (GUI + M12), not a
GUI-editable `SafetyLimits` constant. It must stay strictly below `R_probe` so
the inner radius stays positive.

No trigger before the inner target is reached:

1. Search → Error
2. ProbeCalibration → Error
3. no new `probe_center` / `tool_offset` is stored
4. previous valid calibration remains

This limit is a **motion target**, not a post-hoc validation.

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
SimPlant latcht TCP-Pose
        ↓
HostInputs.probeTriggered
        ↓
Machine::consumeProbeTrigger()
        ↓
ProbeCalibration übernimmt Messpunkt
```

`consumeProbeTrigger()` consumes a host-provided event. It does not create the
trigger. A later TCP move must not overwrite the latched pose. Sequence numbers
prepare the STM32 event counter. SimPlant latches on the discrete tick of the
rising edge; sub-tick interpolation is not required.

Do **not** call `Machine::stop()` merely to end a search; that would abort the
whole process (M18).

## Measurement sequence

Non-blocking FSMs: `ProbeSearch` (SearchL replacement) and `ProbeCalibration`.

Hub pose **P_z** = `(reference.x, reference.y, reference.z + approach_distance)`.
Every direction starts and ends at P_z. Transfer and search legs move **one axis
at a time** (never X+Y or X+Z together).

```text
Measure Z from P_z (Z decreases, v_fast then optional fine)
  → TCP hits disc top face
  → probe_center.z = trigger_tcp_z − measuring_head_thickness/2
  → return to P_z (updated hub Z from measured center)
  → for each of −Y, +Y, −X, +X:
       P_z → lateral at hub Z → descend to XY work height
       → fast search → latch → backoff → optional fine searches
       → clear along search axis → raise to hub Z → return to P_z
  → four TCP contacts → fitCircleCenterXY
  → Install: probeCenter = measured, toolOffset = 0
    OffsetOnly: toolOffset = storedProbeCenter − measured
  → setProbeCenter + setToolOffset only on success
```

XY work height = measured `probe_center.z − search_depth` (default −5 mm).

Each direction: approach (via P_z) → fast search → latch → backoff → optional
fine searches (`num_fine_searches`, default 3, mean of valid hits) → retreat
via P_z.

Rx/Ry remain out of scope.

## Operating modes

### Manual — `CmdStartProbeCalib` (0x54)

Operator jogs TCP to hub **P_z** above the disc. ProbeApproach is skipped.
`search_ref.z = tcp.z − approach_distance` (assumed disc center). On Complete
the process ends (`Completed`).

### Automatic — `CmdStart` (0x13)

```text
ProbeApproach (Safe-Z → XY to probe_center → approach Z = P_z)
  → ProbeCalibration (search_ref.z = probe_center.z)
  → MoveSafePose
  → normal test/aging cycle
```

Safe-Z from M18 is mandatory for XY traverse. No 3D point-to-point.

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
| `num_fine_searches` | 3 |
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
| No trigger / XY inner limit / Z search_depth | Error, no new calib |
| STOP / Abort / Fault during search | Abort, previous calib kept |
| Degenerate 4-point fit / unrealistic radius | Error, previous calib kept |
| Invalid config (NaN/Inf, out of SafetyLimits) | Rejected, previous config kept |
| Repeat successful calib | Offset replaced; no drift |

## Known limits / later STM32

- No Rx/Ry, no second probe, no automatic spreader-state sensing.
- Disc stem/base in the 3D view are visual only.
- STM32 must detect the digital trigger within a few milliseconds and latch
  the current commanded/encoder pose into `HostInputs` with a sequence bump.
- Hardware validation is a separate release. **This milestone does not
  authorize hardware use.**
