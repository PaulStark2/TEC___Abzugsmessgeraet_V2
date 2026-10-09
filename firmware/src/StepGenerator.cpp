#include "StepGenerator.hpp"

#include "PinMap.hpp"

#include "stm32f4xx_hal.h"

#include <cmath>
#include <cstdint>

extern "C" void Error_Handler(void);

constexpr std::uint32_t kMaxHzXy = 10000u;
constexpr std::uint32_t kMaxHzZ = 5000u;
constexpr std::uint32_t kMinHz = 200u;
constexpr bool kInvertDir[3] = {false, false, false};

struct AxisHw
{
    GPIO_TypeDef* step_port;
    std::uint16_t step_pin;
    std::uint32_t gpio_af;
    GPIO_TypeDef* dir_port;
    std::uint16_t dir_pin;
    TIM_TypeDef* tim;
    IRQn_Type irqn;
    std::uint32_t max_hz;
    bool apb1_timer;
};

// X TIM4_CH1 PB6 AF2; Y TIM3_CH1 PB4 AF2; Z TIM10_CH1 PB8 AF3
constexpr AxisHw kHw[3] = {
    {PIN_STEP_X_PORT, PIN_STEP_X_PIN, GPIO_AF2_TIM4, PIN_DIR_X_PORT, PIN_DIR_X_PIN, TIM4, TIM4_IRQn, kMaxHzXy,
     true},
    {PIN_STEP_Y_PORT, PIN_STEP_Y_PIN, GPIO_AF2_TIM3, PIN_DIR_Y_PORT, PIN_DIR_Y_PIN, TIM3, TIM3_IRQn, kMaxHzXy,
     true},
    {PIN_STEP_Z_PORT, PIN_STEP_Z_PIN, GPIO_AF3_TIM10, PIN_DIR_Z_PORT, PIN_DIR_Z_PIN, TIM10, TIM1_UP_TIM10_IRQn,
     kMaxHzZ, false},
};

struct AxisState
{
    std::int32_t current = 0;
    std::int32_t target = 0;
    bool dir_positive = true;
    bool pwm_running = false;
    std::uint32_t last_hz = 0;
    TIM_HandleTypeDef htim{};
};

static AxisState g_axis[3]{};
static bool g_active = false;
static bool g_inited = false;
static bool g_test_tone = false;
static stepgen::Axis g_test_axis = stepgen::Axis::X;
static std::uint32_t g_test_remaining_ms = 0;
static std::uint32_t g_pulse_count = 0;
static std::uint32_t g_tim_clk_apb1 = 90000000u;
static std::uint32_t g_tim_clk_apb2 = 180000000u;

static std::uint8_t axisIndex(stepgen::Axis a)
{
    return static_cast<std::uint8_t>(a);
}

static std::int32_t mmToSteps(float mm)
{
    return static_cast<std::int32_t>(std::lround(static_cast<double>(mm) * static_cast<double>(kStepsPerMm)));
}

static void busyWaitUs(unsigned us)
{
    if (us == 0u)
    {
        return;
    }
    const std::uint32_t cycles_per_us = SystemCoreClock / 1000000u;
    const std::uint32_t start = DWT->CYCCNT;
    const std::uint32_t wait = us * cycles_per_us;
    while ((DWT->CYCCNT - start) < wait)
    {
    }
}

static void enableCycleCounter()
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static std::uint32_t busTimerClockHz(bool apb1_timer)
{
    if (apb1_timer)
    {
        const std::uint32_t pclk = HAL_RCC_GetPCLK1Freq();
        const std::uint32_t ppre = (RCC->CFGR & RCC_CFGR_PPRE1) >> RCC_CFGR_PPRE1_Pos;
        return (ppre < 4u) ? pclk : (pclk * 2u);
    }
    const std::uint32_t pclk = HAL_RCC_GetPCLK2Freq();
    const std::uint32_t ppre = (RCC->CFGR & RCC_CFGR_PPRE2) >> RCC_CFGR_PPRE2_Pos;
    return (ppre < 4u) ? pclk : (pclk * 2u);
}

static void setDir(std::uint8_t i, bool positive_machine)
{
    const bool pin_high = kInvertDir[i] ? !positive_machine : positive_machine;
    HAL_GPIO_WritePin(kHw[i].dir_port, kHw[i].dir_pin, pin_high ? GPIO_PIN_SET : GPIO_PIN_RESET);
    g_axis[i].dir_positive = positive_machine;
}

static void stopPwm(std::uint8_t i)
{
    AxisState& st = g_axis[i];
    if (!st.pwm_running)
    {
        return;
    }
    HAL_TIM_PWM_Stop(&st.htim, TIM_CHANNEL_1);
    __HAL_TIM_DISABLE_IT(&st.htim, TIM_IT_UPDATE);
    st.pwm_running = false;
    st.last_hz = 0;
}

static void applyPwmHz(std::uint8_t i, std::uint32_t hz)
{
    AxisState& st = g_axis[i];
    const std::uint32_t max_hz = kHw[i].max_hz;
    if (hz < kMinHz)
    {
        hz = kMinHz;
    }
    if (hz > max_hz)
    {
        hz = max_hz;
    }
    if (st.pwm_running && st.last_hz == hz)
    {
        return;
    }

    const std::uint32_t tim_clk = kHw[i].apb1_timer ? g_tim_clk_apb1 : g_tim_clk_apb2;
    const std::uint32_t psc = (tim_clk / 1000000u);
    const std::uint32_t psc_reg = (psc > 0u) ? (psc - 1u) : 0u;
    const std::uint32_t tick_hz = tim_clk / (psc_reg + 1u);
    const std::uint32_t arr = (tick_hz / hz) - 1u;
    const std::uint32_t ccr = (arr + 1u) / 2u; // 50 % duty

    __HAL_TIM_SET_PRESCALER(&st.htim, psc_reg);
    __HAL_TIM_SET_AUTORELOAD(&st.htim, arr);
    __HAL_TIM_SET_COMPARE(&st.htim, TIM_CHANNEL_1, ccr);
    st.last_hz = hz;

    if (!st.pwm_running)
    {
        __HAL_TIM_SET_COUNTER(&st.htim, 0);
        if (HAL_TIM_PWM_Start(&st.htim, TIM_CHANNEL_1) != HAL_OK)
        {
            Error_Handler();
        }
        __HAL_TIM_ENABLE_IT(&st.htim, TIM_IT_UPDATE);
        st.pwm_running = true;
    }
}

static void chaseAxis(std::uint8_t i)
{
    AxisState& st = g_axis[i];
    const std::int32_t err = st.target - st.current;
    if (err == 0)
    {
        stopPwm(i);
        return;
    }

    const bool positive = err > 0;
    if (positive != st.dir_positive)
    {
        stopPwm(i);
        setDir(i, positive);
        busyWaitUs(10);
    }

    std::uint32_t abs_err = static_cast<std::uint32_t>(err > 0 ? err : -err);
    applyPwmHz(i, abs_err * 1000u);
}

static void onTimerUpdate(TIM_TypeDef* instance)
{
    std::uint8_t i = 255;
    if (instance == TIM4)
    {
        i = 0;
    }
    else if (instance == TIM3)
    {
        i = 1;
    }
    else if (instance == TIM10)
    {
        i = 2;
    }
    else
    {
        return;
    }

    ++g_pulse_count;

    if (g_test_tone && axisIndex(g_test_axis) == i)
    {
        return;
    }

    AxisState& st = g_axis[i];
    if (st.current < st.target)
    {
        ++st.current;
    }
    else if (st.current > st.target)
    {
        --st.current;
    }

    if (st.current == st.target)
    {
        stopPwm(i);
    }
}

static bool anyLag()
{
    for (std::uint8_t i = 0; i < 3; ++i)
    {
        if (g_axis[i].current != g_axis[i].target || g_axis[i].pwm_running)
        {
            return true;
        }
    }
    return false;
}

namespace stepgen
{

void init()
{
    if (g_inited)
    {
        return;
    }

    enableCycleCounter();
    g_tim_clk_apb1 = busTimerClockHz(true);
    g_tim_clk_apb2 = busTimerClockHz(false);

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    for (std::uint8_t i = 0; i < 3; ++i)
    {
        const AxisHw& hw = kHw[i];
        if (hw.tim == TIM4)
        {
            __HAL_RCC_TIM4_CLK_ENABLE();
        }
        else if (hw.tim == TIM3)
        {
            __HAL_RCC_TIM3_CLK_ENABLE();
        }
        else if (hw.tim == TIM10)
        {
            __HAL_RCC_TIM10_CLK_ENABLE();
        }

        GPIO_InitTypeDef gpio{};
        gpio.Pin = hw.step_pin;
        gpio.Mode = GPIO_MODE_AF_PP;
        gpio.Pull = GPIO_NOPULL;
        gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        gpio.Alternate = hw.gpio_af;
        HAL_GPIO_Init(hw.step_port, &gpio);

        g_axis[i].htim.Instance = hw.tim;
        g_axis[i].htim.Init.Prescaler = 89;
        g_axis[i].htim.Init.CounterMode = TIM_COUNTERMODE_UP;
        g_axis[i].htim.Init.Period = 999;
        g_axis[i].htim.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
        g_axis[i].htim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
        if (HAL_TIM_PWM_Init(&g_axis[i].htim) != HAL_OK)
        {
            Error_Handler();
        }

        TIM_OC_InitTypeDef oc{};
        oc.OCMode = TIM_OCMODE_PWM1;
        oc.Pulse = 500;
        oc.OCPolarity = TIM_OCPOLARITY_HIGH;
        oc.OCFastMode = TIM_OCFAST_DISABLE;
        if (HAL_TIM_PWM_ConfigChannel(&g_axis[i].htim, &oc, TIM_CHANNEL_1) != HAL_OK)
        {
            Error_Handler();
        }

        HAL_NVIC_SetPriority(hw.irqn, 3, 0);
        HAL_NVIC_EnableIRQ(hw.irqn);
        setDir(i, true);
        g_axis[i].current = 0;
        g_axis[i].target = 0;
        g_axis[i].dir_positive = true;
        g_axis[i].pwm_running = false;
        g_axis[i].last_hz = 0;
    }

    g_active = false;
    g_test_tone = false;
    g_test_remaining_ms = 0;
    g_pulse_count = 0;
    g_inited = true;
}

void followRawPose(float raw_x_mm, float raw_y_mm, float raw_z_mm)
{
    if (g_test_tone)
    {
        return;
    }

    g_axis[0].target = mmToSteps(raw_x_mm);
    g_axis[1].target = mmToSteps(raw_y_mm);
    g_axis[2].target = mmToSteps(raw_z_mm);

    chaseAxis(0);
    chaseAxis(1);
    chaseAxis(2);
    g_active = anyLag();
}

void stopAndResync(float raw_x_mm, float raw_y_mm, float raw_z_mm)
{
    stopTestTone();
    for (std::uint8_t i = 0; i < 3; ++i)
    {
        stopPwm(i);
    }
    g_axis[0].current = g_axis[0].target = mmToSteps(raw_x_mm);
    g_axis[1].current = g_axis[1].target = mmToSteps(raw_y_mm);
    g_axis[2].current = g_axis[2].target = mmToSteps(raw_z_mm);
    g_active = false;
}

void emergencyStop()
{
    stopTestTone();
    for (std::uint8_t i = 0; i < 3; ++i)
    {
        stopPwm(i);
    }
    g_active = false;
}

void startTestTone(Axis axis, std::uint32_t duration_ms)
{
    if (duration_ms == 0u)
    {
        duration_ms = 3000u;
    }
    stopTestTone();
    g_test_axis = axis;
    g_test_tone = true;
    g_test_remaining_ms = duration_ms;
    g_active = true;
    applyPwmHz(axisIndex(axis), 2000u);
}

void stopTestTone()
{
    if (!g_test_tone)
    {
        return;
    }
    stopPwm(axisIndex(g_test_axis));
    g_test_tone = false;
    g_test_remaining_ms = 0;
    g_active = anyLag();
}

bool testToneActive()
{
    return g_test_tone;
}

void tickMillisecond()
{
    if (!g_test_tone)
    {
        return;
    }
    if (g_test_remaining_ms > 0u)
    {
        --g_test_remaining_ms;
    }
    if (g_test_remaining_ms == 0u)
    {
        stopTestTone();
    }
}

std::int32_t stepCount(Axis axis)
{
    return g_axis[axisIndex(axis)].current;
}

std::int32_t targetCount(Axis axis)
{
    return g_axis[axisIndex(axis)].target;
}

bool outputsActive()
{
    return g_active || g_test_tone;
}

std::uint32_t pulseCount()
{
    return g_pulse_count;
}

} // namespace stepgen

extern "C" void TIM4_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&g_axis[0].htim);
}

extern "C" void TIM3_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&g_axis[1].htim);
}

extern "C" void TIM1_UP_TIM10_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&g_axis[2].htim);
}

extern "C" void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef* htim)
{
    if (htim == nullptr || htim->Instance == nullptr)
    {
        return;
    }
    onTimerUpdate(htim->Instance);
}
