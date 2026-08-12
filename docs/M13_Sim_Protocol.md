# M13 — Sim Protocol Bridge

M13 exposes the PC simulation over the **M12 binary protocol** on TCP localhost. It maps protocol commands to the existing Core APIs (`Machine`, `TestExecutor`) and publishes Core state as `MSG_STATUS_REPORT` frames.

## Hosts

| Host | Port | Protocol | Purpose |
|------|------|----------|---------|
| `simulation_interactive` | **8765** | Newline JSON (M8.5) | Existing GUI (`SimMachineClient`) |
| `simulation_protocol` | **8766** | M12 binary frames (M13) | External clients, M14 prep |

Both hosts can run in parallel. M13 does **not** replace M8.5 until M14 migrates the GUI.

## Architecture

```
External Client
    ↓ TCP (byte stream)
simulation_protocol/main.cpp
    ↓
FrameStreamBuffer (stream reassembly)
    ↓
SimProtocolBridge
    ├─ CommandDispatcher → Machine / TestExecutor
    ├─ StatusSnapshot    ← Core state (read-only)
    └─ SimHostInputProvider → Machine::setHostInputs()
    ↓
M12 Encoder (communication/)
```

- **No motion/safety logic** in the bridge — only decode, map, encode, transport.
- **Single client** (new connection replaces the previous one).
- **20 Hz** simulation tick (`dt = 0.05 s`) with status push while connected.

## Build & run

```bash
cmake -S . -B build
cmake --build build
./build/simulation_protocol/simulation_protocol.exe      # default port 8766
./build/simulation_protocol/simulation_protocol.exe 9000  # custom port
```

## Wire format

See `communication/ProtocolSpec.hpp`. Frames start with magic bytes `0x54 0x45` ("TE"), version 1, little-endian fields, CRC16-CCITT-FALSE.

Supported commands (host → sim): `CMD_NOP`, `CMD_STOP`, `CMD_PAUSE`, `CMD_RESUME`, `CMD_START`, `CMD_CLEAR_FAULT`, `CMD_MOVE_TO`, `CMD_LOAD_PROGRAM`, `CMD_SET_SPREADER`, `CMD_SET_FIXTURE`, `CMD_REQUEST_STATUS`.

Responses: `RSP_ACK`, `RSP_NACK`, `MSG_STATUS_REPORT`, `MSG_PROGRAM_SUMMARY` (after successful load).

## ACK / NACK semantics

- Protocol/payload errors → **NACK** (`InvalidPayload`, `Unsupported`).
- CRC/frame corruption → **no response** (stream resync).
- Core success → **ACK Ok**; no-op → **ACK Rejected**; wrong state → **ACK InvalidState** / **ProgramEmpty** / **FaultLatched**.

## Example external client flow

1. Connect to `127.0.0.1:8766`.
2. Receive initial `MSG_STATUS_REPORT`.
3. Send `CMD_MOVE_TO` with `{x,y,z}` payload → receive `RSP_ACK`.
4. Receive periodic `MSG_STATUS_REPORT` (~20 Hz) while moving.

For automated tests, see `tests/bridge_*_tests.cpp`.

## M14 / M15

- **M14:** Node.js `ProtocolMachineClient` targeting port 8766.
- **M15:** Same M12 frames over UART on STM32; transport changes, bridge mapping concept stays on the firmware side.
