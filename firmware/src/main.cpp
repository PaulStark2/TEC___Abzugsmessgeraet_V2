#include "stm32f4xx_hal.h"

#include "FirmwareHost.hpp"
#include "HostIo.hpp"
#include "PinMap.hpp"
#include "StepGenerator.hpp"
#include "Types.hpp"

static IWDG_HandleTypeDef g_iwdg;
static FirmwareHost g_host;
static volatile uint8_t g_ms_elapsed;

extern "C" void Error_Handler(void)
{
    __disable_irq();
    for (;;)
    {
    }
}

extern "C" void SysTick_Handler(void)
{
    HAL_IncTick();
    g_ms_elapsed = 1;
}

extern "C" void NMI_Handler(void)
{
}

extern "C" void HardFault_Handler(void)
{
    for (;;)
    {
    }
}

extern "C" void MemManage_Handler(void)
{
    for (;;)
    {
    }
}

extern "C" void BusFault_Handler(void)
{
    for (;;)
    {
    }
}

extern "C" void UsageFault_Handler(void)
{
    for (;;)
    {
    }
}

extern "C" void SVC_Handler(void)
{
}

extern "C" void DebugMon_Handler(void)
{
}

extern "C" void PendSV_Handler(void)
{
}

static void SystemClock_Config(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitTypeDef osc{};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_LSI;
    osc.HSEState = RCC_HSE_ON;
    osc.LSIState = RCC_LSI_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 8;
    osc.PLL.PLLN = 360;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 7;
    osc.PLL.PLLR = 2;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_PWREx_EnableOverDrive() != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitTypeDef clk{};
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK)
    {
        Error_Handler();
    }
}

static void Gpio_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef gpio{};

    gpio.Pin = PIN_FORCE_FX_PIN | PIN_FORCE_FY_PIN | PIN_FORCE_FZ_PIN;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = PIN_PROBE_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(PIN_PROBE_PORT, &gpio);

    gpio.Pin = PIN_LIM_XP_PIN | PIN_LIM_YP_PIN | PIN_LIM_ZP_PIN | PIN_ESTOP_PIN | PIN_LIM_XN_PIN |
               PIN_LIM_YN_PIN | PIN_LIM_ZN_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOA, &gpio);

    HAL_GPIO_WritePin(PIN_DIR_X_PORT, PIN_DIR_X_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PIN_DIR_Y_PORT, PIN_DIR_Y_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PIN_DIR_Z_PORT, PIN_DIR_Z_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PIN_SPREADER_OPEN_PORT, PIN_SPREADER_OPEN_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PIN_SPREADER_CLOSE_PORT, PIN_SPREADER_CLOSE_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PIN_FIXTURE_PORT, PIN_FIXTURE_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PIN_DEBUG_LED_PORT, PIN_DEBUG_LED_PIN, GPIO_PIN_RESET);

    gpio.Pin = PIN_DIR_X_PIN | PIN_DIR_Y_PIN | PIN_DIR_Z_PIN | PIN_SPREADER_OPEN_PIN | PIN_SPREADER_CLOSE_PIN |
               PIN_FIXTURE_PIN | PIN_DEBUG_LED_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
}

static void Iwdg_Init(void)
{
    g_iwdg.Instance = IWDG;
    g_iwdg.Init.Prescaler = IWDG_PRESCALER_32;
    g_iwdg.Init.Reload = 1000;
    if (HAL_IWDG_Init(&g_iwdg) != HAL_OK)
    {
        Error_Handler();
    }
}

extern "C" void HAL_MspInit(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    Gpio_Init();
    Iwdg_Init();
    hostio::init();
    stepgen::init();

    for (;;)
    {
        if (g_ms_elapsed != 0)
        {
            g_ms_elapsed = 0;
            hostio::serviceProtocol(g_host);
            std::uint16_t adc_x = 0;
            std::uint16_t adc_y = 0;
            std::uint16_t adc_z = 0;
            hostio::latestAdc(adc_x, adc_y, adc_z);
            const HostInputs inputs = hostio::sampleInputs(
                g_host.nextForceSequence(), g_host.heartbeatSequence(),
                g_host.machine().machineConfig().force.range_N);
            g_host.tickOneMillisecond(inputs, adc_x, adc_y, adc_z);
            hostio::applyOutputs(g_host.machine());
            stepgen::tickMillisecond();

            const bool fault = g_host.machine().machineState() == MachineState::Fault;
            const bool estop = inputs.e_stop;
            if (fault || estop)
            {
                stepgen::emergencyStop();
            }
            else
            {
                stepgen::followRawPose(g_host.machine().rawX(), g_host.machine().rawY(),
                                      g_host.machine().rawZ());
            }

            const std::uint32_t ticks = g_host.tickCount();
            const std::uint32_t blink_period = fault ? 100u : 500u;
            if ((ticks % blink_period) == 0u)
            {
                HAL_GPIO_TogglePin(PIN_DEBUG_LED_PORT, PIN_DEBUG_LED_PIN);
            }
            HAL_IWDG_Refresh(&g_iwdg);
        }
    }
}
