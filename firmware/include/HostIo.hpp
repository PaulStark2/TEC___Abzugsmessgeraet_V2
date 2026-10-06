#pragma once

#include "HostInputs.hpp"

#include <cstdint>

class Machine;

// Phase-2.1 HAL I/O: ADC1 DMA, USART3 echo, GPIO limits/probe/E-Stop, dual spreader, fixture.
namespace hostio
{

void init();
HostInputs sampleInputs(std::uint32_t force_sequence, float force_range_N);
void applyOutputs(const Machine& machine);
void pollUart(Machine& machine);
void sendStatusLine(const Machine& machine, const HostInputs& inputs, std::uint16_t adc_fx,
                    std::uint16_t adc_fy, std::uint16_t adc_fz);
void latestAdc(std::uint16_t& fx, std::uint16_t& fy, std::uint16_t& fz);

} // namespace hostio
