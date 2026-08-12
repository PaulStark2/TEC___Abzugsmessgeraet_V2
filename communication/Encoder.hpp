#pragma once

#include "ProtocolTypes.hpp"

#include <cstddef>
#include <cstdint>

namespace tecomm
{

// Encode one complete M12 frame (header + payload + CRC) into out buffer.
EncodeResult encodeFrame(uint8_t message_id, const uint8_t* payload, std::size_t payload_size,
                         uint8_t* out, std::size_t out_capacity);

} // namespace tecomm
