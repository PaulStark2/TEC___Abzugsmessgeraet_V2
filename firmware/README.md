# STM32F446RE firmware (M19.x Phase 1–5a / 6–7)

Standalone CMake project. Do not add this folder to the host `CMakeLists.txt` —
the firmware uses `arm-none-eabi` and a different toolchain.

## Scope

- 180 MHz, IWDG, SysTick 1 kHz → Core `Machine` / `TestExecutor`
- ADC DMA Fx/Fy/Fz, limits, probe, E-Stop, spreader pair, fixture
- XYZ step GPIO (PB6/PB4/PB8), DIR PC1/PC3/PC5, 200 steps/mm
- **M12 binary protocol** on host UART (replaces ASCII Phase-2–4 bench)
- **ForceCalibHost**: `MsgRawAdc` at 50 Hz; after GUI commit, polynomial on Fx/Fy/Fz (same Z curve)
- **Process (Phase 6)**: shared `CommandDispatcher` Start/Stop/Pause; `TestExecutor` tick while Running; Core comm-watchdog advances **only on `CmdNop`** (GUI already sends 10 Hz)

## Host UART (`TEC_HOST_UART`)

| Value | Pins | Use |
|-------|------|-----|
| **`USART2`** (default) | PA2/PA3 | Laptop ST-Link VCP → GUI `PROTOCOL_TRANSPORT=uart` |
| `USART3` | PC10/PC11 | Pi `/dev/serial0` (Phase 5b) |

115200 8N1. StatusReport **50 Hz**. GUI sends **CmdNop 10 Hz** when the session is live.

## Build / flash

```powershell
cmake -S firmware -B build-firmware -G Ninja `
  --toolchain "$PWD\firmware\cmake\arm-none-eabi-stm32f446.cmake" `
  -DTEC_HOST_UART=USART2
cmake --build build-firmware
```

Flash `build-firmware/tec_firmware.hex`.

## GUI against Nucleo

```powershell
cd gui
npm install serialport
$env:MACHINE_CLIENT='protocol'
$env:PROTOCOL_TRANSPORT='uart'
$env:PROTOCOL_SERIAL_PATH='COMx'   # Device Manager → ST-Link VCP
npm start
```

Expect: status online after first `MsgStatusReport`; jog/`moveTo` via M12.

### Phase 7 — Kraftkalibrierung (UART)

1. Flash current hex; GUI as above (close Serial Monitor / other COM users).
2. Live status should include **ADC counts** (`MsgRawAdc`); force wizard can sample Z.
3. On connect, GUI push of a committed curve should **ACK** (no longer `Unsupported`).
4. After commit: StatusReport force = polynomial on ADC (all three channels share the Z curve).
5. Without a committed curve: provisional linear map `0…4095 → ±force_range_N` remains.

Bench: real weights / wizard points when the sensor is ready; software path can be checked with a stored curve from a prior sim session.

### Phase 6 — Prozess am Target

Same M12 commands as simulation. Typical gate for `CmdStart`:

1. Homing (`isHomed`)
2. Probe center valid
3. Plate origin valid
4. Non-empty program

While `ProcessState::Running`, Core enables the communication watchdog — GUI `CmdNop` must keep flowing or the machine Faults (no auto-resume). STOP always works.

### Real XYZ jog (existing code, parallel to Phase 6)

Realistic with current firmware if:

- DM542: STEP←PB6/PB4/PB8, DIR←PC1/PC3/PC5, common GND; **ENA** tied/enabled on the driver (no MCU ENA pin yet)
- DIP µstep matches **200 steps/mm** (57HS56 1/4 + SFU1204 assumption)
- 5 V level shifting if the drivers need it; motor supply + E-Stop hardware cut
- Soft-start: single-axis jog at low speed; confirm DIR polarity before multi-axis
- +Max limits for Homing (or temporary button-sim as before)
- Expect ugly Oszi traces to still move motors; lost steps possible (open-loop)

Jog does **not** need Phase 6. Keep E-Stop reachable.

## Motion pins

| Axis | STEP | DIR | Cap |
|------|------|-----|-----|
| X | PB6 | PC1 | 10 kHz |
| Y | PB4 | PC3 | 10 kHz |
| Z | PB8 | PC5 | 5 kHz |

Canonical map: `include/PinMap.hpp`.
