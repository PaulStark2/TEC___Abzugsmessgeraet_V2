#include "HostIo.hpp"

#include "AdcScaler.hpp"
#include "Machine.hpp"
#include "PinMap.hpp"
#include "Types.hpp"

#include <cstdio>

extern "C" void Error_Handler(void);

static ADC_HandleTypeDef g_adc{};
static DMA_HandleTypeDef g_dma_adc{};
static UART_HandleTypeDef g_uart{};
static volatile std::uint16_t g_adc_dma[3]{};

static constexpr std::size_t kRxCap = 16;
static volatile std::uint8_t g_rx_buf[kRxCap]{};
static volatile std::uint8_t g_rx_head = 0;
static volatile std::uint8_t g_rx_tail = 0;
static char g_status_line[96]{};
static volatile bool g_tx_busy = false;
static bool g_spreader_pins_latched = false;
static bool g_spreader_open_latched = false;

static bool pinHigh(GPIO_TypeDef* port, std::uint16_t pin)
{
    return HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET;
}

static void rxPush(std::uint8_t byte)
{
    const std::uint8_t next = static_cast<std::uint8_t>((g_rx_head + 1u) % kRxCap);
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
    g_rx_tail = static_cast<std::uint8_t>((g_rx_tail + 1u) % kRxCap);
    return true;
}

// Complementary pair: open = PC8 High / PC12 Low, close = PC8 Low / PC12 High.
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
}

static void Uart_Init()
{
    __HAL_RCC_USART3_CLK_ENABLE();

    GPIO_InitTypeDef gpio{};
    gpio.Pin = PIN_UART_TX_PIN | PIN_UART_RX_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(PIN_UART_TX_PORT, &gpio);

    g_uart.Instance = USART3;
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

    HAL_NVIC_SetPriority(USART3_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
    __HAL_UART_ENABLE_IT(&g_uart, UART_IT_RXNE);
}

namespace hostio
{

void init()
{
    Adc_Init();
    Uart_Init();
}

HostInputs sampleInputs(std::uint32_t force_sequence, float force_range_N)
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
    inputs.hostHeartbeatSequence = force_sequence;
    inputs.limitXMin = pinHigh(PIN_LIM_XN_PORT, PIN_LIM_XN_PIN);
    inputs.limitXMax = pinHigh(PIN_LIM_XP_PORT, PIN_LIM_XP_PIN);
    inputs.limitYMin = pinHigh(PIN_LIM_YN_PORT, PIN_LIM_YN_PIN);
    inputs.limitYMax = pinHigh(PIN_LIM_YP_PORT, PIN_LIM_YP_PIN);
    inputs.limitZMin = pinHigh(PIN_LIM_ZN_PORT, PIN_LIM_ZN_PIN);
    inputs.limitZMax = pinHigh(PIN_LIM_ZP_PORT, PIN_LIM_ZP_PIN);
    inputs.e_stop = pinHigh(PIN_ESTOP_PORT, PIN_ESTOP_PIN);
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

void sendStatusLine(const Machine& machine, const HostInputs& inputs, std::uint16_t adc_fx,
                    std::uint16_t adc_fy, std::uint16_t adc_fz)
{
    if (g_tx_busy)
    {
        return;
    }
    const int fx_mN = static_cast<int>(inputs.force.fx * 1000.0f);
    const int fy_mN = static_cast<int>(inputs.force.fy * 1000.0f);
    const int fz_mN = static_cast<int>(inputs.force.fz * 1000.0f);
    std::uint8_t lim = 0;
    if (inputs.limitXMin)
    {
        lim |= 1u << 0;
    }
    if (inputs.limitXMax)
    {
        lim |= 1u << 1;
    }
    if (inputs.limitYMin)
    {
        lim |= 1u << 2;
    }
    if (inputs.limitYMax)
    {
        lim |= 1u << 3;
    }
    if (inputs.limitZMin)
    {
        lim |= 1u << 4;
    }
    if (inputs.limitZMax)
    {
        lim |= 1u << 5;
    }
    const unsigned open = g_spreader_open_latched ? 1u : 0u;
    const int n = std::snprintf(
        g_status_line, sizeof(g_status_line),
        "IO e=%u p=%u lim=0x%02X adc=%u,%u,%u mN=%d,%d,%d spr=%u st=%u\r\n",
        inputs.e_stop ? 1u : 0u, inputs.probeTriggered ? 1u : 0u, static_cast<unsigned>(lim),
        static_cast<unsigned>(adc_fx), static_cast<unsigned>(adc_fy), static_cast<unsigned>(adc_fz), fx_mN,
        fy_mN, fz_mN, open, static_cast<unsigned>(machine.machineState()));
    if (n <= 0)
    {
        return;
    }
    g_tx_busy = true;
    if (HAL_UART_Transmit_IT(&g_uart, reinterpret_cast<std::uint8_t*>(g_status_line),
                             static_cast<std::uint16_t>(n)) != HAL_OK)
    {
        g_tx_busy = false;
    }
}

void pollUart(Machine& machine)
{
    std::uint8_t byte = 0;
    while (rxPop(byte))
    {
        if (byte == 'F' || byte == 'f')
        {
            machine.setFixtureExtend(!machine.hostOutputs().fixtureExtend);
        }
        else if (byte == 'R' || byte == 'r')
        {
            machine.setSpreaderExtend(!machine.hostOutputs().spreaderExtend);
        }
        else if (byte == '?')
        {
            std::uint16_t fx = 0;
            std::uint16_t fy = 0;
            std::uint16_t fz = 0;
            latestAdc(fx, fy, fz);
            sendStatusLine(machine, machine.hostInputs(), fx, fy, fz);
        }
    }
}

} // namespace hostio

extern "C" void USART3_IRQHandler(void)
{
    if (__HAL_UART_GET_FLAG(&g_uart, UART_FLAG_RXNE) != RESET)
    {
        const std::uint8_t byte = static_cast<std::uint8_t>(g_uart.Instance->DR);
        if (!g_tx_busy && __HAL_UART_GET_FLAG(&g_uart, UART_FLAG_TXE) != RESET)
        {
            g_uart.Instance->DR = byte;
        }
        rxPush(byte);
    }
    HAL_UART_IRQHandler(&g_uart);
}

extern "C" void HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart)
{
    if (huart->Instance == USART3)
    {
        g_tx_busy = false;
    }
}
