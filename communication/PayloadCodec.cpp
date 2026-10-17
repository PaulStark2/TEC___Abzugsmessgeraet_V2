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
    case MessageId::CmdSetManualSpeed:
        return kManualSpeedPayloadWireSize;
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
    case MessageId::CmdSetMachineConfig:
    case MessageId::MsgMachineConfig:
        return kMachineConfigPayloadWireSize;
    case MessageId::CmdSetTestConfiguration:
        return kTestConfigurationPayloadWireSize;
    case MessageId::CmdSetPlateOrigin:
        return kPlateOriginPayloadWireSize;
    case MessageId::CmdSetProbeCenter:
        return kPlateOriginPayloadWireSize;
    case MessageId::CmdRestoreMcs:
        return kPlateOriginPayloadWireSize;
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
        !writeU8(out, capacity, offset, payload.flags) ||
        !writeU8(out, capacity, offset, payload.procedure_phase) ||
        !writeU8(out, capacity, offset, payload.fault_reason) ||
        !writeU8(out, capacity, offset, payload.spreader_state))
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
        !readU8(data, length, offset, out.flags) ||
        !readU8(data, length, offset, out.procedure_phase) ||
        !readU8(data, length, offset, out.fault_reason) ||
        !readU8(data, length, offset, out.spreader_state))
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

bool encodeMachineConfigPayload(const MachineConfigPayload& payload, uint8_t* out, std::size_t capacity)
{
    std::size_t offset = 0;
    const bool ok =
        writeF32LE(out, capacity, offset, payload.v_approach_testpoints) &&
        writeF32LE(out, capacity, offset, payload.v_fine_testpoints) &&
        writeF32LE(out, capacity, offset, payload.v_unclamp) &&
        writeF32LE(out, capacity, offset, payload.v_homing) &&
        writeF32LE(out, capacity, offset, payload.a_testpoints) &&
        writeF32LE(out, capacity, offset, payload.corner_testpoints) &&
        writeF32LE(out, capacity, offset, payload.a_homing) &&
        writeF32LE(out, capacity, offset, payload.force_filter) &&
        writeF32LE(out, capacity, offset, payload.xy_tolerance_N) &&
        writeF32LE(out, capacity, offset, payload.xy_correction_step) &&
        writeF32LE(out, capacity, offset, payload.xy_max_correction) &&
        writeF32LE(out, capacity, offset, payload.insertion_buffer) &&
        writeF32LE(out, capacity, offset, payload.collision_approach_N) &&
        writeF32LE(out, capacity, offset, payload.collision_retract_N) &&
        writeF32LE(out, capacity, offset, payload.collision_clamp_N) &&
        writeF32LE(out, capacity, offset, payload.collision_unclamp_N) &&
        writeF32LE(out, capacity, offset, payload.collision_contact_N) &&
        writeF32LE(out, capacity, offset, payload.collision_measure_N) &&
        writeF32LE(out, capacity, offset, payload.spreader_extend_time_s) &&
        writeF32LE(out, capacity, offset, payload.spreader_retract_time_s) &&
        writeF32LE(out, capacity, offset, payload.force_tolerance_N) &&
        writeF32LE(out, capacity, offset, payload.z_correction_gain) &&
        writeF32LE(out, capacity, offset, payload.z_correction_max) &&
        writeF32LE(out, capacity, offset, payload.minimum_correction) &&
        writeU16LE(out, capacity, offset, payload.max_optimization_iterations) &&
        writeF32LE(out, capacity, offset, payload.v_search) &&
        writeF32LE(out, capacity, offset, payload.probe_search_distance) &&
        writeF32LE(out, capacity, offset, payload.probe_backoff) &&
        writeF32LE(out, capacity, offset, payload.home_offset_x) &&
        writeF32LE(out, capacity, offset, payload.home_offset_y) &&
        writeF32LE(out, capacity, offset, payload.home_offset_z) &&
        writeF32LE(out, capacity, offset, payload.tool_offset_x) &&
        writeF32LE(out, capacity, offset, payload.tool_offset_y) &&
        writeF32LE(out, capacity, offset, payload.tool_offset_z) &&
        writeF32LE(out, capacity, offset, payload.safe_z_buffer) &&
        writeF32LE(out, capacity, offset, payload.sample_timeout_s) &&
        writeF32LE(out, capacity, offset, payload.heartbeat_timeout_s) &&
        writeF32LE(out, capacity, offset, payload.xy_correct_timeout_s) &&
        writeF32LE(out, capacity, offset, payload.head_diameter) &&
        writeF32LE(out, capacity, offset, payload.measuring_head_diameter) &&
        writeF32LE(out, capacity, offset, payload.measuring_head_thickness) &&
        writeF32LE(out, capacity, offset, payload.tool_diameter) &&
        writeF32LE(out, capacity, offset, payload.search_depth) &&
        writeF32LE(out, capacity, offset, payload.approach_distance) &&
        writeF32LE(out, capacity, offset, payload.v_search_fine) &&
        writeF32LE(out, capacity, offset, payload.probe_offset_reference_x) &&
        writeF32LE(out, capacity, offset, payload.probe_offset_reference_y) &&
        writeF32LE(out, capacity, offset, payload.probe_offset_reference_z) &&
        writeF32LE(out, capacity, offset, payload.tool_cylindrical_height) &&
        writeF32LE(out, capacity, offset, payload.probe_xy_search_margin) &&
        writeF32LE(out, capacity, offset, payload.insert_search_distance) &&
        writeU16LE(out, capacity, offset, payload.num_fine_searches) &&
        writeF32LE(out, capacity, offset, payload.phase1_v_extract) &&
        writeF32LE(out, capacity, offset, payload.phase1_v_insert) &&
        writeF32LE(out, capacity, offset, payload.phase1_a) &&
        writeF32LE(out, capacity, offset, payload.phase1_corner) &&
        writeF32LE(out, capacity, offset, payload.phase2_v_extract) &&
        writeF32LE(out, capacity, offset, payload.phase2_v_insert) &&
        writeF32LE(out, capacity, offset, payload.phase2_a) &&
        writeF32LE(out, capacity, offset, payload.phase2_corner) &&
        writeF32LE(out, capacity, offset, payload.phase3_v_extract) &&
        writeF32LE(out, capacity, offset, payload.phase3_v_insert) &&
        writeF32LE(out, capacity, offset, payload.phase3_a) &&
        writeF32LE(out, capacity, offset, payload.phase3_corner) &&
        writeF32LE(out, capacity, offset, payload.v_approach_probe) &&
        writeF32LE(out, capacity, offset, payload.v_transfer_probe) &&
        writeF32LE(out, capacity, offset, payload.a_probe_transfer) &&
        writeF32LE(out, capacity, offset, payload.corner_probe_transfer) &&
        writeF32LE(out, capacity, offset, payload.a_probe_search) &&
        writeF32LE(out, capacity, offset, payload.a_probe_brake) &&
        writeF32LE(out, capacity, offset, payload.probe_center_x) &&
        writeF32LE(out, capacity, offset, payload.probe_center_y) &&
        writeF32LE(out, capacity, offset, payload.probe_center_z) &&
        writeF32LE(out, capacity, offset, payload.force_range_N) &&
        writeF32LE(out, capacity, offset, payload.force_hard_limit_N) &&
        writeF32LE(out, capacity, offset, payload.a_manual) &&
        writeF32LE(out, capacity, offset, payload.force_hard_limit_x_N) &&
        writeF32LE(out, capacity, offset, payload.force_hard_limit_y_N) &&
        writeF32LE(out, capacity, offset, payload.collision_approach_x_N) &&
        writeF32LE(out, capacity, offset, payload.collision_approach_y_N) &&
        writeF32LE(out, capacity, offset, payload.collision_retract_x_N) &&
        writeF32LE(out, capacity, offset, payload.collision_retract_y_N) &&
        writeF32LE(out, capacity, offset, payload.collision_clamp_x_N) &&
        writeF32LE(out, capacity, offset, payload.collision_clamp_y_N) &&
        writeF32LE(out, capacity, offset, payload.collision_unclamp_x_N) &&
        writeF32LE(out, capacity, offset, payload.collision_unclamp_y_N) &&
        writeF32LE(out, capacity, offset, payload.collision_contact_x_N) &&
        writeF32LE(out, capacity, offset, payload.collision_contact_y_N) &&
        writeF32LE(out, capacity, offset, payload.collision_measure_x_N) &&
        writeF32LE(out, capacity, offset, payload.collision_measure_y_N);
    return ok && offset == kMachineConfigPayloadWireSize;
}

bool decodeMachineConfigPayload(const uint8_t* data, std::size_t length, MachineConfigPayload& out)
{
    std::size_t offset = 0;
    const bool ok =
        readF32LE(data, length, offset, out.v_approach_testpoints, true) &&
        readF32LE(data, length, offset, out.v_fine_testpoints, true) &&
        readF32LE(data, length, offset, out.v_unclamp, true) &&
        readF32LE(data, length, offset, out.v_homing, true) &&
        readF32LE(data, length, offset, out.a_testpoints, true) &&
        readF32LE(data, length, offset, out.corner_testpoints, true) &&
        readF32LE(data, length, offset, out.a_homing, true) &&
        readF32LE(data, length, offset, out.force_filter, true) &&
        readF32LE(data, length, offset, out.xy_tolerance_N, true) &&
        readF32LE(data, length, offset, out.xy_correction_step, true) &&
        readF32LE(data, length, offset, out.xy_max_correction, true) &&
        readF32LE(data, length, offset, out.insertion_buffer, true) &&
        readF32LE(data, length, offset, out.collision_approach_N, true) &&
        readF32LE(data, length, offset, out.collision_retract_N, true) &&
        readF32LE(data, length, offset, out.collision_clamp_N, true) &&
        readF32LE(data, length, offset, out.collision_unclamp_N, true) &&
        readF32LE(data, length, offset, out.collision_contact_N, true) &&
        readF32LE(data, length, offset, out.collision_measure_N, true) &&
        readF32LE(data, length, offset, out.spreader_extend_time_s, true) &&
        readF32LE(data, length, offset, out.spreader_retract_time_s, true) &&
        readF32LE(data, length, offset, out.force_tolerance_N, true) &&
        readF32LE(data, length, offset, out.z_correction_gain, true) &&
        readF32LE(data, length, offset, out.z_correction_max, true) &&
        readF32LE(data, length, offset, out.minimum_correction, true) &&
        readU16LE(data, length, offset, out.max_optimization_iterations) &&
        readF32LE(data, length, offset, out.v_search, true) &&
        readF32LE(data, length, offset, out.probe_search_distance, true) &&
        readF32LE(data, length, offset, out.probe_backoff, true) &&
        readF32LE(data, length, offset, out.home_offset_x, true) &&
        readF32LE(data, length, offset, out.home_offset_y, true) &&
        readF32LE(data, length, offset, out.home_offset_z, true) &&
        readF32LE(data, length, offset, out.tool_offset_x, true) &&
        readF32LE(data, length, offset, out.tool_offset_y, true) &&
        readF32LE(data, length, offset, out.tool_offset_z, true) &&
        readF32LE(data, length, offset, out.safe_z_buffer, true) &&
        readF32LE(data, length, offset, out.sample_timeout_s, true) &&
        readF32LE(data, length, offset, out.heartbeat_timeout_s, true) &&
        readF32LE(data, length, offset, out.xy_correct_timeout_s, true) &&
        readF32LE(data, length, offset, out.head_diameter, true) &&
        readF32LE(data, length, offset, out.measuring_head_diameter, true) &&
        readF32LE(data, length, offset, out.measuring_head_thickness, true) &&
        readF32LE(data, length, offset, out.tool_diameter, true) &&
        readF32LE(data, length, offset, out.search_depth, true) &&
        readF32LE(data, length, offset, out.approach_distance, true) &&
        readF32LE(data, length, offset, out.v_search_fine, true) &&
        readF32LE(data, length, offset, out.probe_offset_reference_x, true) &&
        readF32LE(data, length, offset, out.probe_offset_reference_y, true) &&
        readF32LE(data, length, offset, out.probe_offset_reference_z, true) &&
        readF32LE(data, length, offset, out.tool_cylindrical_height, true) &&
        readF32LE(data, length, offset, out.probe_xy_search_margin, true) &&
        readF32LE(data, length, offset, out.insert_search_distance, true) &&
        readU16LE(data, length, offset, out.num_fine_searches) &&
        readF32LE(data, length, offset, out.phase1_v_extract, true) &&
        readF32LE(data, length, offset, out.phase1_v_insert, true) &&
        readF32LE(data, length, offset, out.phase1_a, true) &&
        readF32LE(data, length, offset, out.phase1_corner, true) &&
        readF32LE(data, length, offset, out.phase2_v_extract, true) &&
        readF32LE(data, length, offset, out.phase2_v_insert, true) &&
        readF32LE(data, length, offset, out.phase2_a, true) &&
        readF32LE(data, length, offset, out.phase2_corner, true) &&
        readF32LE(data, length, offset, out.phase3_v_extract, true) &&
        readF32LE(data, length, offset, out.phase3_v_insert, true) &&
        readF32LE(data, length, offset, out.phase3_a, true) &&
        readF32LE(data, length, offset, out.phase3_corner, true) &&
        readF32LE(data, length, offset, out.v_approach_probe, true) &&
        readF32LE(data, length, offset, out.v_transfer_probe, true) &&
        readF32LE(data, length, offset, out.a_probe_transfer, true) &&
        readF32LE(data, length, offset, out.corner_probe_transfer, true) &&
        readF32LE(data, length, offset, out.a_probe_search, true) &&
        readF32LE(data, length, offset, out.a_probe_brake, true) &&
        readF32LE(data, length, offset, out.probe_center_x, true) &&
        readF32LE(data, length, offset, out.probe_center_y, true) &&
        readF32LE(data, length, offset, out.probe_center_z, true) &&
        readF32LE(data, length, offset, out.force_range_N, true) &&
        readF32LE(data, length, offset, out.force_hard_limit_N, true) &&
        readF32LE(data, length, offset, out.a_manual, true) &&
        readF32LE(data, length, offset, out.force_hard_limit_x_N, true) &&
        readF32LE(data, length, offset, out.force_hard_limit_y_N, true) &&
        readF32LE(data, length, offset, out.collision_approach_x_N, true) &&
        readF32LE(data, length, offset, out.collision_approach_y_N, true) &&
        readF32LE(data, length, offset, out.collision_retract_x_N, true) &&
        readF32LE(data, length, offset, out.collision_retract_y_N, true) &&
        readF32LE(data, length, offset, out.collision_clamp_x_N, true) &&
        readF32LE(data, length, offset, out.collision_clamp_y_N, true) &&
        readF32LE(data, length, offset, out.collision_unclamp_x_N, true) &&
        readF32LE(data, length, offset, out.collision_unclamp_y_N, true) &&
        readF32LE(data, length, offset, out.collision_contact_x_N, true) &&
        readF32LE(data, length, offset, out.collision_contact_y_N, true) &&
        readF32LE(data, length, offset, out.collision_measure_x_N, true) &&
        readF32LE(data, length, offset, out.collision_measure_y_N, true);
    return ok && offset == length && length == kMachineConfigPayloadWireSize;
}

bool encodeTestConfigurationPayload(const TestConfigurationPayload& payload, uint8_t* out,
                                    std::size_t capacity)
{
    if (payload.mode > 1)
    {
        return false;
    }
    std::size_t offset = 0;
    return writeU8(out, capacity, offset, payload.mode) &&
           writeF32LE(out, capacity, offset, payload.target_force_N) &&
           writeU16LE(out, capacity, offset, payload.iterations) &&
           writeU8(out, capacity, offset, payload.active_point_index) &&
           offset == kTestConfigurationPayloadWireSize;
}

bool decodeTestConfigurationPayload(const uint8_t* data, std::size_t length, TestConfigurationPayload& out)
{
    std::size_t offset = 0;
    if (!readU8(data, length, offset, out.mode) ||
        !readF32LE(data, length, offset, out.target_force_N, true) ||
        !readU16LE(data, length, offset, out.iterations) ||
        !readU8(data, length, offset, out.active_point_index))
    {
        return false;
    }
    return out.mode <= 1 && offset == length && length == kTestConfigurationPayloadWireSize;
}

EncodeResult encodeMessage(uint8_t message_id, const MachineConfigPayload& payload, uint8_t* out,
                           std::size_t out_capacity)
{
    uint8_t buffer[kMachineConfigPayloadWireSize];
    if (!encodeMachineConfigPayload(payload, buffer, sizeof(buffer)))
    {
        EncodeResult result{};
        result.ok = false;
        result.error = EncodeError::InvalidPayload;
        return result;
    }
    return encodePayloadBuffer(message_id, buffer, sizeof(buffer), out, out_capacity);
}

EncodeResult encodeMessage(uint8_t message_id, const TestConfigurationPayload& payload, uint8_t* out,
                           std::size_t out_capacity)
{
    uint8_t buffer[kTestConfigurationPayloadWireSize];
    if (!encodeTestConfigurationPayload(payload, buffer, sizeof(buffer)))
    {
        EncodeResult result{};
        result.ok = false;
        result.error = EncodeError::InvalidPayload;
        return result;
    }
    return encodePayloadBuffer(message_id, buffer, sizeof(buffer), out, out_capacity);
}

bool encodeForcePathChunkPayload(const ForcePathChunkPayload& payload, uint8_t* out, std::size_t capacity,
                                 std::size_t& bytes_written)
{
    bytes_written = 0;
    if (payload.sample_count > kForcePathChunkMaxSamples)
    {
        return false;
    }
    std::size_t offset = 0;
    if (!writeU8(out, capacity, offset, payload.point_index) ||
        !writeU16LE(out, capacity, offset, payload.iteration) ||
        !writeU32LE(out, capacity, offset, payload.sequence) ||
        !writeU8(out, capacity, offset, payload.sample_count) ||
        !writeU32LE(out, capacity, offset, payload.dropped))
    {
        return false;
    }
    for (uint8_t i = 0; i < payload.sample_count; ++i)
    {
        if (!writeF32LE(out, capacity, offset, payload.z[i]) ||
            !writeF32LE(out, capacity, offset, payload.fz[i]))
        {
            return false;
        }
    }
    bytes_written = offset;
    return offset == forcePathChunkWireSize(payload.sample_count);
}

bool decodeForcePathChunkPayload(const uint8_t* data, std::size_t length, ForcePathChunkPayload& out)
{
    out = ForcePathChunkPayload{};
    std::size_t offset = 0;
    if (!readU8(data, length, offset, out.point_index) || !readU16LE(data, length, offset, out.iteration) ||
        !readU32LE(data, length, offset, out.sequence) || !readU8(data, length, offset, out.sample_count) ||
        !readU32LE(data, length, offset, out.dropped))
    {
        return false;
    }
    if (out.sample_count > kForcePathChunkMaxSamples)
    {
        return false;
    }
    if (length != forcePathChunkWireSize(out.sample_count))
    {
        return false;
    }
    for (uint8_t i = 0; i < out.sample_count; ++i)
    {
        if (!readF32LE(data, length, offset, out.z[i], true) ||
            !readF32LE(data, length, offset, out.fz[i], true))
        {
            return false;
        }
    }
    return offset == length;
}

EncodeResult encodeMessage(uint8_t message_id, const ForcePathChunkPayload& payload, uint8_t* out,
                           std::size_t out_capacity)
{
    uint8_t buffer[kForcePathChunkMaxPayloadWireSize];
    std::size_t payload_size = 0;
    if (!encodeForcePathChunkPayload(payload, buffer, sizeof(buffer), payload_size))
    {
        EncodeResult result{};
        result.ok = false;
        result.error = EncodeError::InvalidPayload;
        return result;
    }
    return encodePayloadBuffer(message_id, buffer, payload_size, out, out_capacity);
}

bool encodeSetForceCalibrationPayload(const SetForceCalibrationPayload& payload, uint8_t* out, std::size_t capacity,
                                      std::size_t& bytes_written)
{
    if (payload.count > kForceCalibMaxPoints || payload.source > 2 || payload.confirm > 1 || payload.commit > 1)
    {
        return false;
    }
    std::size_t offset = 0;
    if (!writeU8(out, capacity, offset, payload.source) || !writeU8(out, capacity, offset, payload.confirm) ||
        !writeU8(out, capacity, offset, payload.commit) || !writeU8(out, capacity, offset, payload.count))
    {
        return false;
    }
    for (uint8_t i = 0; i < payload.count; ++i)
    {
        const ForceCalibPointPayload& point = payload.points[i];
        if (point.direction > 2 || !isFiniteFloat(point.mass_g) || !isFiniteFloat(point.nominal_g))
        {
            return false;
        }
        if (!writeU16LE(out, capacity, offset, point.adc_net) || !writeU8(out, capacity, offset, point.direction) ||
            !writeU8(out, capacity, offset, 0) || !writeF32LE(out, capacity, offset, point.mass_g) ||
            !writeF32LE(out, capacity, offset, point.nominal_g))
        {
            return false;
        }
    }
    bytes_written = offset;
    return offset == setForceCalibrationWireSize(payload.count);
}

bool decodeSetForceCalibrationPayload(const uint8_t* data, std::size_t length, SetForceCalibrationPayload& out)
{
    std::size_t offset = 0;
    if (!readU8(data, length, offset, out.source) || !readU8(data, length, offset, out.confirm) ||
        !readU8(data, length, offset, out.commit) || !readU8(data, length, offset, out.count))
    {
        return false;
    }
    if (out.source > 2 || out.confirm > 1 || out.commit > 1 || out.count > kForceCalibMaxPoints || out.count < 2)
    {
        return false;
    }
    if (length != setForceCalibrationWireSize(out.count))
    {
        return false;
    }
    for (uint8_t i = 0; i < out.count; ++i)
    {
        uint8_t pad = 0;
        if (!readU16LE(data, length, offset, out.points[i].adc_net) ||
            !readU8(data, length, offset, out.points[i].direction) || !readU8(data, length, offset, pad) ||
            !readF32LE(data, length, offset, out.points[i].mass_g, true) ||
            !readF32LE(data, length, offset, out.points[i].nominal_g, true))
        {
            return false;
        }
        if (out.points[i].direction > 2)
        {
            return false;
        }
    }
    return offset == length;
}

bool encodeSelectForceCurvePayload(const SelectForceCurvePayload& payload, uint8_t* out, std::size_t capacity)
{
    if (payload.source > 3 || payload.confirm > 1 || payload.unit > 1 || payload.commit > 1)
    {
        return false;
    }
    if (!isFiniteFloat(payload.beta0) || !isFiniteFloat(payload.beta1) || !isFiniteFloat(payload.beta2))
    {
        return false;
    }
    std::size_t offset = 0;
    return writeU8(out, capacity, offset, payload.source) && writeU8(out, capacity, offset, payload.confirm) &&
           writeU8(out, capacity, offset, payload.unit) && writeU8(out, capacity, offset, payload.commit) &&
           writeF32LE(out, capacity, offset, payload.beta0) && writeF32LE(out, capacity, offset, payload.beta1) &&
           writeF32LE(out, capacity, offset, payload.beta2) && offset == kSelectForceCurveWireSize;
}

bool decodeSelectForceCurvePayload(const uint8_t* data, std::size_t length, SelectForceCurvePayload& out)
{
    if (length != kSelectForceCurveWireSize)
    {
        return false;
    }
    std::size_t offset = 0;
    if (!readU8(data, length, offset, out.source) || !readU8(data, length, offset, out.confirm) ||
        !readU8(data, length, offset, out.unit) || !readU8(data, length, offset, out.commit) ||
        !readF32LE(data, length, offset, out.beta0, true) || !readF32LE(data, length, offset, out.beta1, true) ||
        !readF32LE(data, length, offset, out.beta2, true))
    {
        return false;
    }
    return out.source <= 3 && out.confirm <= 1 && out.unit <= 1 && out.commit <= 1 && offset == length;
}

bool encodeRawAdcPayload(const RawAdcPayload& payload, uint8_t* out, std::size_t capacity)
{
    if (payload.valid > 1)
    {
        return false;
    }
    std::size_t offset = 0;
    return writeU16LE(out, capacity, offset, payload.x) && writeU16LE(out, capacity, offset, payload.y) &&
           writeU16LE(out, capacity, offset, payload.z) && writeU8(out, capacity, offset, payload.valid) &&
           offset == kRawAdcWireSize;
}

bool decodeRawAdcPayload(const uint8_t* data, std::size_t length, RawAdcPayload& out)
{
    if (length != kRawAdcWireSize)
    {
        return false;
    }
    std::size_t offset = 0;
    return readU16LE(data, length, offset, out.x) && readU16LE(data, length, offset, out.y) &&
           readU16LE(data, length, offset, out.z) && readU8(data, length, offset, out.valid) && out.valid <= 1 &&
           offset == length;
}

bool encodeForceCalibrationResultPayload(const ForceCalibrationResultPayload& payload, uint8_t* out,
                                         std::size_t capacity, std::size_t& bytes_written)
{
    if (payload.point_count > kForceCalibMaxPoints)
    {
        return false;
    }
    const float fields[] = {payload.beta0,
                            payload.beta1,
                            payload.beta2,
                            payload.pearson_r,
                            payload.r_squared,
                            payload.max_abs_residual_N,
                            payload.mean_residual_tension_N,
                            payload.mean_residual_compression_N,
                            payload.linear_max_abs_N,
                            payload.quadratic_max_abs_N,
                            payload.linear_r2,
                            payload.quadratic_r2,
                            payload.linear_beta0,
                            payload.linear_beta1,
                            payload.linear_beta2,
                            payload.linear_valid,
                            payload.quadratic_beta0,
                            payload.quadratic_beta1,
                            payload.quadratic_beta2,
                            payload.quadratic_valid};
    for (float value : fields)
    {
        if (!isFiniteFloat(value))
        {
            return false;
        }
    }
    std::size_t offset = 0;
    if (!writeU8(out, capacity, offset, payload.requested_source) ||
        !writeU8(out, capacity, offset, payload.resolved_source) ||
        !writeU8(out, capacity, offset, payload.committed) || !writeU8(out, capacity, offset, payload.reason) ||
        !writeU8(out, capacity, offset, payload.degree) ||
        !writeU8(out, capacity, offset, payload.needs_confirmation) ||
        !writeU8(out, capacity, offset, payload.worst_index) ||
        !writeU8(out, capacity, offset, payload.point_count))
    {
        return false;
    }
    for (float value : fields)
    {
        if (!writeF32LE(out, capacity, offset, value))
        {
            return false;
        }
    }
    for (uint8_t i = 0; i < payload.point_count; ++i)
    {
        if (!isFiniteFloat(payload.reference_force_N[i]) ||
            !writeF32LE(out, capacity, offset, payload.reference_force_N[i]))
        {
            return false;
        }
    }
    bytes_written = offset;
    return offset == forceCalibResultWireSize(payload.point_count);
}

bool decodeForceCalibrationResultPayload(const uint8_t* data, std::size_t length, ForceCalibrationResultPayload& out)
{
    std::size_t offset = 0;
    if (!readU8(data, length, offset, out.requested_source) || !readU8(data, length, offset, out.resolved_source) ||
        !readU8(data, length, offset, out.committed) || !readU8(data, length, offset, out.reason) ||
        !readU8(data, length, offset, out.degree) || !readU8(data, length, offset, out.needs_confirmation) ||
        !readU8(data, length, offset, out.worst_index) || !readU8(data, length, offset, out.point_count))
    {
        return false;
    }
    if (out.committed > 1 || out.needs_confirmation > 1 || out.point_count > kForceCalibMaxPoints)
    {
        return false;
    }
    if (length != forceCalibResultWireSize(out.point_count))
    {
        return false;
    }
    float* fields[] = {&out.beta0,
                       &out.beta1,
                       &out.beta2,
                       &out.pearson_r,
                       &out.r_squared,
                       &out.max_abs_residual_N,
                       &out.mean_residual_tension_N,
                       &out.mean_residual_compression_N,
                       &out.linear_max_abs_N,
                       &out.quadratic_max_abs_N,
                       &out.linear_r2,
                       &out.quadratic_r2,
                       &out.linear_beta0,
                       &out.linear_beta1,
                       &out.linear_beta2,
                       &out.linear_valid,
                       &out.quadratic_beta0,
                       &out.quadratic_beta1,
                       &out.quadratic_beta2,
                       &out.quadratic_valid};
    for (float* field : fields)
    {
        if (!readF32LE(data, length, offset, *field, true))
        {
            return false;
        }
    }
    for (uint8_t i = 0; i < out.point_count; ++i)
    {
        if (!readF32LE(data, length, offset, out.reference_force_N[i], true))
        {
            return false;
        }
    }
    return offset == length;
}

EncodeResult encodeMessage(uint8_t message_id, const SetForceCalibrationPayload& payload, uint8_t* out,
                           std::size_t out_capacity)
{
    uint8_t buffer[kSetForceCalibrationHeaderWireSize + kForceCalibMaxPoints * kForceCalibPointWireSize];
    std::size_t payload_size = 0;
    if (!encodeSetForceCalibrationPayload(payload, buffer, sizeof(buffer), payload_size))
    {
        EncodeResult result{};
        result.ok = false;
        result.error = EncodeError::InvalidPayload;
        return result;
    }
    return encodePayloadBuffer(message_id, buffer, payload_size, out, out_capacity);
}

EncodeResult encodeMessage(uint8_t message_id, const SelectForceCurvePayload& payload, uint8_t* out,
                           std::size_t out_capacity)
{
    uint8_t buffer[kSelectForceCurveWireSize];
    if (!encodeSelectForceCurvePayload(payload, buffer, sizeof(buffer)))
    {
        EncodeResult result{};
        result.ok = false;
        result.error = EncodeError::InvalidPayload;
        return result;
    }
    return encodePayloadBuffer(message_id, buffer, kSelectForceCurveWireSize, out, out_capacity);
}

EncodeResult encodeMessage(uint8_t message_id, const RawAdcPayload& payload, uint8_t* out, std::size_t out_capacity)
{
    uint8_t buffer[kRawAdcWireSize];
    if (!encodeRawAdcPayload(payload, buffer, sizeof(buffer)))
    {
        EncodeResult result{};
        result.ok = false;
        result.error = EncodeError::InvalidPayload;
        return result;
    }
    return encodePayloadBuffer(message_id, buffer, kRawAdcWireSize, out, out_capacity);
}

EncodeResult encodeMessage(uint8_t message_id, const ForceCalibrationResultPayload& payload, uint8_t* out,
                           std::size_t out_capacity)
{
    uint8_t buffer[kForceCalibResultMaxWireSize];
    std::size_t payload_size = 0;
    if (!encodeForceCalibrationResultPayload(payload, buffer, sizeof(buffer), payload_size))
    {
        EncodeResult result{};
        result.ok = false;
        result.error = EncodeError::InvalidPayload;
        return result;
    }
    return encodePayloadBuffer(message_id, buffer, payload_size, out, out_capacity);
}

EncodeResult encodeEmptyMessage(uint8_t message_id, uint8_t* out, std::size_t out_capacity)
{
    return encodeFrame(message_id, nullptr, 0, out, out_capacity);
}

} // namespace tecomm
