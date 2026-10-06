# STM32F446RE firmware (M19.x Phase 1–2.1)

Standalone CMake project. Do not add this folder to the host `CMakeLists.txt` —
the firmware uses `arm-none-eabi` and a different toolchain.

## Phase 1 scope

- 180 MHz clock (HSE 8 MHz from Nucleo ST-Link)
- GPIO according to `include/PinMap.hpp` (step/dir held low)
- Independent watchdog ~1 s, refreshed every Core tick
- SysTick 1 kHz → `Machine::tick(0.001f)`
- Heartbeat LED on **PC7** (Nucleo LD2 on PA5 is the probe input)
- Same `core/` and `communication/` sources as the PC simulation

## Phase 2.1 bench (USART3 115200 8N1 on PC10/PC11)

USB-TTL or Pi UART. **Not** the ST-Link VCP — SB13/SB14 stay closed so USART2
PA2/PA3 remains available for debug.

- Bytes are echoed.
- Once per second a status line: `IO e=.. p=.. lim=0x.. adc=..,..,.. mN=..,..,.. spr=.. st=..`
- `spr=1` means the open valve (PC8) is High and the close valve (PC12) is Low
- `?` — status immediately
- `F` — toggle fixture relay (PC9)
- `R` — toggle spreader (PC8/PC12 complementary pair)
- `lim` bits: 0=X− 1=X+ 2=Y− 3=Y+ 4=Z− 5=Z+ (High = triggered)
- `p=1` when probe pin is Low (NC contact)
- `e=1` when E-Stop is High
- PC7 blinks ~1 Hz; **~5 Hz if Core Fault**
- Open analog pins can float into Fault (collision 20 N / hard limit 30 N). Tie
  PA0/PA1/PA4 toward ~1.5 V for a quiet bench, or accept Fault while probing GPIO.
- Newton on the status line is provisional: 0…4095 → ±`force_range_N` (default ±30 N)

Not in Phase 2.1: step PWM timers, probe EXTI pose-latch, M12 protocol.

## Build

Requires GNU Arm Embedded Toolchain 14.2 (`arm-none-eabi-gcc` **14.x** on PATH).
A Nanotec Plug & Drive install may put GCC 4.8.4 first — put the 14.2 `bin` directory in front:

`C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1\bin`

From the repo root. CMake 4 resolves `--toolchain` relative to the build directory, so pass an absolute path:

```bash
cmake -S firmware -B build-firmware -G Ninja --toolchain "$PWD/firmware/cmake/arm-none-eabi-stm32f446.cmake"
cmake --build build-firmware
```

On Windows PowerShell:

```powershell
cmake -S firmware -B build-firmware -G Ninja --toolchain "$PWD\firmware\cmake\arm-none-eabi-stm32f446.cmake"
cmake --build build-firmware
```

Outputs: `build-firmware/tec_firmware.elf`, `.hex`, `.bin`.

Flash with STM32CubeProgrammer or OpenOCD (ST-Link).

## CubeMX

`TEC_Abzug_F446.ioc` records the pinout. Application code lives in this CMake tree; regenerate HAL only if you bump Cube packages. Canonical pins: `include/PinMap.hpp`.
