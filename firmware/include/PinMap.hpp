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

// USART2 PA2/PA3 stays on the ST-Link VCP (SB13/SB14 closed). Not the Pi link.

// USART3 to Raspberry Pi (AF7, 115200 8N1).
#define PIN_UART_TX_PORT GPIOC
#define PIN_UART_TX_PIN GPIO_PIN_10
#define PIN_UART_RX_PORT GPIOC
#define PIN_UART_RX_PIN GPIO_PIN_11

// Probe NC: idle High, contact Low. Internal pull-down. Polled in Phase 2 (EXTI latch later).
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

// Step PWM (held low until Phase 3; TIM3/4/10 later).
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

// Open-loop conversion used by later step generators (not applied in Phase 2.1).
constexpr float kStepsPerMm = 200.0f;
