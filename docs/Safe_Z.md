# Safe-Z (dynamic, program-relative)

There are two clearance concepts:

| Name | When | Formula base |
|------|------|----------------|
| **Program-Safe-Z** | Automatic procedure / ProbeApproach | program points (+ probe) in plate frame + `safe_z_buffer` |
| **Safe-Z-Calib** | Teach Anfahrt (`CmdApproachCalibPoint`) | max machine-Z of stored KM/K1–K3/probe + `safe_z_calib_buffer` |

## Formula (plate frame) — Program-Safe-Z

```text
approach_z_plate    = program.point.z + point_z_clearance_mm
probe_z_plate       = probe_center.z − plate_origin.z − tool_offset.z
max_clearance_z     = max(max(approach_z_plate), probe_z_plate)
safe_z_plate        = max_clearance_z + safe_z_buffer
```

- `program.point.z` is the JSON clamp floor in **Bauplattenkoordinaten**.
- `point_z_clearance_mm` (default **2 mm**) is the approach offset in +Z. The fine approach stops there. Fine approach and pneumatic Clamp do not command below the floor.
- `probe_center` is the calibrated Einmessbohrung mid-point (**machine** frame);
  it is converted to plate frame before the max.
- If the Einmessbohrung sits higher than every test point, the buffer is added to
  the probe Z instead. Otherwise the previous point-only height is unchanged.
- `safe_z_buffer` is the configurable clearance (default **30 mm**).
- There is no freely editable absolute machine-frame Safe-Z.
- Probe is included only when `probe_center_valid`.

## Machine frame

Plate → machine transformation (unchanged):

```text
machine = plate_origin + plate_point + tool_offset
```

Applied to Safe-Z:

```text
safe_z_machine = plate_origin.z + safe_z_plate + tool_offset.z
```

Therefore:

- Changing `plate_origin.z` changes the **machine** Safe-Z height, not `safe_z_plate`.
- `tool_offset` is applied once in that transform (never doubled for Safe-Z).

## Trajectory

Existing approach legs (no second planner):

```text
Raise Z to safe_z_machine
→ XY at safe_z_machine
→ Lower Z to target
```

`MotionPlanner` remains the axis authority.

## Workspace reject

The GUI planned path (`buildProcedurePlannedPath`) is checked against the
simulation plant limits (raw frame; `raw = machine + referenceOffset`).

If any waypoint lies outside that envelope, start is refused and the Sim-UI
shows an error. Safe-Z is **not** clamped to `zMax`. Soft-limit blocking in
Core is unchanged (no Fault).

Without a reachable plant channel the envelope check is skipped (hardware).

## Configuration

| Field | Role |
|---|---|
| `MachineConfig.motion.safe_z_buffer` | Editable clearance [mm], default 30 |
| computed `safe_z_plate` / `safe_z_machine` | Derived; not stored as MachineConfig |

Legacy absolute `safe_z` in persisted GUI JSON is stripped on load.

## Safe-Z-Calib (teach Anfahrt)

Used only by `CmdApproachCalibPoint` (Wizard: alten KM/K1–K3/Einmesspunkt anfahren). No program required.

```text
max_calib_z_machine = max(Z of valid plate_origin, k1, k2, k3, probe_center)
safe_z_calib_machine = max_calib_z_machine + safe_z_calib_buffer
```

Trajectory (same legs as ProbeApproach, different clearance):

```text
Raise Z to safe_z_calib_machine
→ XY at safe_z_calib_machine to target.xy
→ Lower Z to target.z + probe.approach_distance
```

| Field | Role |
|---|---|
| `MachineConfig.motion.safe_z_calib_buffer` | Editable clearance [mm], default **30** |
| `MachineConfig.probe.approach_distance` | Standoff above stored center [mm], default **15** |

K1–K3 must be pushed into Core (`CmdSetRotationPoints`) on connect so they contribute to the max and can be approached by point id.

## Start preconditions

Automatic start requires:

- non-empty program (otherwise Safe-Z cannot be computed),
- valid `plate_origin`,
- valid `probe_center`,
- machine homed,
- resolvable `safe_z_machine` with all points strictly below it,
- planned path inside plant limits when those limits are available.

Core helpers: `core/SafeZ.hpp`. GUI mirror: `gui/server/coord/safeZ.js`.
