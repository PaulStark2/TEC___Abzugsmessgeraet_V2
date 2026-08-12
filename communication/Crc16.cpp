#include "Crc16.hpp"

namespace tecomm
{

uint16_t crc16CcittFalse(const uint8_t* data, std::size_t length)
{
    uint16_t crc = 0xFFFFu;
    for (std::size_t i = 0; i < length; ++i)
    {
        crc ^= static_cast<uint16_t>(static_cast<uint16_t>(data[i]) << 8);
        for (int bit = 0; bit < 8; ++bit)
        {
            if ((crc & 0x8000u) != 0)
            {
                crc = static_cast<uint16_t>((crc << 1) ^ 0x1021u);
            }
            else
            {
                crc = static_cast<uint16_t>(crc << 1);
            }
        }
    }
    return crc;
}

} // namespace tecomm
