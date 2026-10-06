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
constexpr std::size_t kStatusReportPayloadWireSize = 41;
constexpr std::size_t kProgramSummaryPayloadWireSize = 26;
constexpr std::size_t kLoadProgramMaxPointCount = 32;
constexpr std::size_t kLoadProgramMaxPayloadWireSize =
    1 + kLoadProgramMaxPointCount * kMoveToPayloadWireSize; // 385
// Previous 288-byte layout, plus force_range_N and force_hard_limit_N.
constexpr std::size_t kMachineConfigPayloadWireSize = 296;
constexpr std::size_t kTestConfigurationPayloadWireSize = 8; // mode u8 + force f32 + iterations u16 + index u8
constexpr std::size_t kPlateOriginPayloadWireSize = kMoveToPayloadWireSize;
constexpr std::size_t kForcePathChunkMaxSamples = 32;
constexpr std::size_t kForcePathChunkHeaderWireSize = 12; // point u8 + iter u16 + seq u32 + count u8 + dropped u32
constexpr std::size_t kForcePathChunkMaxPayloadWireSize =
    kForcePathChunkHeaderWireSize + kForcePathChunkMaxSamples * 8;

constexpr std::size_t kForceCalibMaxPoints = 33;
constexpr std::size_t kForceCalibPointWireSize = 12;
constexpr std::size_t kSetForceCalibrationHeaderWireSize = 4;
constexpr std::size_t kSelectForceCurveWireSize = 16;
constexpr std::size_t kRawAdcWireSize = 7;
constexpr std::size_t kForceCalibResultHeaderWireSize = 88; // 8 bytes + 20 x float32
constexpr std::size_t kForceCalibResultMaxWireSize =
    kForceCalibResultHeaderWireSize + kForceCalibMaxPoints * 4;

inline std::size_t setForceCalibrationWireSize(uint8_t point_count)
{
    return kSetForceCalibrationHeaderWireSize + static_cast<std::size_t>(point_count) * kForceCalibPointWireSize;
}

inline std::size_t forceCalibResultWireSize(uint8_t point_count)
{
    return kForceCalibResultHeaderWireSize + static_cast<std::size_t>(point_count) * 4;
}

inline std::size_t forcePathChunkWireSize(uint8_t sample_count)
{
    return kForcePathChunkHeaderWireSize + static_cast<std::size_t>(sample_count) * 8;
}

// CRC-16/CCITT-FALSE over frame bytes [0 .. 5 + payload_length] (header + payload).
// Polynomial 0x1021, initial value 0xFFFF, no input/output reflection.

} // namespace tecomm
