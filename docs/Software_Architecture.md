#Software Architecture

The project is divided into:

1. Core
2. Simulation
3. STM32 Firmware
4. Node.js GUI
5. Communication Protocol

## Core

The Core contains platform-independent C++ code.

The same Core shall run:
- on the development PC in the simulation
- on the STM32 in the final system

Hardware-specific code must NOT be placed inside the Core.

The Core must not directly use:
- STM32 HAL
- Arduino libraries
- Windows APIs
- Linux APIs
- Node.js APIs

## Simulation

The simulation runs entirely on a development PC.

The simulation shall reproduce the relevant machine behavior,
but does NOT need to reproduce physical motor dynamics.

Initially the simulation shall model:

- X axis
- Y axis
- Z axis
- position
- velocity
- acceleration
- target position
- machine state
- test points

Later the simulation shall model:

- force sensor
- force-dependent XY correction
- limit switches
- pneumatic cylinder

The simulation is intentionally simplified.

Do NOT implement detailed physical motor models unless explicitly requested.

## Architecture verification (M14.2)

See [`docs/M14_2_Architecture.md`](M14_2_Architecture.md) for the as-built
GUI / Core / Simulation separation, command/status data flows, and runtime
independence of HMI vs. controller host.

## Simulation UI and sensor plant (M15)

See [`docs/M15_Simulation_UI.md`](M15_Simulation_UI.md) for the standalone
simulation UI, the virtual outside world (`simulation_plant`), the separate
plant channel on port 8767, and the hierarchical gantry visualization.

## Soft limits and homing (M16)

End stops block only the affected axis direction. Homing is Z+ → X+ → Y+.
The M12 StatusReport is 41 bytes (`homingState`, `blockedDirections`, `flags`,
`procedurePhase`, `faultReason`, `spreaderState`); `CmdHome` is `0x15`. See
[`docs/M16_Agent2_Interface.md`](M16_Agent2_Interface.md),
[`docs/M17_Procedure.md`](M17_Procedure.md), and [`docs/M18_Procedure.md`](M18_Procedure.md).
Probe calibration on the 3-axis Core is **M19.0**
([`docs/M19_ProbeCalibration.md`](M19_ProbeCalibration.md)). STM32 firmware
hosting follows later. M19.0 is not a hardware release.