#pragma once

#include <cstddef>
#include <cstdint>

// M12 wire protocol specification (host-agnostic, transport-independent).
// All multi-byte integers are little-endian on the wire.
// float32 values are IEEE754 binary32 little-endian.

namespace tecomm
{

// Frame magic bytes: ASCII "TE".
constexpr uint8_t kMagic0 = 0x54;
constexpr uint8_t kMagic1 = 0x45;

// Current protocol version for encode/decode.
constexpr uint8_t kProtocolVersion = 1;

// Maximum payload and frame sizes.
constexpr std::size_t kMaxPayloadSize = 512;
constexpr std::size_t kMinFrameSize = 8; // header(6) + crc(2), zero-length payload
constexpr std::size_t kMaxFrameSize = 520; // 6 + 512 + 2

// Fixed payload sizes for typed messages (explicit wire layout, no struct padding).
constexpr std::size_t kMoveToPayloadWireSize = 12; // 3 x float32
constexpr std::size_t kBoolPayloadWireSize = 1;
constexpr std::size_t kAckPayloadWireSize = 2;
constexpr std::size_t kNackPayloadWireSize = 3;
constexpr std::size_t kStatusReportPayloadWireSize = 35;
constexpr std::size_t kProgramSummaryPayloadWireSize = 26;
constexpr std::size_t kLoadProgramMaxPointCount = 32;
constexpr std::size_t kLoadProgramMaxPayloadWireSize =
    1 + kLoadProgramMaxPointCount * kMoveToPayloadWireSize; // 385

// CRC-16/CCITT-FALSE over frame bytes [0 .. 5 + payload_length] (header + payload).
// Polynomial 0x1021, initial value 0xFFFF, no input/output reflection.

} // namespace tecomm
