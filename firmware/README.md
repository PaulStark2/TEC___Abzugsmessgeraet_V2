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
- **Probe latch**: NC on PA5; `FirmwareHost` latches machine pose + sequence on contact (required by Core)

## Critical host contracts (do not regress)

These are easy to break on the MCU path and look like “hardware dead” in the GUI.

### Probe / Messtaster

| Item | Value |
|------|--------|
| Pin | **PA5** (not Nucleo LD2 — disconnect LD2 solder bridge if shared) |
| Switch | **NC**: idle High, contact Low |
| GPIO | Input + pull-down; `HostIo`: `probeTriggered = !pinHigh(PA5)` |
| Core | Latches **only** if host also sets `probeTriggerPoseValid`, finite X/Y/Z, and a **new** `probeTriggerSequence` |
| Firmware | `FirmwareHost` on rising edge of `probeTriggered`: store `machine_.x/y/z()`, bump sequence; hold while contact |

Without the pose latch, plate/probe Einmessung never sees contact even when the
scope shows Low on PA5. Spec: [docs/M19_ProbeCalibration.md](../docs/M19_ProbeCalibration.md).

### Limit switches / false Homing

| Item | Value |
|------|--------|
| Logic | High = triggered (+Max = Homing-Referenz) |
| Noise | Stepper EMI on long wires → spikes (~2 V); Core used to accept **one** High sample |
| Firmware | **10 ms** stable High/Low debounce in `HostIo` before publishing limits (E-Stop undebounced) |
| Hardware | Keep **≤4.7 kΩ** (better **1–2.2 kΩ**) pull-down to MCU GND; short twisted pair; away from STEP/DIR; optional **RC** (~100 Ω + 10–100 nF) at the pin; stronger filter / Schmitt if still dirty |

At `v_homing = 5 mm/s`, 10 ms ≈ 0.05 mm past the switch — acceptable. Spikes below VIH (~2.31 V @ 3.3 V) can still false-trigger if peaks are narrower/higher than the scope shows or GND bounces.

### Communication watchdog (Automatic)

`hostHeartbeatSequence` advances **only on `CmdNop`**, not on every ADC tick.
GUI already sends Nop at 10 Hz while the UART session is live.

### Steps/mm (open-loop)

Per-axis `TEC_STEPS_PER_MM_*` — see Build below. Bench Zortrax XY is **20** steps/mm
(belt, measured 2026-10-15 after the one-pulse fix: soft 10 mm → 5 mm at 10 steps/mm). Bench Z is **200** steps/mm (4 mm lead, 1/4 µstep): 800 pulses = one
revolution. Each software step is one rising edge (one-pulse PWM). Reset every
axis to **200** before the SFU1204 target mechanics.

## Host UART (`TEC_HOST_UART`)

| Value | Pins | Use |
|-------|------|-----|
| **`USART2`** (default) | PA2/PA3 | Laptop ST-Link VCP → GUI `PROTOCOL_TRANSPORT=uart` |
| `USART3` | PC10/PC11 | Pi `/dev/serial0` (Phase 5b) |

115200 8N1. StatusReport **50 Hz**, `MsgRawAdc` **10 Hz**. GUI sends **CmdNop 10 Hz** when the session is live.

### UART stalls / sticky jog / hung calibration (important)

Symptoms: GUI says process still running / red feedback while you are done; STOP sluggish; jog steps ignored or motors stutter while status may still update.

Typical cause on this firmware: **Step TIM IRQs outranked USART** → TX ring fills → **ACK frames dropped** → GUI waits on `sendFrameAndWait` / process never gets a clean STOP. Force fields with `—` use CSS class `status-bar__force--pending` (looks “stuck transmitting”).

Mitigations in current builds:

| Fix | Detail |
|-----|--------|
| IRQ priority | Host USART **prio 2**, Step TIM **prio 4** |
| TX pending queue | Command replies retried; status deferred while pending |
| Bandwidth | RawAdc throttled to 10 Hz; status skipped if TX tight |

If it still happens: raise `PROTOCOL_ACK_TIMEOUT_MS` (default UART 5000), avoid serial monitors on the same COM, consider later baud >115200 on both sides.

## Build / flash

```powershell
cmake -S firmware -B build-firmware -G Ninja `
  --toolchain "$PWD\firmware\cmake\arm-none-eabi-stm32f446.cmake" `
  -DTEC_HOST_UART=USART2 `
  -DTEC_STEPS_PER_MM=200
cmake --build build-firmware
```

Per-axis overrides (empty = `TEC_STEPS_PER_MM`):

```powershell
# Zortrax M300 bench — XY belt 20; Z 4 mm lead, 1/4 µstep → 200
cmake -S firmware -B build-firmware -G Ninja `
  --toolchain "$PWD\firmware\cmake\arm-none-eabi-stm32f446.cmake" `
  -DTEC_HOST_UART=USART2 `
  -DTEC_STEPS_PER_MM=200 `
  -DTEC_STEPS_PER_MM_X=20 `
  -DTEC_STEPS_PER_MM_Y=20 `
  -DTEC_STEPS_PER_MM_Z=200
```


| Value | Use |
|-------|-----|
| **200** all | Zielkinematik SFU1204 |
| **XY 20 / Z 200** | Zortrax bench. Z: `(200 × 4) / 4 mm`. XY: `10 × (10/5)` |

Wrong value scales **speed and distance**. Recalibrate: `spm_new = spm_old × (mm_commanded / mm_measured)`. Theoretical: screw `(200 × µstep) / lead_mm`; belt `(200 × µstep) / (teeth × pitch_mm)`.

Also keep `v_homing` modest in the GUI (default 5 mm/s; for first seeks try 1–2). Homing hits the switch at constant speed (`a_homing = 0`).

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

Expect: status online after first `MsgStatusReport`; jog/`moveTo` via M12. Der Handmodus-Knopf „Ruheprobe“ und die Stillstände der Prüfprozedur nutzen `CmdForceTare` (`0x5C`). Ablauf, Offset und Statusbit 7 stehen in `docs/Force_Calibration.md`. Die Statusmeldung bleibt 41 Byte.

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

### Probe Einmessung (Bauplatte / Messtaster)

1. MCS + Homing as usual; probe wired NC to PA5 (Low = contact).
2. Flash a build that includes the `FirmwareHost` pose latch (see Critical host contracts).
3. Start geometric probe calibration from the GUI over UART.
4. On contact, Core must consume the latched event — if search times out with a clear Low on PA5, the latch/sequence path is broken (not the NC wiring).

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
