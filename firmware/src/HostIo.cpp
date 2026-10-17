#include "HostIo.hpp"

#include "AdcScaler.hpp"
#include "CommandDispatcher.hpp"
#include "FirmwareHost.hpp"
#include "FixedFrameBuffer.hpp"
#include "Machine.hpp"
#include "PinMap.hpp"
#include "Types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

extern "C" void Error_Handler(void);

static ADC_HandleTypeDef g_adc{};
static DMA_HandleTypeDef g_dma_adc{};
static UART_HandleTypeDef g_uart{};
static volatile std::uint16_t g_adc_dma[3] = {2048u, 2048u, 2048u};

static constexpr std::size_t kRxCap = 1024;
static volatile std::uint8_t g_rx_buf[kRxCap]{};
static volatile std::uint16_t g_rx_head = 0;
static volatile std::uint16_t g_rx_tail = 0;

static constexpr std::size_t kTxCap = 4096;
static volatile std::uint8_t g_tx_buf[kTxCap]{};
static volatile std::uint16_t g_tx_head = 0;
static volatile std::uint16_t g_tx_tail = 0;
static volatile bool g_tx_irq_armed = false;

static FixedFrameBuffer g_frames{};
static bool g_spreader_pins_latched = false;
static bool g_spreader_open_latched = false;
static std::uint32_t g_status_divider = 0;

// Limit lines pick up stepper EMI (spikes to ~2 V). Core treats a single High
// sample as +max during homing — debounce at 1 kHz before publishing.
static constexpr std::uint8_t kLimitDebounceMs = 10;

struct DebouncedLevel
{
    bool stable = false;
    std::uint8_t streak = 0;
};

static DebouncedLevel g_lim_xn{};
static DebouncedLevel g_lim_xp{};
static DebouncedLevel g_lim_yn{};
static DebouncedLevel g_lim_yp{};
static DebouncedLevel g_lim_zn{};
static DebouncedLevel g_lim_zp{};

static bool pinHigh(GPIO_TypeDef* port, std::uint16_t pin)
{
    return HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET;
}

static bool debounceLevel(DebouncedLevel& state, bool raw, std::uint8_t need_ms)
{
    if (raw == state.stable)
    {
        state.streak = 0;
        return state.stable;
    }
    if (state.streak < 255u)
    {
        ++state.streak;
    }
    if (state.streak >= need_ms)
    {
        state.stable = raw;
        state.streak = 0;
    }
    return state.stable;
}

static void rxPush(std::uint8_t byte)
{
    const std::uint16_t next = static_cast<std::uint16_t>((g_rx_head + 1u) % kRxCap);
    if (next == g_rx_tail)
    {
        return;
    }
    g_rx_buf[g_rx_head] = byte;
    g_rx_head = next;
}

static bool rxPop(std::uint8_t& byte)
{
    if (g_rx_head == g_rx_tail)
    {
        return false;
    }
    byte = g_rx_buf[g_rx_tail];
    g_rx_tail = static_cast<std::uint16_t>((g_rx_tail + 1u) % kRxCap);
    return true;
}

static bool txPush(std::uint8_t byte)
{
    const std::uint16_t next = static_cast<std::uint16_t>((g_tx_head + 1u) % kTxCap);
    if (next == g_tx_tail)
    {
        return false;
    }
    g_tx_buf[g_tx_head] = byte;
    g_tx_head = next;
    return true;
}

static bool txPop(std::uint8_t& byte)
{
    if (g_tx_head == g_tx_tail)
    {
        return false;
    }
    byte = g_tx_buf[g_tx_tail];
    g_tx_tail = static_cast<std::uint16_t>((g_tx_tail + 1u) % kTxCap);
    return true;
}

static void txArm()
{
    if (g_tx_irq_armed)
    {
        return;
    }
    if (g_tx_head == g_tx_tail)
    {
        return;
    }
    g_tx_irq_armed = true;
    __HAL_UART_ENABLE_IT(&g_uart, UART_IT_TXE);
}

static std::size_t txFreeSpace()
{
    // One slot left empty to distinguish full vs empty.
    const std::uint16_t head = g_tx_head;
    const std::uint16_t tail = g_tx_tail;
    if (head >= tail)
    {
        return static_cast<std::size_t>(kTxCap - 1u - (head - tail));
    }
    return static_cast<std::size_t>(tail - head - 1u);
}

// All-or-nothing: never push a partial frame (truncation corrupts the M12 stream).
static bool enqueueFrame(const simbridge::OutboundFrame& frame)
{
    if (frame.length == 0 || frame.length > txFreeSpace())
    {
        return false;
    }
    for (std::size_t i = 0; i < frame.length; ++i)
    {
        (void)txPush(frame.data[i]);
    }
    txArm();
    return true;
}

// Command ACKs/NACKs must not be silently dropped when the TX ring is tight.
static constexpr std::size_t kPendingTxCap = 8;
static simbridge::OutboundFrame g_pending_tx[kPendingTxCap]{};
static std::size_t g_pending_tx_n = 0;
static std::uint8_t g_adc_push_div = 0;

static void flushPendingTx()
{
    while (g_pending_tx_n > 0u)
    {
        if (!enqueueFrame(g_pending_tx[0]))
        {
            break;
        }
        for (std::size_t i = 1; i < g_pending_tx_n; ++i)
        {
            g_pending_tx[i - 1u] = g_pending_tx[i];
        }
        --g_pending_tx_n;
    }
}

static void enqueueFramesPriority(const std::vector<simbridge::OutboundFrame>& frames)
{
    for (const simbridge::OutboundFrame& frame : frames)
    {
        if (enqueueFrame(frame))
        {
            continue;
        }
        if (g_pending_tx_n < kPendingTxCap)
        {
            g_pending_tx[g_pending_tx_n++] = frame;
            continue;
        }
        // Queue full: drop oldest pending slot to keep the newest reply (usually ACK/STOP).
        for (std::size_t i = 1; i < kPendingTxCap; ++i)
        {
            g_pending_tx[i - 1u] = g_pending_tx[i];
        }
        g_pending_tx[kPendingTxCap - 1u] = frame;
    }
}

static void driveSpreaderPair(bool open)
{
    if (open)
    {
        HAL_GPIO_WritePin(PIN_SPREADER_CLOSE_PORT, PIN_SPREADER_CLOSE_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(PIN_SPREADER_OPEN_PORT, PIN_SPREADER_OPEN_PIN, GPIO_PIN_SET);
    }
    else
    {
        HAL_GPIO_WritePin(PIN_SPREADER_OPEN_PORT, PIN_SPREADER_OPEN_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(PIN_SPREADER_CLOSE_PORT, PIN_SPREADER_CLOSE_PIN, GPIO_PIN_SET);
    }
    g_spreader_open_latched = open;
    g_spreader_pins_latched = true;
}

static void Adc_Init()
{
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();

    g_dma_adc.Instance = DMA2_Stream0;
    g_dma_adc.Init.Channel = DMA_CHANNEL_0;
    g_dma_adc.Init.Direction = DMA_PERIPH_TO_MEMORY;
    g_dma_adc.Init.PeriphInc = DMA_PINC_DISABLE;
    g_dma_adc.Init.MemInc = DMA_MINC_ENABLE;
    g_dma_adc.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    g_dma_adc.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    g_dma_adc.Init.Mode = DMA_CIRCULAR;
    g_dma_adc.Init.Priority = DMA_PRIORITY_HIGH;
    g_dma_adc.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&g_dma_adc) != HAL_OK)
    {
        Error_Handler();
    }
    __HAL_LINKDMA(&g_adc, DMA_Handle, g_dma_adc);

    g_adc.Instance = ADC1;
    g_adc.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    g_adc.Init.Resolution = ADC_RESOLUTION_12B;
    g_adc.Init.ScanConvMode = ENABLE;
    g_adc.Init.ContinuousConvMode = ENABLE;
    g_adc.Init.DiscontinuousConvMode = DISABLE;
    g_adc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    g_adc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    g_adc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    g_adc.Init.NbrOfConversion = 3;
    g_adc.Init.DMAContinuousRequests = ENABLE;
    g_adc.Init.EOCSelection = ADC_EOC_SEQ_CONV;
    if (HAL_ADC_Init(&g_adc) != HAL_OK)
    {
        Error_Handler();
    }

    ADC_ChannelConfTypeDef ch{};
    ch.SamplingTime = ADC_SAMPLETIME_84CYCLES;
    ch.Channel = ADC_CHANNEL_0;
    ch.Rank = 1;
    if (HAL_ADC_ConfigChannel(&g_adc, &ch) != HAL_OK)
    {
        Error_Handler();
    }
    ch.Channel = ADC_CHANNEL_1;
    ch.Rank = 2;
    if (HAL_ADC_ConfigChannel(&g_adc, &ch) != HAL_OK)
    {
        Error_Handler();
    }
    ch.Channel = ADC_CHANNEL_4;
    ch.Rank = 3;
    if (HAL_ADC_ConfigChannel(&g_adc, &ch) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_ADC_Start_DMA(&g_adc, reinterpret_cast<std::uint32_t*>(const_cast<std::uint16_t*>(g_adc_dma)), 3) !=
        HAL_OK)
    {
        Error_Handler();
    }
    __HAL_DMA_DISABLE_IT(&g_dma_adc, DMA_IT_TC | DMA_IT_HT | DMA_IT_TE);
    HAL_Delay(2);
}

static void Uart_Init()
{
    TEC_HOST_USART_CLK_ENABLE();

    GPIO_InitTypeDef gpio{};
    gpio.Pin = PIN_UART_TX_PIN | PIN_UART_RX_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = TEC_HOST_USART_AF;
    HAL_GPIO_Init(PIN_UART_TX_PORT, &gpio);

    g_uart.Instance = TEC_HOST_USART;
    g_uart.Init.BaudRate = 115200;
    g_uart.Init.WordLength = UART_WORDLENGTH_8B;
    g_uart.Init.StopBits = UART_STOPBITS_1;
    g_uart.Init.Parity = UART_PARITY_NONE;
    g_uart.Init.Mode = UART_MODE_TX_RX;
    g_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    g_uart.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&g_uart) != HAL_OK)
    {
        Error_Handler();
    }

    // Higher priority than StepGenerator TIM IRQs (prio 4): during motion, TIM at
    // prio 3 used to starve USART TXE → TX ring full → ACKs dropped → GUI hangs.
    HAL_NVIC_SetPriority(TEC_HOST_USART_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(TEC_HOST_USART_IRQn);
    __HAL_UART_ENABLE_IT(&g_uart, UART_IT_RXNE);
}

namespace hostio
{

void init()
{
    Adc_Init();
    Uart_Init();
}

HostInputs sampleInputs(std::uint32_t force_sequence, std::uint32_t heartbeat_sequence,
                        float force_range_N)
{
    HostInputs inputs{};
    std::uint16_t fx = 0;
    std::uint16_t fy = 0;
    std::uint16_t fz = 0;
    latestAdc(fx, fy, fz);
    inputs.force.fx = adc_scaler::countsToNewton(fx, force_range_N);
    inputs.force.fy = adc_scaler::countsToNewton(fy, force_range_N);
    inputs.force.fz = adc_scaler::countsToNewton(fz, force_range_N);
    inputs.forceSampleValid = true;
    inputs.forceSampleSequence = force_sequence;
    inputs.hostHeartbeatValid = true;
    inputs.hostHeartbeatSequence = heartbeat_sequence;
    inputs.limitXMin = debounceLevel(g_lim_xn, pinHigh(PIN_LIM_XN_PORT, PIN_LIM_XN_PIN), kLimitDebounceMs);
    inputs.limitXMax = debounceLevel(g_lim_xp, pinHigh(PIN_LIM_XP_PORT, PIN_LIM_XP_PIN), kLimitDebounceMs);
    inputs.limitYMin = debounceLevel(g_lim_yn, pinHigh(PIN_LIM_YN_PORT, PIN_LIM_YN_PIN), kLimitDebounceMs);
    inputs.limitYMax = debounceLevel(g_lim_yp, pinHigh(PIN_LIM_YP_PORT, PIN_LIM_YP_PIN), kLimitDebounceMs);
    inputs.limitZMin = debounceLevel(g_lim_zn, pinHigh(PIN_LIM_ZN_PORT, PIN_LIM_ZN_PIN), kLimitDebounceMs);
    inputs.limitZMax = debounceLevel(g_lim_zp, pinHigh(PIN_LIM_ZP_PORT, PIN_LIM_ZP_PIN), kLimitDebounceMs);
    // E-Stop: no debounce — fail fast. Hardware should still cut power.
    inputs.e_stop = pinHigh(PIN_ESTOP_PORT, PIN_ESTOP_PIN);
    // Level only (NC: Low = contact). Pose/sequence latch is in FirmwareHost.
    inputs.probeTriggered = !pinHigh(PIN_PROBE_PORT, PIN_PROBE_PIN);
    inputs.probeTriggerPoseValid = false;
    inputs.probeTriggerSequence = 0;
    return inputs;
}

void applyOutputs(const Machine& machine)
{
    const SpreaderState st = machine.spreaderState();
    if (st != SpreaderState::SafeDeenergized)
    {
        const bool open = (st == SpreaderState::Extending || st == SpreaderState::Extended);
        driveSpreaderPair(open);
    }
    else if (!g_spreader_pins_latched)
    {
        driveSpreaderPair(false);
    }

    const HostOutputs outputs = machine.hostOutputs();
    HAL_GPIO_WritePin(PIN_FIXTURE_PORT, PIN_FIXTURE_PIN,
                      outputs.fixtureExtend ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void latestAdc(std::uint16_t& fx, std::uint16_t& fy, std::uint16_t& fz)
{
    fx = g_adc_dma[0];
    fy = g_adc_dma[1];
    fz = g_adc_dma[2];
}

void serviceProtocol(FirmwareHost& host)
{
    flushPendingTx();

    std::uint8_t byte = 0;
    while (rxPop(byte))
    {
        g_frames.append(&byte, 1);
    }

    tecomm::DecodedFrame decoded{};
    tecomm::DecodeResult decode_result = tecomm::DecodeResult::NeedMoreData;
    while (g_frames.tryPopFrame(decoded, decode_result))
    {
        std::vector<simbridge::OutboundFrame> out;
        out.reserve(4);
        host.handleFrame(decoded, decode_result, out);
        enqueueFramesPriority(out);
    }

    flushPendingTx();

    // StatusReport 50 Hz; MsgRawAdc 10 Hz. Skip if TX tight or pending replies wait.
    ++g_status_divider;
    if (g_status_divider >= 20u)
    {
        g_status_divider = 0;
        const bool include_adc = (++g_adc_push_div % 5u) == 0u;
        const std::size_t need = include_adc ? 96u : 64u;
        if (g_pending_tx_n == 0u && txFreeSpace() >= need)
        {
            std::vector<simbridge::OutboundFrame> status;
            status.reserve(2);
            host.appendStatusPush(status, include_adc);
            for (const simbridge::OutboundFrame& frame : status)
            {
                (void)enqueueFrame(frame);
            }
        }
    }
}

} // namespace hostio

extern "C" void TEC_HOST_USART_IRQHandler(void)
{
    if (__HAL_UART_GET_FLAG(&g_uart, UART_FLAG_RXNE) != RESET)
    {
        const std::uint8_t byte = static_cast<std::uint8_t>(g_uart.Instance->DR);
        rxPush(byte);
    }

    if (__HAL_UART_GET_FLAG(&g_uart, UART_FLAG_TXE) != RESET && g_tx_irq_armed)
    {
        std::uint8_t byte = 0;
        if (txPop(byte))
        {
            g_uart.Instance->DR = byte;
        }
        else
        {
            __HAL_UART_DISABLE_IT(&g_uart, UART_IT_TXE);
            g_tx_irq_armed = false;
        }
    }

    if (__HAL_UART_GET_FLAG(&g_uart, UART_FLAG_ORE) != RESET)
    {
        volatile std::uint32_t sync = g_uart.Instance->SR;
        sync = g_uart.Instance->DR;
        (void)sync;
    }
}
