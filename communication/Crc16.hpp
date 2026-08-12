#pragma once

#include <cstddef>
#include <cstdint>

namespace tecomm
{

// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF.
uint16_t crc16CcittFalse(const uint8_t* data, std::size_t length);

} // namespace tecomm
