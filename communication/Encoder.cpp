#include "Encoder.hpp"

#include "Crc16.hpp"
#include "ProtocolSpec.hpp"

#include <cstring>

namespace tecomm
{

EncodeResult encodeFrame(uint8_t message_id, const uint8_t* payload, std::size_t payload_size,
                         uint8_t* out, std::size_t out_capacity)
{
    EncodeResult result{};
    result.ok = false;
    result.bytes_written = 0;
    result.error = EncodeError::None;

    if (payload_size > kMaxPayloadSize)
    {
        result.error = EncodeError::PayloadTooLarge;
        return result;
    }

    const std::size_t frame_size = 6 + payload_size + 2;
    if (out_capacity < frame_size)
    {
        result.error = EncodeError::BufferTooSmall;
        return result;
    }

    out[0] = kMagic0;
    out[1] = kMagic1;
    out[2] = kProtocolVersion;
    out[3] = message_id;
    out[4] = static_cast<uint8_t>(payload_size & 0xFFu);
    out[5] = static_cast<uint8_t>((payload_size >> 8) & 0xFFu);

    if (payload_size > 0 && payload != nullptr)
    {
        std::memcpy(out + 6, payload, payload_size);
    }

    const uint16_t crc = crc16CcittFalse(out, 6 + payload_size);
    out[6 + payload_size + 0] = static_cast<uint8_t>(crc & 0xFFu);
    out[6 + payload_size + 1] = static_cast<uint8_t>((crc >> 8) & 0xFFu);

    result.ok = true;
    result.bytes_written = frame_size;
    result.error = EncodeError::None;
    return result;
}

} // namespace tecomm
