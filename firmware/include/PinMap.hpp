#pragma once

// Canonical STM32F446RE pin map for M19.x Phase 2.1+.
// HAL code must follow these macros. CubeMX .ioc mirrors this table.
// Nucleo LD2 (PA5) is NOT used: PA5 is the probe input.

#include "stm32f4xx_hal.h"

// Force ADC channels (DMA conversion from Phase 2).
#define PIN_FORCE_FX_PORT GPIOA
#define PIN_FORCE_FX_PIN GPIO_PIN_0
#define PIN_FORCE_FY_PORT GPIOA
#define PIN_FORCE_FY_PIN GPIO_PIN_1
#define PIN_FORCE_FZ_PORT GPIOA
#define PIN_FORCE_FZ_PIN GPIO_PIN_4

// Host UART: compile-time select via CMake TEC_HOST_UART=USART2|USART3.
// USART2 = ST-Link VCP PA2/PA3 (bring-up). USART3 = Pi PC10/PC11 (production).
// SB13/SB14 stay closed. Exactly one host UART — never dual M12.
#if defined(TEC_HOST_UART_USART2)
#define PIN_UART_TX_PORT GPIOA
#define PIN_UART_TX_PIN GPIO_PIN_2
#define PIN_UART_RX_PORT GPIOA
#define PIN_UART_RX_PIN GPIO_PIN_3
#define TEC_HOST_USART USART2
#define TEC_HOST_USART_IRQn USART2_IRQn
#define TEC_HOST_USART_IRQHandler USART2_IRQHandler
#define TEC_HOST_USART_AF GPIO_AF7_USART2
#define TEC_HOST_USART_CLK_ENABLE() __HAL_RCC_USART2_CLK_ENABLE()
#elif defined(TEC_HOST_UART_USART3)
#define PIN_UART_TX_PORT GPIOC
#define PIN_UART_TX_PIN GPIO_PIN_10
#define PIN_UART_RX_PORT GPIOC
#define PIN_UART_RX_PIN GPIO_PIN_11
#define TEC_HOST_USART USART3
#define TEC_HOST_USART_IRQn USART3_IRQn
#define TEC_HOST_USART_IRQHandler USART3_IRQHandler
#define TEC_HOST_USART_AF GPIO_AF7_USART3
#define TEC_HOST_USART_CLK_ENABLE() __HAL_RCC_USART3_CLK_ENABLE()
#else
#error "Define TEC_HOST_UART_USART2 or TEC_HOST_UART_USART3 (CMake TEC_HOST_UART)"
#endif

// Probe NC: idle High, contact Low. Internal pull-down.
// HostIo sets probeTriggered level only; FirmwareHost must latch machine pose +
// sequence on the contact edge or Core ProbeSearch ignores the event
// (docs/M19_ProbeCalibration.md). Nucleo LD2 often shares PA5 — disconnect SB.
#define PIN_PROBE_PORT GPIOA
#define PIN_PROBE_PIN GPIO_PIN_5

// Limit switches: High = triggered. +Max is homing reference.
#define PIN_LIM_XP_PORT GPIOA
#define PIN_LIM_XP_PIN GPIO_PIN_6
#define PIN_LIM_YP_PORT GPIOA
#define PIN_LIM_YP_PIN GPIO_PIN_7
#define PIN_LIM_ZP_PORT GPIOA
#define PIN_LIM_ZP_PIN GPIO_PIN_8
#define PIN_LIM_XN_PORT GPIOA
#define PIN_LIM_XN_PIN GPIO_PIN_10
#define PIN_LIM_YN_PORT GPIOA
#define PIN_LIM_YN_PIN GPIO_PIN_11
#define PIN_LIM_ZN_PORT GPIOA
#define PIN_LIM_ZN_PIN GPIO_PIN_12

// E-Stop input: High = active. Hardware also cuts motor/pneumatic power.
#define PIN_ESTOP_PORT GPIOA
#define PIN_ESTOP_PIN GPIO_PIN_9

// Step outputs (GPIO pulses Phase 3–4; TIM AF optional later).
// X=PB6, Y=PB4, Z=PB8 — Morpho pin numbers in firmware/README.md.
#define PIN_STEP_Y_PORT GPIOB
#define PIN_STEP_Y_PIN GPIO_PIN_4
#define PIN_STEP_X_PORT GPIOB
#define PIN_STEP_X_PIN GPIO_PIN_6
#define PIN_STEP_Z_PORT GPIOB
#define PIN_STEP_Z_PIN GPIO_PIN_8

// Direction GPIO. High = positive machine direction (firmware-invertible later).
#define PIN_DIR_X_PORT GPIOC
#define PIN_DIR_X_PIN GPIO_PIN_1
#define PIN_DIR_Y_PORT GPIOC
#define PIN_DIR_Y_PIN GPIO_PIN_3
#define PIN_DIR_Z_PORT GPIOC
#define PIN_DIR_Z_PIN GPIO_PIN_5

// Double-acting spreader: always a complementary pair. Never both High.
#define PIN_SPREADER_OPEN_PORT GPIOC
#define PIN_SPREADER_OPEN_PIN GPIO_PIN_8
#define PIN_SPREADER_CLOSE_PORT GPIOC
#define PIN_SPREADER_CLOSE_PIN GPIO_PIN_12

// Fixture (single valve). 3.3 V to high-side driver. Low = off at boot.
#define PIN_FIXTURE_PORT GPIOC
#define PIN_FIXTURE_PIN GPIO_PIN_9

// Heartbeat LED on a reserved pin (not the Nucleo LD2).
#define PIN_DEBUG_LED_PORT GPIOC
#define PIN_DEBUG_LED_PIN GPIO_PIN_7

// Open-loop mm↔step per axis. Default = Zielkinematik (57HS56, 1/4 µstep, SFU1204 → 200).
// Bench Zortrax: XY belt = 20. Z screw = 200 (4 mm lead, 1/4 µstep, measured 2026-10-15).
#ifndef TEC_STEPS_PER_MM
#define TEC_STEPS_PER_MM 200
#endif
#ifndef TEC_STEPS_PER_MM_X
#define TEC_STEPS_PER_MM_X TEC_STEPS_PER_MM
#endif
#ifndef TEC_STEPS_PER_MM_Y
#define TEC_STEPS_PER_MM_Y TEC_STEPS_PER_MM
#endif
#ifndef TEC_STEPS_PER_MM_Z
#define TEC_STEPS_PER_MM_Z TEC_STEPS_PER_MM
#endif
constexpr float kStepsPerMmX = static_cast<float>(TEC_STEPS_PER_MM_X);
constexpr float kStepsPerMmY = static_cast<float>(TEC_STEPS_PER_MM_Y);
constexpr float kStepsPerMmZ = static_cast<float>(TEC_STEPS_PER_MM_Z);
constexpr float kStepsPerMm[3] = {kStepsPerMmX, kStepsPerMmY, kStepsPerMmZ};
