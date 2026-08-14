#include "PayloadCodec.hpp"

#include "MessageIds.hpp"
#include "ProtocolSpec.hpp"
#include "WireUtil.hpp"

namespace tecomm
{

namespace
{

bool validateBoolByte(uint8_t value)
{
    return value == 0 || value == 1;
}

EncodeResult encodePayloadBuffer(uint8_t message_id, const uint8_t* payload, std::size_t payload_size,
                                 uint8_t* out, std::size_t out_capacity)
{
    EncodeResult encoded = encodeFrame(message_id, payload, payload_size, out, out_capacity);
    if (!encoded.ok && encoded.error == EncodeError::None)
    {
        encoded.error = EncodeError::InvalidPayload;
    }
    return encoded;
}

} // namespace

std::size_t encodedPayloadSize(uint8_t message_id, const LoadProgramPayload* load_program)
{
    switch (static_cast<MessageId>(message_id))
    {
    case MessageId::CmdMoveTo:
        return kMoveToPayloadWireSize;
    case MessageId::CmdSetSpreader:
    case MessageId::CmdSetFixture:
        return kBoolPayloadWireSize;
    case MessageId::CmdLoadProgram:
        if (load_program == nullptr)
        {
            return 0;
        }
        return 1 + static_cast<std::size_t>(load_program->point_count) * kMoveToPayloadWireSize;
    case MessageId::RspAck:
        return kAckPayloadWireSize;
    case MessageId::RspNack:
        return kNackPayloadWireSize;
    case MessageId::MsgStatusReport:
        return kStatusReportPayloadWireSize;
    case MessageId::MsgProgramSummary:
        return kProgramSummaryPayloadWireSize;
    default:
        return 0;
    }
}

bool encodeMoveToPayload(const MoveToPayload& payload, uint8_t* out, std::size_t capacity)
{
    std::size_t offset = 0;
    return writeF32LE(out, capacity, offset, payload.x) && writeF32LE(out, capacity, offset, payload.y) &&
           writeF32LE(out, capacity, offset, payload.z) && offset == kMoveToPayloadWireSize;
}

bool decodeMoveToPayload(const uint8_t* data, std::size_t length, MoveToPayload& out)
{
    std::size_t offset = 0;
    return readF32LE(data, length, offset, out.x, true) && readF32LE(data, length, offset, out.y, true) &&
           readF32LE(data, length, offset, out.z, true) && offset == length;
}

bool encodeBoolPayload(const BoolPayload& payload, uint8_t* out, std::size_t capacity)
{
    if (payload.value != 0 && payload.value != 1)
    {
        return false;
    }
    std::size_t offset = 0;
    return writeU8(out, capacity, offset, payload.value) && offset == kBoolPayloadWireSize;
}

bool decodeBoolPayload(const uint8_t* data, std::size_t length, BoolPayload& out)
{
    std::size_t offset = 0;
    if (!readU8(data, length, offset, out.value))
    {
        return false;
    }
    return (out.value == 0 || out.value == 1) && offset == length;
}

bool encodeLoadProgramPayload(const LoadProgramPayload& payload, uint8_t* out, std::size_t capacity,
                              std::size_t& bytes_written)
{
    bytes_written = 0;
    if (payload.point_count > kLoadProgramMaxPointCount)
    {
        return false;
    }
    std::size_t offset = 0;
    if (!writeU8(out, capacity, offset, payload.point_count))
    {
        return false;
    }
    for (uint8_t i = 0; i < payload.point_count; ++i)
    {
        if (!writeF32LE(out, capacity, offset, payload.points[i].x) ||
            !writeF32LE(out, capacity, offset, payload.points[i].y) ||
            !writeF32LE(out, capacity, offset, payload.points[i].z))
        {
            return false;
        }
    }
    bytes_written = offset;
    return true;
}

bool decodeLoadProgramPayload(const uint8_t* data, std::size_t length, LoadProgramPayload& out)
{
    out.point_count = 0;
    std::size_t offset = 0;
    if (!readU8(data, length, offset, out.point_count))
    {
        return false;
    }
    if (out.point_count > kLoadProgramMaxPointCount)
    {
        return false;
    }
    const std::size_t expected = 1 + static_cast<std::size_t>(out.point_count) * kMoveToPayloadWireSize;
    if (length != expected)
    {
        return false;
    }
    for (uint8_t i = 0; i < out.point_count; ++i)
    {
        if (!readF32LE(data, length, offset, out.points[i].x, true) ||
            !readF32LE(data, length, offset, out.points[i].y, true) ||
            !readF32LE(data, length, offset, out.points[i].z, true))
        {
            return false;
        }
    }
    return offset == length;
}

bool encodeStatusReportPayload(const StatusReportPayload& payload, uint8_t* out, std::size_t capacity)
{
    std::size_t offset = 0;
    if (!writeU8(out, capacity, offset, static_cast<uint8_t>(payload.machine_state)) ||
        !writeU8(out, capacity, offset, static_cast<uint8_t>(payload.motion_state)) ||
        !writeU8(out, capacity, offset, static_cast<uint8_t>(payload.process_state)) ||
        !writeF32LE(out, capacity, offset, payload.x) || !writeF32LE(out, capacity, offset, payload.y) ||
        !writeF32LE(out, capacity, offset, payload.z) || !writeF32LE(out, capacity, offset, payload.fx) ||
        !writeF32LE(out, capacity, offset, payload.fy) || !writeF32LE(out, capacity, offset, payload.fz) ||
        !writeU8(out, capacity, offset, payload.limits) || !writeU8(out, capacity, offset, payload.e_stop) ||
        !writeU8(out, capacity, offset, payload.spreader_extend) ||
        !writeU8(out, capacity, offset, payload.spreader_extended) ||
        !writeU8(out, capacity, offset, payload.fixture_extend) ||
        !writeU8(out, capacity, offset, payload.fixture_extended) ||
        !writeU8(out, capacity, offset, payload.current_point_index) ||
        !writeU8(out, capacity, offset, payload.program_point_count) ||
        !writeU8(out, capacity, offset, payload.homing_state) ||
        !writeU8(out, capacity, offset, payload.blocked_directions) ||
        !writeU8(out, capacity, offset, payload.flags))
    {
        return false;
    }
    return offset == kStatusReportPayloadWireSize;
}

bool decodeStatusReportPayload(const uint8_t* data, std::size_t length, StatusReportPayload& out)
{
    if (length != kStatusReportPayloadWireSize)
    {
        return false;
    }
    std::size_t offset = 0;
    uint8_t machine_state = 0;
    uint8_t motion_state = 0;
    uint8_t process_state = 0;
    if (!readU8(data, length, offset, machine_state) || !readU8(data, length, offset, motion_state) ||
        !readU8(data, length, offset, process_state) ||
        !readF32LE(data, length, offset, out.x, true) || !readF32LE(data, length, offset, out.y, true) ||
        !readF32LE(data, length, offset, out.z, true) || !readF32LE(data, length, offset, out.fx, true) ||
        !readF32LE(data, length, offset, out.fy, true) || !readF32LE(data, length, offset, out.fz, true) ||
        !readU8(data, length, offset, out.limits) || !readU8(data, length, offset, out.e_stop) ||
        !readU8(data, length, offset, out.spreader_extend) ||
        !readU8(data, length, offset, out.spreader_extended) ||
        !readU8(data, length, offset, out.fixture_extend) ||
        !readU8(data, length, offset, out.fixture_extended) ||
        !readU8(data, length, offset, out.current_point_index) ||
        !readU8(data, length, offset, out.program_point_count) ||
        !readU8(data, length, offset, out.homing_state) ||
        !readU8(data, length, offset, out.blocked_directions) ||
        !readU8(data, length, offset, out.flags))
    {
        return false;
    }
    if (!validateBoolByte(out.e_stop) || !validateBoolByte(out.spreader_extend) ||
        !validateBoolByte(out.spreader_extended) || !validateBoolByte(out.fixture_extend) ||
        !validateBoolByte(out.fixture_extended))
    {
        return false;
    }
    out.machine_state = static_cast<ProtocolMachineState>(machine_state);
    out.motion_state = static_cast<ProtocolMotionState>(motion_state);
    out.process_state = static_cast<ProtocolProcessState>(process_state);
    return offset == length;
}

bool encodeProgramSummaryPayload(const ProgramSummaryPayload& payload, uint8_t* out, std::size_t capacity)
{
    std::size_t offset = 0;
    if (!writeU8(out, capacity, offset, payload.point_count) ||
        !writeU8(out, capacity, offset, payload.current_index) ||
        !writeF32LE(out, capacity, offset, payload.x_min) ||
        !writeF32LE(out, capacity, offset, payload.x_max) ||
        !writeF32LE(out, capacity, offset, payload.y_min) ||
        !writeF32LE(out, capacity, offset, payload.y_max) ||
        !writeF32LE(out, capacity, offset, payload.z_min) ||
        !writeF32LE(out, capacity, offset, payload.z_max))
    {
        return false;
    }
    return offset == kProgramSummaryPayloadWireSize;
}

bool decodeProgramSummaryPayload(const uint8_t* data, std::size_t length, ProgramSummaryPayload& out)
{
    if (length != kProgramSummaryPayloadWireSize)
    {
        return false;
    }
    std::size_t offset = 0;
    if (!readU8(data, length, offset, out.point_count) ||
        !readU8(data, length, offset, out.current_index) ||
        !readF32LE(data, length, offset, out.x_min, true) ||
        !readF32LE(data, length, offset, out.x_max, true) ||
        !readF32LE(data, length, offset, out.y_min, true) ||
        !readF32LE(data, length, offset, out.y_max, true) ||
        !readF32LE(data, length, offset, out.z_min, true) ||
        !readF32LE(data, length, offset, out.z_max, true))
    {
        return false;
    }
    return offset == length;
}

bool encodeAckPayload(const AckPayload& payload, uint8_t* out, std::size_t capacity)
{
    std::size_t offset = 0;
    return writeU8(out, capacity, offset, payload.ref_message_id) &&
           writeU8(out, capacity, offset, payload.result_code) && offset == kAckPayloadWireSize;
}

bool decodeAckPayload(const uint8_t* data, std::size_t length, AckPayload& out)
{
    std::size_t offset = 0;
    return readU8(data, length, offset, out.ref_message_id) &&
           readU8(data, length, offset, out.result_code) && offset == length;
}

bool encodeNackPayload(const NackPayload& payload, uint8_t* out, std::size_t capacity)
{
    std::size_t offset = 0;
    return writeU8(out, capacity, offset, payload.ref_message_id) &&
           writeU8(out, capacity, offset, payload.error_code) &&
           writeU8(out, capacity, offset, payload.detail) && offset == kNackPayloadWireSize;
}

bool decodeNackPayload(const uint8_t* data, std::size_t length, NackPayload& out)
{
    std::size_t offset = 0;
    return readU8(data, length, offset, out.ref_message_id) &&
           readU8(data, length, offset, out.error_code) && readU8(data, length, offset, out.detail) &&
           offset == length;
}

EncodeResult encodeMessage(uint8_t message_id, const MoveToPayload& payload, uint8_t* out,
                           std::size_t out_capacity)
{
    uint8_t buffer[kMoveToPayloadWireSize];
    if (!encodeMoveToPayload(payload, buffer, sizeof(buffer)))
    {
        EncodeResult result{};
        result.ok = false;
        result.error = EncodeError::InvalidPayload;
        return result;
    }
    return encodePayloadBuffer(message_id, buffer, sizeof(buffer), out, out_capacity);
}

EncodeResult encodeMessage(uint8_t message_id, const BoolPayload& payload, uint8_t* out,
                           std::size_t out_capacity)
{
    uint8_t buffer[kBoolPayloadWireSize];
    if (!encodeBoolPayload(payload, buffer, sizeof(buffer)))
    {
        EncodeResult result{};
        result.ok = false;
        result.error = EncodeError::InvalidPayload;
        return result;
    }
    return encodePayloadBuffer(message_id, buffer, sizeof(buffer), out, out_capacity);
}

EncodeResult encodeMessage(uint8_t message_id, const LoadProgramPayload& payload, uint8_t* out,
                           std::size_t out_capacity)
{
    uint8_t buffer[kLoadProgramMaxPayloadWireSize];
    std::size_t payload_size = 0;
    if (!encodeLoadProgramPayload(payload, buffer, sizeof(buffer), payload_size))
    {
        EncodeResult result{};
        result.ok = false;
        result.error = EncodeError::InvalidPayload;
        return result;
    }
    return encodePayloadBuffer(message_id, buffer, payload_size, out, out_capacity);
}

EncodeResult encodeMessage(uint8_t message_id, const StatusReportPayload& payload, uint8_t* out,
                           std::size_t out_capacity)
{
    uint8_t buffer[kStatusReportPayloadWireSize];
    if (!encodeStatusReportPayload(payload, buffer, sizeof(buffer)))
    {
        EncodeResult result{};
        result.ok = false;
        result.error = EncodeError::InvalidPayload;
        return result;
    }
    return encodePayloadBuffer(message_id, buffer, sizeof(buffer), out, out_capacity);
}

EncodeResult encodeMessage(uint8_t message_id, const ProgramSummaryPayload& payload, uint8_t* out,
                           std::size_t out_capacity)
{
    uint8_t buffer[kProgramSummaryPayloadWireSize];
    if (!encodeProgramSummaryPayload(payload, buffer, sizeof(buffer)))
    {
        EncodeResult result{};
        result.ok = false;
        result.error = EncodeError::InvalidPayload;
        return result;
    }
    return encodePayloadBuffer(message_id, buffer, sizeof(buffer), out, out_capacity);
}

EncodeResult encodeMessage(uint8_t message_id, const AckPayload& payload, uint8_t* out,
                           std::size_t out_capacity)
{
    uint8_t buffer[kAckPayloadWireSize];
    if (!encodeAckPayload(payload, buffer, sizeof(buffer)))
    {
        EncodeResult result{};
        result.ok = false;
        result.error = EncodeError::InvalidPayload;
        return result;
    }
    return encodePayloadBuffer(message_id, buffer, sizeof(buffer), out, out_capacity);
}

EncodeResult encodeMessage(uint8_t message_id, const NackPayload& payload, uint8_t* out,
                           std::size_t out_capacity)
{
    uint8_t buffer[kNackPayloadWireSize];
    if (!encodeNackPayload(payload, buffer, sizeof(buffer)))
    {
        EncodeResult result{};
        result.ok = false;
        result.error = EncodeError::InvalidPayload;
        return result;
    }
    return encodePayloadBuffer(message_id, buffer, sizeof(buffer), out, out_capacity);
}

EncodeResult encodeEmptyMessage(uint8_t message_id, uint8_t* out, std::size_t out_capacity)
{
    return encodeFrame(message_id, nullptr, 0, out, out_capacity);
}

} // namespace tecomm
