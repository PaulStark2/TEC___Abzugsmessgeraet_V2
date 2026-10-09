#pragma once

// Local copy of the pins used by this bench test.
// Origin: firmware/include/PinMap.hpp and .cursor/plans/m19.x_stm32_plan_5f56eecd.plan.md
// as of 2026-10-06. Do not include the production header. This file is not
// updated when the production map changes.

#include "stm32f4xx_hal.h"

// Force ADC channels. Voltage display only: counts * 3.3 / 4095.
#define PIN_FORCE_FX_PORT GPIOA
#define PIN_FORCE_FX_PIN GPIO_PIN_0
#define PIN_FORCE_FY_PORT GPIOA
#define PIN_FORCE_FY_PIN GPIO_PIN_1
#define PIN_FORCE_FZ_PORT GPIOA
#define PIN_FORCE_FZ_PIN GPIO_PIN_4

// ST-Link virtual COM port (USART2). SB13/SB14 stay closed.
// The production PinMap mentions these pins in a comment and does not macro them.
#define PIN_VCP_TX_PORT GPIOA
#define PIN_VCP_TX_PIN GPIO_PIN_2
#define PIN_VCP_RX_PORT GPIOA
#define PIN_VCP_RX_PIN GPIO_PIN_3

// USART3 to the Raspberry Pi (AF7, 115200 8N1).
#define PIN_UART_TX_PORT GPIOC
#define PIN_UART_TX_PIN GPIO_PIN_10
#define PIN_UART_RX_PORT GPIOC
#define PIN_UART_RX_PIN GPIO_PIN_11

// Probe NC: idle High, contact Low. Internal pull-down. Reported as High/Low.
#define PIN_PROBE_PORT GPIOA
#define PIN_PROBE_PIN GPIO_PIN_5

// Limit switches: High = triggered. Internal pull-down.
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

// E-Stop input: High = active. Hardware also cuts motor and pneumatic power.
#define PIN_ESTOP_PORT GPIOA
#define PIN_ESTOP_PIN GPIO_PIN_9

// Step PWM. Y is PB4 (TIM3_CH1); configuring it releases NJTRST and keeps SWD.
#define PIN_STEP_Y_PORT GPIOB
#define PIN_STEP_Y_PIN GPIO_PIN_4
#define PIN_STEP_X_PORT GPIOB
#define PIN_STEP_X_PIN GPIO_PIN_6
#define PIN_STEP_Z_PORT GPIOB
#define PIN_STEP_Z_PIN GPIO_PIN_8

// Direction GPIO. High = positive machine direction.
#define PIN_DIR_X_PORT GPIOC
#define PIN_DIR_X_PIN GPIO_PIN_1
#define PIN_DIR_Y_PORT GPIOC
#define PIN_DIR_Y_PIN GPIO_PIN_3
#define PIN_DIR_Z_PORT GPIOC
#define PIN_DIR_Z_PIN GPIO_PIN_5

// Pneumatics are not driven by this test. Held Low (de-energized). Never both High.
#define PIN_SPREADER_OPEN_PORT GPIOC
#define PIN_SPREADER_OPEN_PIN GPIO_PIN_8
#define PIN_SPREADER_CLOSE_PORT GPIOC
#define PIN_SPREADER_CLOSE_PIN GPIO_PIN_12
#define PIN_FIXTURE_PORT GPIOC
#define PIN_FIXTURE_PIN GPIO_PIN_9

// Heartbeat LED. Nucleo LD2 on PA5 is the probe input and is not used as an LED.
#define PIN_DEBUG_LED_PORT GPIOC
#define PIN_DEBUG_LED_PIN GPIO_PIN_7
