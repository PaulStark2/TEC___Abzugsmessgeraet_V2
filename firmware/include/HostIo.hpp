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

void latestAdc(std::uint16_t& fx, std::uint16_t& fy, std::uint16_t& fz);

} // namespace hostio
