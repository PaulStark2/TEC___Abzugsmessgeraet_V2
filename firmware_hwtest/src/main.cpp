// Electronics bench test for the TEC tensile rig.
// Drives X/Y/Z step/dir in five speed steps and prints force volts plus
// probe, limit and E-Stop levels. Does not link the production Core.
//
// USART2 (ST-Link VCP) and USART3 (Raspberry Pi) run together unless
// kConsole in HwTestConfig.hpp selects only one of them.

#include "stm32f4xx_hal.h"

#include "HwTestConfig.hpp"
#include "PinMap.hpp"

#include <atomic>
#include <cstdio>
#include <cstdint>

extern "C" void Error_Handler(void);

namespace
{

constexpr std::uint32_t kTimClkApb1Hz = 90000000;  // TIM3, TIM4
constexpr std::uint32_t kTimClkApb2Hz = 180000000; // TIM10
constexpr std::uint32_t kPwmTickHz = 1000000;
constexpr unsigned kRxCap = 128;
constexpr unsigned kLineCap = 64;

static_assert(kTimClkApb1Hz % kPwmTickHz == 0, "APB1 timer clock must divide to 1 MHz");
static_assert(kTimClkApb2Hz % kPwmTickHz == 0, "APB2 timer clock must divide to 1 MHz");
static_assert(kPwmTickHz % kSpeedHz[1] == 0, "level 1");
static_assert(kPwmTickHz % kSpeedHz[2] == 0, "level 2");
static_assert(kPwmTickHz % kSpeedHz[3] == 0, "level 3");
static_assert(kPwmTickHz % kSpeedHz[4] == 0, "level 4");
static_assert(kPwmTickHz % kSpeedHz[5] == 0, "level 5");

struct Axis
{
    TIM_HandleTypeDef tim{};
    std::uint32_t channel = TIM_CHANNEL_1;
    GPIO_TypeDef* stepPort = nullptr;
    std::uint16_t stepPin = 0;
    std::uint32_t stepAf = 0;
    GPIO_TypeDef* dirPort = nullptr;
    std::uint16_t dirPin = 0;
    std::uint16_t limPlusPin = 0;
    std::uint16_t limMinusPin = 0;
    std::uint32_t timerHz = 0;
    std::int8_t dir = 0;
    std::uint8_t level = 0;
    char name = '?';
};

struct UartPort
{
    USART_TypeDef* inst = nullptr;
    UART_HandleTypeDef hal{};
    IRQn_Type irqn = USART2_IRQn;
    std::uint8_t rx[kRxCap]{};
    volatile std::uint16_t head = 0;
    volatile std::uint16_t tail = 0;
    char line[kLineCap]{};
    std::uint8_t len = 0;
    bool overflow = false;
    bool enabled = false;
};

Axis g_axis[3];
UartPort g_uart2;
UartPort g_uart3;
ADC_HandleTypeDef g_adc{};
std::uint32_t g_nextStatus = 0;
bool g_announceEstop = false;
bool g_announceLimit[3]{};

bool useUsart2()
{
    return kConsole != Console::Usart3Only;
}

bool useUsart3()
{
    return kConsole != Console::Usart2Only;
}

bool pinHigh(GPIO_TypeDef* port, std::uint16_t pin)
{
    return HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET;
}

const char* levelWord(bool high)
{
    return high ? "High" : "Low";
}

void consoleWrite(const char* text);

void stepIdle(Axis& axis)
{
    HAL_TIM_PWM_Stop(&axis.tim, axis.channel);

    GPIO_InitTypeDef gpio{};
    gpio.Pin = axis.stepPin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(axis.stepPort, &gpio);
    HAL_GPIO_WritePin(axis.stepPort, axis.stepPin, GPIO_PIN_RESET);
}

void axisStop(Axis& axis)
{
    stepIdle(axis);
    axis.dir = 0;
    axis.level = 0;
}

void disarmRunning()
{
    if (pinHigh(PIN_ESTOP_PORT, PIN_ESTOP_PIN))
    {
        bool stopped = false;
        for (Axis& axis : g_axis)
        {
            if (axis.dir != 0)
            {
                axisStop(axis);
                stopped = true;
            }
        }
        if (stopped)
        {
            g_announceEstop = true;
        }
        return;
    }

    for (int i = 0; i < 3; ++i)
    {
        Axis& axis = g_axis[i];
        const bool plusHit = axis.dir > 0 && pinHigh(GPIOA, axis.limPlusPin);
        const bool minusHit = axis.dir < 0 && pinHigh(GPIOA, axis.limMinusPin);
        if (plusHit || minusHit)
        {
            axisStop(axis);
            g_announceLimit[i] = true;
        }
    }
}

void flushAnnounce()
{
    if (g_announceEstop)
    {
        g_announceEstop = false;
        consoleWrite("STOP estop\r\n");
    }
    for (int i = 0; i < 3; ++i)
    {
        if (!g_announceLimit[i])
        {
            continue;
        }
        g_announceLimit[i] = false;
        char msg[20];
        std::snprintf(msg, sizeof msg, "STOP %c limit\r\n", g_axis[i].name);
        consoleWrite(msg);
    }
}

void uartPutc(UartPort& port, char ch)
{
    while ((port.inst->SR & USART_SR_TXE) == 0)
    {
    }
    port.inst->DR = static_cast<std::uint8_t>(ch);
}

void consoleWrite(const char* text)
{
    while (*text != '\0')
    {
        disarmRunning();
        if (g_uart2.enabled)
        {
            uartPutc(g_uart2, *text);
        }
        if (g_uart3.enabled)
        {
            uartPutc(g_uart3, *text);
        }
        ++text;
    }
}

void applyFrequency(Axis& axis, std::uint16_t hz)
{
    const std::uint32_t psc = (axis.timerHz / kPwmTickHz) - 1u;
    const std::uint32_t arr = (kPwmTickHz / hz) - 1u;
    const std::uint32_t ccr = (arr + 1u) / 2u;
    __HAL_TIM_SET_PRESCALER(&axis.tim, psc);
    __HAL_TIM_SET_AUTORELOAD(&axis.tim, arr);
    __HAL_TIM_SET_COMPARE(&axis.tim, axis.channel, ccr);
    axis.tim.Instance->EGR = TIM_EGR_UG;
    axis.tim.Instance->SR = 0;
}

void stepPinAf(Axis& axis)
{
    GPIO_InitTypeDef gpio{};
    gpio.Pin = axis.stepPin;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = axis.stepAf;
    HAL_GPIO_Init(axis.stepPort, &gpio);
}

const char* blockReason(const Axis& axis, int dir)
{
    if (pinHigh(PIN_ESTOP_PORT, PIN_ESTOP_PIN))
    {
        return "ERR estop\r\n";
    }
    const std::uint16_t limPin = dir > 0 ? axis.limPlusPin : axis.limMinusPin;
    if (pinHigh(GPIOA, limPin))
    {
        return "ERR limit\r\n";
    }
    return nullptr;
}

void axisRun(Axis& axis, int dir, int level)
{
    if (dir == 0 || level < 1 || level > 5)
    {
        consoleWrite("ERR cmd\r\n");
        return;
    }
    if (const char* err = blockReason(axis, dir))
    {
        consoleWrite(err);
        return;
    }

    stepIdle(axis);
    axis.dir = 0;
    axis.level = 0;
    HAL_GPIO_WritePin(axis.dirPort, axis.dirPin, dir > 0 ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_Delay(1);
    if (const char* err = blockReason(axis, dir))
    {
        consoleWrite(err);
        return;
    }

    applyFrequency(axis, kSpeedHz[level]);
    stepPinAf(axis);
    if (HAL_TIM_PWM_Start(&axis.tim, axis.channel) != HAL_OK)
    {
        Error_Handler();
    }
    axis.dir = static_cast<std::int8_t>(dir);
    axis.level = static_cast<std::uint8_t>(level);

    char msg[24];
    std::snprintf(msg, sizeof msg, "OK %c%c%d\r\n", axis.name, dir > 0 ? '+' : '-', level);
    consoleWrite(msg);
}

void formatVolts(char* out, std::size_t outLen, bool ok, std::uint16_t counts)
{
    if (!ok)
    {
        std::snprintf(out, outLen, "?.???");
        return;
    }
    const int millivolts = (static_cast<int>(counts) * 3300 + 2047) / 4095;
    std::snprintf(out, outLen, "%d.%03d", millivolts / 1000, millivolts % 1000);
}

void formatMotion(char* out, std::size_t outLen, const Axis& axis)
{
    if (axis.dir > 0)
    {
        std::snprintf(out, outLen, "+%u", axis.level);
    }
    else if (axis.dir < 0)
    {
        std::snprintf(out, outLen, "-%u", axis.level);
    }
    else
    {
        std::snprintf(out, outLen, "0");
    }
}

bool readCounts(std::uint32_t channel, std::uint16_t& counts)
{
    ADC_ChannelConfTypeDef config{};
    config.Channel = channel;
    config.Rank = 1;
    config.SamplingTime = ADC_SAMPLETIME_84CYCLES;
    if (HAL_ADC_ConfigChannel(&g_adc, &config) != HAL_OK)
    {
        return false;
    }
    if (HAL_ADC_Start(&g_adc) != HAL_OK)
    {
        return false;
    }
    if (HAL_ADC_PollForConversion(&g_adc, 10) != HAL_OK)
    {
        HAL_ADC_Stop(&g_adc);
        return false;
    }
    counts = static_cast<std::uint16_t>(HAL_ADC_GetValue(&g_adc) & 0x0FFFu);
    HAL_ADC_Stop(&g_adc);
    return true;
}

void sendStatus()
{
    std::uint16_t cx = 0;
    std::uint16_t cy = 0;
    std::uint16_t cz = 0;
    const bool okx = readCounts(ADC_CHANNEL_0, cx);
    const bool oky = readCounts(ADC_CHANNEL_1, cy);
    const bool okz = readCounts(ADC_CHANNEL_4, cz);

    char fx[8];
    char fy[8];
    char fz[8];
    char mx[8];
    char my[8];
    char mz[8];
    formatVolts(fx, sizeof fx, okx, cx);
    formatVolts(fy, sizeof fy, oky, cy);
    formatVolts(fz, sizeof fz, okz, cz);
    formatMotion(mx, sizeof mx, g_axis[0]);
    formatMotion(my, sizeof my, g_axis[1]);
    formatMotion(mz, sizeof mz, g_axis[2]);

    char msg[200];
    std::snprintf(
        msg,
        sizeof msg,
        "HW Fx=%s Fy=%s Fz=%s V | probe=%s | estop=%s | "
        "X-=%s X+=%s Y-=%s Y+=%s Z-=%s Z+=%s | mot X=%s Y=%s Z=%s\r\n",
        fx,
        fy,
        fz,
        levelWord(pinHigh(PIN_PROBE_PORT, PIN_PROBE_PIN)),
        levelWord(pinHigh(PIN_ESTOP_PORT, PIN_ESTOP_PIN)),
        levelWord(pinHigh(PIN_LIM_XN_PORT, PIN_LIM_XN_PIN)),
        levelWord(pinHigh(PIN_LIM_XP_PORT, PIN_LIM_XP_PIN)),
        levelWord(pinHigh(PIN_LIM_YN_PORT, PIN_LIM_YN_PIN)),
        levelWord(pinHigh(PIN_LIM_YP_PORT, PIN_LIM_YP_PIN)),
        levelWord(pinHigh(PIN_LIM_ZN_PORT, PIN_LIM_ZN_PIN)),
        levelWord(pinHigh(PIN_LIM_ZP_PORT, PIN_LIM_ZP_PIN)),
        mx,
        my,
        mz);
    consoleWrite(msg);
}

void sendHelp()
{
    consoleWrite("HWTEST: ? | H | STOP | X+3 X-1 X0 (Y,Z gleich)\r\n");

    std::uint32_t mmMilli[5];
    for (int i = 0; i < 5; ++i)
    {
        mmMilli[i] = (static_cast<std::uint32_t>(kSpeedHz[i + 1]) * 1000u) / kStepsPerMm;
    }
    char line[180];
    std::snprintf(
        line,
        sizeof line,
        "Stufen 1..5: %u %u %u %u %u Hz = %u.%03u %u.%03u %u.%03u %u.%03u %u.%03u mm/s\r\n",
        static_cast<unsigned>(kSpeedHz[1]),
        static_cast<unsigned>(kSpeedHz[2]),
        static_cast<unsigned>(kSpeedHz[3]),
        static_cast<unsigned>(kSpeedHz[4]),
        static_cast<unsigned>(kSpeedHz[5]),
        static_cast<unsigned>(mmMilli[0] / 1000u),
        static_cast<unsigned>(mmMilli[0] % 1000u),
        static_cast<unsigned>(mmMilli[1] / 1000u),
        static_cast<unsigned>(mmMilli[1] % 1000u),
        static_cast<unsigned>(mmMilli[2] / 1000u),
        static_cast<unsigned>(mmMilli[2] % 1000u),
        static_cast<unsigned>(mmMilli[3] / 1000u),
        static_cast<unsigned>(mmMilli[3] % 1000u),
        static_cast<unsigned>(mmMilli[4] / 1000u),
        static_cast<unsigned>(mmMilli[4] % 1000u));
    consoleWrite(line);
    consoleWrite("E-Stop High blockiert. Endanschlag in Fahrtrichtung stoppt die Achse.\r\n");
}

bool equalIgnoreCase(const char* left, const char* right)
{
    while (*left != '\0' && *right != '\0')
    {
        char a = *left;
        char b = *right;
        if (a >= 'a' && a <= 'z')
        {
            a = static_cast<char>(a - 'a' + 'A');
        }
        if (b >= 'a' && b <= 'z')
        {
            b = static_cast<char>(b - 'a' + 'A');
        }
        if (a != b)
        {
            return false;
        }
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

void handleLine(char* line)
{
    while (*line == ' ' || *line == '\t')
    {
        ++line;
    }
    char* end = line;
    while (*end != '\0')
    {
        ++end;
    }
    while (end > line && (end[-1] == ' ' || end[-1] == '\t'))
    {
        --end;
    }
    *end = '\0';
    if (*line == '\0')
    {
        return;
    }

    if (equalIgnoreCase(line, "?"))
    {
        sendStatus();
        return;
    }
    if (equalIgnoreCase(line, "H") || equalIgnoreCase(line, "HELP"))
    {
        sendHelp();
        return;
    }
    if (equalIgnoreCase(line, "STOP"))
    {
        for (Axis& axis : g_axis)
        {
            axisStop(axis);
        }
        consoleWrite("OK STOP\r\n");
        return;
    }

    char name = line[0];
    if (name >= 'a' && name <= 'z')
    {
        name = static_cast<char>(name - 'a' + 'A');
    }
    if (name != 'X' && name != 'Y' && name != 'Z')
    {
        consoleWrite("ERR cmd\r\n");
        return;
    }

    const char* cursor = line + 1;
    while (*cursor == ' ' || *cursor == '\t')
    {
        ++cursor;
    }
    Axis& axis = g_axis[name == 'X' ? 0 : (name == 'Y' ? 1 : 2)];
    if (cursor[0] == '0' && cursor[1] == '\0')
    {
        axisStop(axis);
        char msg[16];
        std::snprintf(msg, sizeof msg, "OK %c0\r\n", name);
        consoleWrite(msg);
        return;
    }

    int dir = 0;
    if (cursor[0] == '+')
    {
        dir = 1;
    }
    else if (cursor[0] == '-')
    {
        dir = -1;
    }
    else
    {
        consoleWrite("ERR cmd\r\n");
        return;
    }
    ++cursor;
    while (*cursor == ' ' || *cursor == '\t')
    {
        ++cursor;
    }
    if (cursor[0] < '1' || cursor[0] > '5' || cursor[1] != '\0')
    {
        consoleWrite("ERR cmd\r\n");
        return;
    }
    axisRun(axis, dir, cursor[0] - '0');
}

bool popRx(UartPort& port, std::uint8_t& byte)
{
    const std::uint16_t head = port.head;
    if (port.tail == head)
    {
        return false;
    }
    std::atomic_signal_fence(std::memory_order_acquire);
    byte = port.rx[port.tail];
    port.tail = static_cast<std::uint16_t>((port.tail + 1u) % kRxCap);
    return true;
}

void pollUart(UartPort& port)
{
    if (!port.enabled)
    {
        return;
    }
    std::uint8_t byte = 0;
    while (popRx(port, byte))
    {
        if (byte == '\r' || byte == '\n')
        {
            if (port.overflow)
            {
                port.overflow = false;
                port.len = 0;
                consoleWrite("ERR cmd\r\n");
                continue;
            }
            port.line[port.len] = '\0';
            port.len = 0;
            handleLine(port.line);
            continue;
        }
        if (port.len + 1u >= kLineCap)
        {
            port.overflow = true;
            continue;
        }
        port.line[port.len++] = static_cast<char>(byte);
    }
}

void uartOnIrq(UartPort& port)
{
    if (!port.enabled || port.inst == nullptr)
    {
        return;
    }
    for (;;)
    {
        const std::uint32_t status = port.inst->SR;
        if ((status & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) == 0)
        {
            break;
        }
        const std::uint8_t byte = static_cast<std::uint8_t>(port.inst->DR & 0xFFu);
        if ((status & USART_SR_RXNE) == 0)
        {
            continue;
        }
        const std::uint16_t next = static_cast<std::uint16_t>((port.head + 1u) % kRxCap);
        if (next == port.tail)
        {
            continue;
        }
        port.rx[port.head] = byte;
        std::atomic_signal_fence(std::memory_order_release);
        port.head = next;
    }
}

void initAxis(Axis& axis, TIM_TypeDef* instance, std::uint32_t timerHz, GPIO_TypeDef* stepPort, std::uint16_t stepPin,
              std::uint32_t stepAf, GPIO_TypeDef* dirPort, std::uint16_t dirPin, std::uint16_t limPlusPin,
              std::uint16_t limMinusPin, char name)
{
    axis.channel = TIM_CHANNEL_1;
    axis.stepPort = stepPort;
    axis.stepPin = stepPin;
    axis.stepAf = stepAf;
    axis.dirPort = dirPort;
    axis.dirPin = dirPin;
    axis.limPlusPin = limPlusPin;
    axis.limMinusPin = limMinusPin;
    axis.timerHz = timerHz;
    axis.dir = 0;
    axis.level = 0;
    axis.name = name;

    if (instance == TIM3)
    {
        __HAL_RCC_TIM3_CLK_ENABLE();
    }
    else if (instance == TIM4)
    {
        __HAL_RCC_TIM4_CLK_ENABLE();
    }
    else if (instance == TIM10)
    {
        __HAL_RCC_TIM10_CLK_ENABLE();
    }

    axis.tim = TIM_HandleTypeDef{};
    axis.tim.Instance = instance;
    axis.tim.Init.Prescaler = (timerHz / kPwmTickHz) - 1u;
    axis.tim.Init.CounterMode = TIM_COUNTERMODE_UP;
    axis.tim.Init.Period = 9999;
    axis.tim.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    axis.tim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&axis.tim) != HAL_OK)
    {
        Error_Handler();
    }

    TIM_OC_InitTypeDef oc{};
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = 5000;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&axis.tim, &oc, TIM_CHANNEL_1) != HAL_OK)
    {
        Error_Handler();
    }
}

void initAxes()
{
    initAxis(g_axis[0], TIM4, kTimClkApb1Hz, PIN_STEP_X_PORT, PIN_STEP_X_PIN, GPIO_AF2_TIM4, PIN_DIR_X_PORT,
             PIN_DIR_X_PIN, PIN_LIM_XP_PIN, PIN_LIM_XN_PIN, 'X');
    initAxis(g_axis[1], TIM3, kTimClkApb1Hz, PIN_STEP_Y_PORT, PIN_STEP_Y_PIN, GPIO_AF2_TIM3, PIN_DIR_Y_PORT,
             PIN_DIR_Y_PIN, PIN_LIM_YP_PIN, PIN_LIM_YN_PIN, 'Y');
    initAxis(g_axis[2], TIM10, kTimClkApb2Hz, PIN_STEP_Z_PORT, PIN_STEP_Z_PIN, GPIO_AF3_TIM10, PIN_DIR_Z_PORT,
             PIN_DIR_Z_PIN, PIN_LIM_ZP_PIN, PIN_LIM_ZN_PIN, 'Z');
}

void initUart(UartPort& port, USART_TypeDef* instance, IRQn_Type irqn, GPIO_TypeDef* gpioPort, std::uint16_t txPin,
              std::uint16_t rxPin, std::uint32_t alternate)
{
    port.inst = instance;
    port.irqn = irqn;
    port.head = 0;
    port.tail = 0;
    port.len = 0;
    port.overflow = false;

    if (instance == USART2)
    {
        __HAL_RCC_USART2_CLK_ENABLE();
    }
    else if (instance == USART3)
    {
        __HAL_RCC_USART3_CLK_ENABLE();
    }

    GPIO_InitTypeDef gpio{};
    gpio.Pin = txPin | rxPin;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = alternate;
    HAL_GPIO_Init(gpioPort, &gpio);

    port.hal = UART_HandleTypeDef{};
    port.hal.Instance = instance;
    port.hal.Init.BaudRate = kUartBaud;
    port.hal.Init.WordLength = UART_WORDLENGTH_8B;
    port.hal.Init.StopBits = UART_STOPBITS_1;
    port.hal.Init.Parity = UART_PARITY_NONE;
    port.hal.Init.Mode = UART_MODE_TX_RX;
    port.hal.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    port.hal.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&port.hal) != HAL_OK)
    {
        Error_Handler();
    }

    port.enabled = true;
    HAL_NVIC_SetPriority(irqn, 6, 0);
    HAL_NVIC_EnableIRQ(irqn);
    __HAL_UART_ENABLE_IT(&port.hal, UART_IT_RXNE);
}

void Adc_Init()
{
    __HAL_RCC_ADC1_CLK_ENABLE();
    g_adc = ADC_HandleTypeDef{};
    g_adc.Instance = ADC1;
    g_adc.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    g_adc.Init.Resolution = ADC_RESOLUTION_12B;
    g_adc.Init.ScanConvMode = DISABLE;
    g_adc.Init.ContinuousConvMode = DISABLE;
    g_adc.Init.DiscontinuousConvMode = DISABLE;
    g_adc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    g_adc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    g_adc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    g_adc.Init.NbrOfConversion = 1;
    g_adc.Init.DMAContinuousRequests = DISABLE;
    g_adc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    if (HAL_ADC_Init(&g_adc) != HAL_OK)
    {
        Error_Handler();
    }
}

void Led_EarlyInit()
{
    __HAL_RCC_GPIOC_CLK_ENABLE();
    HAL_GPIO_WritePin(PIN_DEBUG_LED_PORT, PIN_DEBUG_LED_PIN, GPIO_PIN_RESET);
    GPIO_InitTypeDef gpio{};
    gpio.Pin = PIN_DEBUG_LED_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(PIN_DEBUG_LED_PORT, &gpio);
}

void SystemClock_Config()
{
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitTypeDef osc{};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
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

void Gpio_Init()
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

    gpio.Pin = PIN_LIM_XP_PIN | PIN_LIM_YP_PIN | PIN_LIM_ZP_PIN | PIN_ESTOP_PIN | PIN_LIM_XN_PIN | PIN_LIM_YN_PIN |
               PIN_LIM_ZN_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOA, &gpio);

    HAL_GPIO_WritePin(PIN_STEP_X_PORT, PIN_STEP_X_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PIN_STEP_Y_PORT, PIN_STEP_Y_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PIN_STEP_Z_PORT, PIN_STEP_Z_PIN, GPIO_PIN_RESET);
    gpio.Pin = PIN_STEP_X_PIN | PIN_STEP_Y_PIN | PIN_STEP_Z_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);

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

const char* consoleName()
{
    if (kConsole == Console::Usart2Only)
    {
        return "usart2";
    }
    if (kConsole == Console::Usart3Only)
    {
        return "usart3";
    }
    return "both";
}

} // namespace

extern "C" void Error_Handler(void)
{
    __disable_irq();
    for (;;)
    {
        HAL_GPIO_TogglePin(PIN_DEBUG_LED_PORT, PIN_DEBUG_LED_PIN);
        for (volatile std::uint32_t i = 0; i < 200000u; ++i)
        {
        }
    }
}

extern "C" void SysTick_Handler(void)
{
    HAL_IncTick();
}

extern "C" void USART2_IRQHandler(void)
{
    uartOnIrq(g_uart2);
}

extern "C" void USART3_IRQHandler(void)
{
    uartOnIrq(g_uart3);
}

extern "C" void NMI_Handler(void) {}

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

extern "C" void SVC_Handler(void) {}

extern "C" void DebugMon_Handler(void) {}

extern "C" void PendSV_Handler(void) {}

extern "C" void HAL_MspInit(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
}

int main(void)
{
    HAL_Init();
    Led_EarlyInit();
    SystemClock_Config();
    Gpio_Init();
    Adc_Init();
    initAxes();

    if (useUsart2())
    {
        initUart(g_uart2, USART2, USART2_IRQn, PIN_VCP_TX_PORT, PIN_VCP_TX_PIN, PIN_VCP_RX_PIN, GPIO_AF7_USART2);
    }
    if (useUsart3())
    {
        initUart(g_uart3, USART3, USART3_IRQn, PIN_UART_TX_PORT, PIN_UART_TX_PIN, PIN_UART_RX_PIN, GPIO_AF7_USART3);
    }

    char banner[64];
    std::snprintf(banner, sizeof banner, "HWTEST ready console=%s. H fuer Hilfe.\r\n", consoleName());
    consoleWrite(banner);
    sendStatus();
    g_nextStatus = HAL_GetTick() + kStatusPeriodMs;

    for (;;)
    {
        disarmRunning();
        flushAnnounce();
        pollUart(g_uart2);
        pollUart(g_uart3);

        const std::uint32_t now = HAL_GetTick();
        if (static_cast<std::int32_t>(now - g_nextStatus) >= 0)
        {
            g_nextStatus = now + kStatusPeriodMs;
            HAL_GPIO_TogglePin(PIN_DEBUG_LED_PORT, PIN_DEBUG_LED_PIN);
            sendStatus();
        }
        __WFI();
    }
}
