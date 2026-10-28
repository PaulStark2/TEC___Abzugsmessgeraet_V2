#pragma once

#include "HostInputs.hpp"

#include <cstdint>

class FirmwareHost;
class Machine;

// ADC DMA, limits/probe/E-Stop, spreader/fixture GPIO, M12 on TEC_HOST_UART.
namespace hostio
{

void init();
// force_sequence advances every ADC tick; heartbeat_sequence only on CmdNop (Phase 6).
HostInputs sampleInputs(std::uint32_t force_sequence, std::uint32_t heartbeat_sequence,
                        float force_range_N);
void applyOutputs(const Machine& machine);

// Drain RX → FixedFrameBuffer → CommandDispatcher; enqueue TX; 50 Hz status.
void serviceProtocol(FirmwareHost& host);

// Mean of every completed XYZ scan since the previous call. Repeats the last
// mean when the DMA has not finished a new scan.
void consumeMillisecondMean(float& x, float& y, float& z);

} // namespace hostio
