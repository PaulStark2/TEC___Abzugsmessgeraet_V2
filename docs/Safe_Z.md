# Safe-Z (dynamic, program-relative)

## Formula (plate frame)

```text
safe_z_plate = max(program.point.z) + safe_z_buffer
```

- `program.point.z` are **Bauplattenkoordinaten** (plate frame).
- `safe_z_buffer` is the configurable clearance above the highest test point (default **30 mm**).
- There is no freely editable absolute machine-frame Safe-Z.

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
- resolvable `safe_z_machine` with all points strictly below it.

Core helpers: `core/SafeZ.hpp`. GUI mirror: `gui/server/coord/safeZ.js`.
