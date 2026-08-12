#include "WireUtil.hpp"

#include <cmath>
#include <cstring>

namespace tecomm
{

bool isFiniteFloat(float value)
{
    return std::isfinite(value) != 0;
}

bool writeU8(uint8_t* buffer, std::size_t capacity, std::size_t& offset, uint8_t value)
{
    if (offset >= capacity)
    {
        return false;
    }
    buffer[offset] = value;
    ++offset;
    return true;
}

bool writeU16LE(uint8_t* buffer, std::size_t capacity, std::size_t& offset, uint16_t value)
{
    if (offset + 2 > capacity)
    {
        return false;
    }
    buffer[offset + 0] = static_cast<uint8_t>(value & 0xFFu);
    buffer[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
    offset += 2;
    return true;
}

bool writeF32LE(uint8_t* buffer, std::size_t capacity, std::size_t& offset, float value)
{
    if (!isFiniteFloat(value))
    {
        return false;
    }
    if (offset + 4 > capacity)
    {
        return false;
    }
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    buffer[offset + 0] = static_cast<uint8_t>(bits & 0xFFu);
    buffer[offset + 1] = static_cast<uint8_t>((bits >> 8) & 0xFFu);
    buffer[offset + 2] = static_cast<uint8_t>((bits >> 16) & 0xFFu);
    buffer[offset + 3] = static_cast<uint8_t>((bits >> 24) & 0xFFu);
    offset += 4;
    return true;
}

bool readU8(const uint8_t* buffer, std::size_t length, std::size_t& offset, uint8_t& value_out)
{
    if (offset >= length)
    {
        return false;
    }
    value_out = buffer[offset];
    ++offset;
    return true;
}

bool readU16LE(const uint8_t* buffer, std::size_t length, std::size_t& offset, uint16_t& value_out)
{
    if (offset + 2 > length)
    {
        return false;
    }
    value_out = static_cast<uint16_t>(buffer[offset + 0]) |
                static_cast<uint16_t>(static_cast<uint16_t>(buffer[offset + 1]) << 8);
    offset += 2;
    return true;
}

bool readF32LE(const uint8_t* buffer, std::size_t length, std::size_t& offset, float& value_out,
               bool validate_finite)
{
    if (offset + 4 > length)
    {
        return false;
    }
    uint32_t bits = static_cast<uint32_t>(buffer[offset + 0]) |
                    (static_cast<uint32_t>(buffer[offset + 1]) << 8) |
                    (static_cast<uint32_t>(buffer[offset + 2]) << 16) |
                    (static_cast<uint32_t>(buffer[offset + 3]) << 24);
    offset += 4;
    std::memcpy(&value_out, &bits, sizeof(value_out));
    if (validate_finite && !isFiniteFloat(value_out))
    {
        return false;
    }
    return true;
}

} // namespace tecomm
