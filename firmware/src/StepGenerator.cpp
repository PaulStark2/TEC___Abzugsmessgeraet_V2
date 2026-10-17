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

// One-pulse PWM mode 2: the pin stays low until CCR, then high until the
// period ends. The rising edge is the motor step. The update event only
// falls the pin and stops the counter (OPM), so stopping there cannot emit
// a second step. The next pulse starts only when software sets CEN again.
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
    volatile std::int32_t current = 0;
    volatile std::int32_t target = 0;
    volatile bool dir_positive = true;
    volatile bool pulse_active = false;
    volatile bool block_rearm = false;
    volatile bool abort = false;
    volatile bool rise_seen = false;
    std::uint32_t last_hz = 0;
    TIM_HandleTypeDef htim{};
};

static AxisState g_axis[3]{};
static bool g_active = false;
static bool g_inited = false;
static volatile bool g_test_tone = false;
static stepgen::Axis g_test_axis = stepgen::Axis::X;
static std::uint32_t g_test_remaining_ms = 0;
static volatile std::uint32_t g_pulse_count = 0;
static std::uint32_t g_tim_clk_apb1 = 90000000u;
static std::uint32_t g_tim_clk_apb2 = 180000000u;

static std::uint8_t axisIndex(stepgen::Axis a)
{
    return static_cast<std::uint8_t>(a);
}

static int axisFromTimer(TIM_TypeDef* tim)
{
    if (tim == TIM4)
    {
        return 0;
    }
    if (tim == TIM3)
    {
        return 1;
    }
    if (tim == TIM10)
    {
        return 2;
    }
    return -1;
}

static std::int32_t mmToSteps(std::uint8_t axis, float mm)
{
    return static_cast<std::int32_t>(
        std::lround(static_cast<double>(mm) * static_cast<double>(kStepsPerMm[axis])));
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

static void setOc1Mode(TIM_TypeDef* tim, std::uint32_t mode)
{
    std::uint32_t ccmr = tim->CCMR1;
    ccmr &= ~TIM_CCMR1_OC1M;
    ccmr |= (mode & TIM_CCMR1_OC1M);
    tim->CCMR1 = ccmr;
}

// Drive STEP low and leave the timer stopped. Caller masks IRQs when racing the pulse ISR.
static void silenceAxis(std::uint8_t i)
{
    AxisState& st = g_axis[i];
    TIM_TypeDef* tim = kHw[i].tim;
    st.abort = true;
    st.block_rearm = true;
    tim->CR1 &= ~TIM_CR1_CEN;
    tim->CNT = 0;
    setOc1Mode(tim, TIM_OCMODE_FORCED_INACTIVE);
    tim->SR = 0;
    st.pulse_active = false;
    st.last_hz = 0;
    st.abort = false;
    st.block_rearm = false;
    setOc1Mode(tim, TIM_OCMODE_PWM2);
}

static std::uint32_t clampHz(std::uint8_t i, std::uint32_t hz)
{
    const std::uint32_t max_hz = kHw[i].max_hz;
    if (hz < kMinHz)
    {
        hz = kMinHz;
    }
    if (hz > max_hz)
    {
        hz = max_hz;
    }
    return hz;
}

// Start exactly one PWM2 period. Timer must be stopped. Rising edge is at CCR.
static void armPulse(std::uint8_t i, std::uint32_t hz)
{
    AxisState& st = g_axis[i];
    TIM_TypeDef* tim = kHw[i].tim;
    hz = clampHz(i, hz);

    const std::uint32_t tim_clk = kHw[i].apb1_timer ? g_tim_clk_apb1 : g_tim_clk_apb2;
    const std::uint32_t psc = tim_clk / 1000000u;
    const std::uint32_t psc_reg = (psc > 0u) ? (psc - 1u) : 0u;
    const std::uint32_t tick_hz = tim_clk / (psc_reg + 1u);
    const std::uint32_t arr = (tick_hz / hz) - 1u;
    const std::uint32_t ccr = (arr + 1u) / 2u;

    tim->CR1 &= ~TIM_CR1_CEN;
    tim->ARR = arr;
    tim->CCR1 = ccr;
    tim->CNT = 0;
    setOc1Mode(tim, TIM_OCMODE_PWM2);
    tim->CCER |= TIM_CCER_CC1E;
    tim->SR = 0;
    st.last_hz = hz;
    st.abort = false;
    st.rise_seen = false;
    st.pulse_active = true;
    tim->CR1 |= TIM_CR1_OPM;
    tim->CR1 |= TIM_CR1_CEN;
}

static std::uint32_t hzForError(std::uint8_t i)
{
    const std::int32_t err = g_axis[i].target - g_axis[i].current;
    if (err == 0)
    {
        return 0u;
    }
    const std::uint32_t abs_err = static_cast<std::uint32_t>(err > 0 ? err : -err);
    const std::uint32_t max_hz = kHw[i].max_hz;
    if (abs_err > (max_hz / 1000u))
    {
        return max_hz;
    }
    return abs_err * 1000u;
}

static void chaseAxis(std::uint8_t i)
{
    AxisState& st = g_axis[i];
    const std::int32_t err = st.target - st.current;
    if (err != 0)
    {
        const bool positive = err > 0;
        if (positive != st.dir_positive)
        {
            st.block_rearm = true;
            if (st.pulse_active)
            {
                return;
            }
            setDir(i, positive);
            busyWaitUs(10);
        }
    }
    st.block_rearm = false;
    if (err == 0 || st.pulse_active)
    {
        return;
    }
    armPulse(i, hzForError(i));
}

static void onPulseRise(TIM_TypeDef* instance)
{
    const int index = axisFromTimer(instance);
    if (index < 0)
    {
        return;
    }
    AxisState& st = g_axis[static_cast<std::uint8_t>(index)];
    if (!st.pulse_active || st.abort || st.rise_seen)
    {
        return;
    }
    st.rise_seen = true;

    ++g_pulse_count;
    if (g_test_tone && axisIndex(g_test_axis) == static_cast<std::uint8_t>(index))
    {
        return;
    }
    if (st.current < st.target)
    {
        ++st.current;
    }
    else if (st.current > st.target)
    {
        --st.current;
    }
}

static void onPulseEnd(TIM_TypeDef* instance)
{
    const int index = axisFromTimer(instance);
    if (index < 0)
    {
        return;
    }
    const auto i = static_cast<std::uint8_t>(index);
    AxisState& st = g_axis[i];
    if (!st.pulse_active || st.abort)
    {
        st.pulse_active = false;
        return;
    }

    if (g_test_tone && axisIndex(g_test_axis) == i)
    {
        if (g_test_tone)
        {
            armPulse(i, 2000u);
        }
        else
        {
            st.pulse_active = false;
        }
        return;
    }

    const std::int32_t err = st.target - st.current;
    const bool positive = err > 0;
    if (err == 0 || st.block_rearm || positive != st.dir_positive)
    {
        st.pulse_active = false;
        if (err != 0 && positive != st.dir_positive)
        {
            st.block_rearm = true;
        }
        return;
    }
    armPulse(i, hzForError(i));
}

static bool anyLag()
{
    for (std::uint8_t i = 0; i < 3; ++i)
    {
        if (g_axis[i].current != g_axis[i].target || g_axis[i].pulse_active)
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

        const std::uint32_t tim_clk = hw.apb1_timer ? g_tim_clk_apb1 : g_tim_clk_apb2;
        const std::uint32_t psc = tim_clk / 1000000u;
        const std::uint32_t psc_reg = (psc > 0u) ? (psc - 1u) : 0u;

        g_axis[i].htim.Instance = hw.tim;
        g_axis[i].htim.Init.Prescaler = psc_reg;
        g_axis[i].htim.Init.CounterMode = TIM_COUNTERMODE_UP;
        g_axis[i].htim.Init.Period = 999;
        g_axis[i].htim.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
        g_axis[i].htim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
        if (HAL_TIM_PWM_Init(&g_axis[i].htim) != HAL_OK)
        {
            Error_Handler();
        }

        TIM_OC_InitTypeDef oc{};
        oc.OCMode = TIM_OCMODE_PWM2;
        oc.Pulse = 500;
        oc.OCPolarity = TIM_OCPOLARITY_HIGH;
        oc.OCFastMode = TIM_OCFAST_DISABLE;
        if (HAL_TIM_PWM_ConfigChannel(&g_axis[i].htim, &oc, TIM_CHANNEL_1) != HAL_OK)
        {
            Error_Handler();
        }

        // CCR and ARR are written only while the timer is stopped. Preload would
        // keep the previous period for the pulse that is about to start.
        hw.tim->CCMR1 &= ~TIM_CCMR1_OC1PE;
        hw.tim->CCER &= ~TIM_CCER_CC1E;
        hw.tim->PSC = psc_reg;
        hw.tim->EGR = TIM_EGR_UG;
        hw.tim->SR = 0;
        hw.tim->CNT = 0;
        setOc1Mode(hw.tim, TIM_OCMODE_PWM2);
        hw.tim->CCER |= TIM_CCER_CC1E;
        // This HAL's PWM_Init does not write OnePulseMode. OPM makes the
        // update event stop the counter after a single period.
        hw.tim->CR1 |= TIM_CR1_OPM;

        __HAL_TIM_ENABLE_IT(&g_axis[i].htim, TIM_IT_CC1);
        __HAL_TIM_ENABLE_IT(&g_axis[i].htim, TIM_IT_UPDATE);

        // Below host UART (prio 2) so M12 TX/RX is not starved while stepping.
        HAL_NVIC_SetPriority(hw.irqn, 4, 0);
        HAL_NVIC_EnableIRQ(hw.irqn);
        setDir(i, true);
        g_axis[i].current = 0;
        g_axis[i].target = 0;
        g_axis[i].dir_positive = true;
        g_axis[i].pulse_active = false;
        g_axis[i].block_rearm = false;
        g_axis[i].abort = false;
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

    g_axis[0].target = mmToSteps(0, raw_x_mm);
    g_axis[1].target = mmToSteps(1, raw_y_mm);
    g_axis[2].target = mmToSteps(2, raw_z_mm);

    chaseAxis(0);
    chaseAxis(1);
    chaseAxis(2);
    g_active = anyLag();
}

void stopAndResync(float raw_x_mm, float raw_y_mm, float raw_z_mm)
{
    stopTestTone();
    __disable_irq();
    for (std::uint8_t i = 0; i < 3; ++i)
    {
        silenceAxis(i);
    }
    g_axis[0].current = g_axis[0].target = mmToSteps(0, raw_x_mm);
    g_axis[1].current = g_axis[1].target = mmToSteps(1, raw_y_mm);
    g_axis[2].current = g_axis[2].target = mmToSteps(2, raw_z_mm);
    g_active = false;
    __enable_irq();
}

void emergencyStop()
{
    g_test_tone = false;
    g_test_remaining_ms = 0;
    __disable_irq();
    for (std::uint8_t i = 0; i < 3; ++i)
    {
        silenceAxis(i);
    }
    g_active = false;
    __enable_irq();
}

void startTestTone(Axis axis, std::uint32_t duration_ms)
{
    if (duration_ms == 0u)
    {
        duration_ms = 3000u;
    }
    const std::uint8_t i = axisIndex(axis);
    __disable_irq();
    g_test_tone = false;
    silenceAxis(i);
    g_test_axis = axis;
    g_test_tone = true;
    g_test_remaining_ms = duration_ms;
    g_active = true;
    __enable_irq();
    armPulse(i, 2000u);
}

void stopTestTone()
{
    if (!g_test_tone && !g_axis[axisIndex(g_test_axis)].pulse_active)
    {
        g_test_tone = false;
        g_test_remaining_ms = 0;
        return;
    }
    const std::uint8_t i = axisIndex(g_test_axis);
    __disable_irq();
    g_test_tone = false;
    g_test_remaining_ms = 0;
    silenceAxis(i);
    g_active = anyLag();
    __enable_irq();
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

extern "C" void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef* htim)
{
    if (htim == nullptr || htim->Instance == nullptr)
    {
        return;
    }
    onPulseRise(htim->Instance);
}

extern "C" void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef* htim)
{
    if (htim == nullptr || htim->Instance == nullptr)
    {
        return;
    }
    onPulseEnd(htim->Instance);
}
