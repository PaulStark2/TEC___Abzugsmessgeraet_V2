# M15 — Simulation UI and virtual gantry

M15 adds a standalone simulation UI plus a virtual outside world (sensor plant)
for the PC simulation. The controller stays the only authority for motion.

## Component roles

| Component | Role |
| --- | --- |
| `core/` (`Machine`, `MotionPlanner`, `Axis`, `TestExecutor`) | **Controller.** Decides motion, safety reaction, force control. Unchanged by M15 except one new input field. |
| `simulation_plant/SimPlant` | **Virtual outside world.** Turns the controller-reported pose into sensor signals (limit switches, probe, e-stop). No motion logic. |
| `simulation_plant/PlantCommandCodec` | Parses/serializes the plant channel messages (platform independent, unit tested). |
| `simulation_protocol/PlantChannelServer` | TCP transport for the plant channel (loopback, line-delimited JSON). |
| `simulation_bridge/SimProtocolBridge` | M12 frame dispatch + tick scheduling; feeds `SimPlant` output into `Machine::setHostInputs()`. |
| `gui/server/simulation/plantClient.js` | Node proxy client for the plant channel. |
| `gui/public/js/sim/gantryView.js` | Display-only Three.js gantry (hierarchical scene graph). |
| `gui/public/js/sim/simulationPage.js` | Simulation UI page logic: status rendering + sensor input. |

## Data flow

Machine commands (unchanged, M12 only):

```text
Browser (HMI)
  → POST /api/command
  → ProtocolMachineClient
  → M12 frame (:8766)
  → CommandDispatcher
  → Machine / TestExecutor        ← CONTROLLER DECIDES
```

Pose to 3D view (status path only):

```text
Machine (Core pose)
  → StatusSnapshot → MSG_STATUS_REPORT (:8766)
  → ProtocolMachineClient
  → /ws/status (Node, ~5 Hz)
  → simulationPage.renderStatus()
  → gantryView.setPose({x, y, z})
```

Sensor injection (simulation only, never M12):

```text
Simulation UI
  → POST /api/simulation/{limits,probe,estop}
  → plantClient (TCP :8767, JSON lines)
  → PlantChannelServer → applyPlantCommand()
  → SimPlant state
```

Sensor evaluation inside the simulation host (~20 Hz tick):

```text
SimPlant.sample(machine.x(), machine.y(), machine.z())
  → HostInputs (force = 0, limitX/Y/ZMax by position, probeTriggered, e_stop)
  → Machine::setHostInputs()
  → Machine::tick() / TestExecutor::tick()   ← CORE SAFETY DECIDES
  → StatusSnapshot
```

## Limit switches

`SimPlant` trips a max limit switch when the pose reaches the configured value
(`x >= x_max`, analogous for Y and Z). The plant does **not** stop motion. The
reaction is `Machine::tick()` → `isCommandedIntoActiveLimit()` →
`enterSafetyFault()` (latched `MachineState::Fault`, motion frozen).

Min limit switches are not simulated in M15.

## Probe

`HostInputs.probeTriggered` is a digital input. SimPlant now also raises it on
**geometric contact** of the spreader TCP with the virtual measuring disc
(center, diameter, thickness, tool diameter). A manual `triggerProbe` pulse
remains for debugging. The host latches TCP at the rising edge.

The simulation UI can move the disc with `setProbeDisc`. See
[`docs/M19_ProbeCalibration.md`](M19_ProbeCalibration.md).

## Force

Force simulation is disabled in M15: `SimPlant::sample()` always reports
`fx = fy = fz = 0`. The Core force control (`Machine::ForceControlParams`,
hysteresis, Fz safety) is fully retained and still covered by
`forcecontrol_tests`.

The older `SimHostInputProvider` (M9 demo force `k · pose`) is no longer used by
the protocol host; it remains as the M9/M13 input stub for `bridge_status_tests`.

## Plant channel (port 8767)

Loopback TCP, one JSON object per line, one response line per request. This
channel is PC-simulation only and carries no machine commands.

| Request | Effect |
| --- | --- |
| `{"cmd":"getState"}` | read plant state + Core pose |
| `{"cmd":"setLimits","xMax":190,"yMax":140,"zMax":110}` | set trip points (omitted axes keep their value) |
| `{"cmd":"triggerProbe","pulseMs":100}` | one probe pulse (debug) |
| `{"cmd":"setProbeDisc","x":40,"y":40,"z":10,"diameter":8,"thickness":3,"toolDiameter":2.5,"toolCylindricalHeight":15}` | virtual measuring disc |
| `{"cmd":"setEStop","active":true}` | set/clear the virtual e-stop input |

Response: `{"ok":true,"state":{...},"pose":{"x":…,"y":…,"z":…}}` or
`{"ok":false,"error":"…"}`. Unknown commands (for example `moveTo`) are rejected.

Node exposes it as `GET /api/simulation/state` and
`POST /api/simulation/{limits,probe,probe-disc,estop}`. If the simulation host is down, the
proxy answers HTTP 503 with a readable message.

## 3D gantry hierarchy

```text
staticGroup      X rails, floor plate, grid, axes   (never move)
bridgeGroup      position.x = pose.x                (uprights + Y traverse)
  carriageGroup  position.y = pose.y                (carriage on the traverse)
    columnGroup  position.z = pose.z                (Z column + TCP cone)
```

The Z column is longer than its travel and slides through the carriage, so the
assembly never separates visually. Geometry (sizes, profile) is display only and
has no effect on Core or sensors. Limit trip points are drawn as faint planes.

## Ports and dev commands

| Port | Purpose |
| --- | --- |
| 3000 | GUI (HMI + `/simulation`) |
| 8766 | M12 controller protocol |
| 8767 | simulation plant channel (M15) |

```text
npm run dev:start            # simulation + GUI (independent lifecycles)
npm run dev:start-gui        # GUI only (offline until simulation starts)
npm run dev:start-simulation # simulation/controller host only
npm run dev:stop-gui         # stop GUI only
npm run dev:stop-simulation  # stop simulation only (also frees 8767)
npm run smoke:m15            # M15 simulation smoke test
npm run proof:architecture   # M14.2 separation proof
ctest --test-dir build       # C++ unit tests incl. simplant/plantchannel
```

## Offline behaviour

- Machine HMI and simulation UI stay open.
- `renderStatus()` skips `setPose()` while `connected === false`: the gantry
  freezes at the last controller pose, no local animation.
- Machine commands are rejected by `ProtocolMachineClient`.
- Plant requests answer with a connection error (HTTP 503).

## Deliberately not implemented in M15

- No force simulation and no probe-based measuring/calibration logic.
- No min limit switches.
- No M12 extension (no new message IDs, unchanged frame layout).
- No simulation of collisions or drive dynamics.
- The gantry module is not yet embedded in the machine HMI (kept modular for a
  later integration); the existing small TCP view there is untouched.
