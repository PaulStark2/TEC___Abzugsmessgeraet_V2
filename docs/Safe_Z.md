# Safe-Z (dynamic, program-relative)

## Formula (plate frame)

```text
probe_z_plate       = probe_center.z − plate_origin.z − tool_offset.z
max_clearance_z     = max(max(program.point.z), probe_z_plate)
safe_z_plate        = max_clearance_z + safe_z_buffer
```

- `program.point.z` are **Bauplattenkoordinaten** (plate frame).
- `probe_center` is the calibrated Messtaster position (**machine** frame); it is
  converted to plate frame before the max.
- If the Messtaster sits higher than every test point, the buffer is added to
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

## Start preconditions

Automatic start requires:

- non-empty program (otherwise Safe-Z cannot be computed),
- valid `plate_origin`,
- valid `probe_center`,
- machine homed,
- resolvable `safe_z_machine` with all points strictly below it,
- planned path inside plant limits when those limits are available.

Core helpers: `core/SafeZ.hpp`. GUI mirror: `gui/server/coord/safeZ.js`.
