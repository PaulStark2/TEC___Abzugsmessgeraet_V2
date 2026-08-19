#pragma once

#include <cstdint>

namespace tecomm
{

// Protocol-native state enums (values match Core ordinals for M13 mapping).
enum class ProtocolMachineState : uint8_t
{
    Idle = 0,
    Active = 1,
    Paused = 2,
    Stopped = 3,
    Fault = 4,
    Homing = 5,
};

enum class ProtocolHomingState : uint8_t
{
    None = 0,
    HomingZ = 1,
    HomingX = 2,
    HomingY = 3,
    Homed = 4,
    HomingError = 5,
};

enum class ProtocolMotionState : uint8_t
{
    Idle = 0,
    Moving = 1,
    AtTarget = 2,
};

enum class ProtocolProcessState : uint8_t
{
    Idle = 0,
    Running = 1,
    Completed = 2,
    Aborted = 3,
};

// ACK result codes.
enum class AckResultCode : uint8_t
{
    Ok = 0,
    Rejected = 1,
    InvalidState = 2,
    ProgramEmpty = 3,
    FaultLatched = 4,
};

// NACK error codes.
enum class NackErrorCode : uint8_t
{
    Unknown = 0,
    InvalidPayload = 1,
    InvalidState = 2,
    Unsupported = 3,
};

// Single-frame decoder results (no exceptions).
enum class DecodeResult
{
    Ok,
    NeedMoreData,
    InvalidFrame,
    UnsupportedVersion,
    UnknownMessageId,
    InvalidLength,
    CrcError,
    InvalidPayload,
};

// Encoder results.
enum class EncodeError
{
    None,
    BufferTooSmall,
    PayloadTooLarge,
    InvalidPayload,
};

struct EncodeResult
{
    bool ok;
    std::size_t bytes_written;
    EncodeError error;
};

struct DecodedFrame
{
    uint8_t protocol_version;
    uint8_t message_id;
    const uint8_t* payload;
    std::size_t payload_length;
};

// Sentinel when current point index is not applicable.
constexpr uint8_t kNoCurrentPointIndex = 0xFF;

// Limit switch bitmask bit indices (StatusReportPayload.limits).
constexpr uint8_t kLimitBitXMin = 0;
constexpr uint8_t kLimitBitXMax = 1;
constexpr uint8_t kLimitBitYMin = 2;
constexpr uint8_t kLimitBitYMax = 3;
constexpr uint8_t kLimitBitZMin = 4;
constexpr uint8_t kLimitBitZMax = 5;

// StatusReportPayload.flags bit indices (not GUI byte offsets).
constexpr uint8_t kStatusFlagIsHomed = 0;
constexpr uint8_t kStatusFlagLimitWarning = 1;
constexpr uint8_t kStatusFlagCommWatchdog = 2;
constexpr uint8_t kStatusFlagForceFresh = 3;
constexpr uint8_t kStatusFlagPlateOriginValid = 4;
constexpr uint8_t kStatusFlagProbeCenterValid = 5;

} // namespace tecomm
