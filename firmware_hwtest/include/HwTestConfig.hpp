#pragma once

#include <cstdint>

// Change this single value and rebuild to select the serial console.
// Both:     laptop ST-Link (USART2) and Raspberry Pi (USART3) together.
// Usart2Only: laptop only. Usart3Only: Pi only.
enum class Console : std::uint8_t { Both = 0, Usart2Only = 1, Usart3Only = 2 };

constexpr Console kConsole = Console::Both;

// Step pulse rate for levels 1..5. Index 0 is unused.
// Mechanical scale of this bench (not used as a control loop): SFU1204, 1/4 microstep,
// 200 steps/mm. mm/s = kSpeedHz[level] / kStepsPerMm.
constexpr std::uint32_t kStepsPerMm = 200;
constexpr std::uint16_t kSpeedHz[6] = {0, 100, 200, 500, 1000, 2000};

constexpr std::uint32_t kStatusPeriodMs = 500;
constexpr std::uint32_t kUartBaud = 115200;
