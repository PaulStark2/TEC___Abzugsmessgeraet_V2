// Deterministic M12 communication protocol roundtrip and error tests (no Core dependency).
#include "Crc16.hpp"
#include "Decoder.hpp"
#include "Encoder.hpp"
#include "MessageIds.hpp"
#include "PayloadCodec.hpp"
#include "Payloads.hpp"
#include "ProtocolSpec.hpp"
#include "ProtocolTypes.hpp"
#include "WireUtil.hpp"

#include <cmath>
#include <cstring>
#include <limits>

static bool nearEqual(float a, float b, float eps = 1e-5f)
{
    return std::fabs(a - b) <= eps;
}

static bool moveToEqual(const tecomm::MoveToPayload& a, const tecomm::MoveToPayload& b)
{
    return nearEqual(a.x, b.x) && nearEqual(a.y, b.y) && nearEqual(a.z, b.z);
}

static bool statusEqual(const tecomm::StatusReportPayload& a, const tecomm::StatusReportPayload& b)
{
    return a.machine_state == b.machine_state && a.motion_state == b.motion_state &&
           a.process_state == b.process_state && nearEqual(a.x, b.x) && nearEqual(a.y, b.y) &&
           nearEqual(a.z, b.z) && nearEqual(a.fx, b.fx) && nearEqual(a.fy, b.fy) &&
           nearEqual(a.fz, b.fz) && a.limits == b.limits && a.e_stop == b.e_stop &&
           a.spreader_extend == b.spreader_extend && a.spreader_extended == b.spreader_extended &&
           a.fixture_extend == b.fixture_extend && a.fixture_extended == b.fixture_extended &&
           a.current_point_index == b.current_point_index && a.program_point_count == b.program_point_count &&
           a.homing_state == b.homing_state && a.blocked_directions == b.blocked_directions &&
           a.flags == b.flags && a.procedure_phase == b.procedure_phase &&
           a.fault_reason == b.fault_reason && a.spreader_state == b.spreader_state;
}

static bool roundtripEmpty(uint8_t message_id)
{
    uint8_t frame[tecomm::kMaxFrameSize];
    const tecomm::EncodeResult encoded = tecomm::encodeEmptyMessage(message_id, frame, sizeof(frame));
    if (!encoded.ok)
    {
        return false;
    }

    tecomm::DecodedFrame decoded{};
    std::size_t consumed = 0;
    if (tecomm::decodeFrame(frame, encoded.bytes_written, decoded, consumed) != tecomm::DecodeResult::Ok)
    {
        return false;
    }
    return decoded.message_id == message_id && decoded.payload_length == 0 && consumed == encoded.bytes_written;
}

static bool roundtripMoveTo(const tecomm::MoveToPayload& original)
{
    uint8_t frame[tecomm::kMaxFrameSize];
    const tecomm::EncodeResult encoded =
        tecomm::encodeMessage(static_cast<uint8_t>(tecomm::MessageId::CmdMoveTo), original, frame, sizeof(frame));
    if (!encoded.ok)
    {
        return false;
    }

    tecomm::DecodedFrame decoded{};
    std::size_t consumed = 0;
    if (tecomm::decodeFrame(frame, encoded.bytes_written, decoded, consumed) != tecomm::DecodeResult::Ok)
    {
        return false;
    }

    tecomm::MoveToPayload parsed{};
    if (!tecomm::decodeMoveToPayload(decoded.payload, decoded.payload_length, parsed))
    {
        return false;
    }
    return moveToEqual(original, parsed);
}

static tecomm::LoadProgramPayload makeLoadProgram(uint8_t count)
{
    tecomm::LoadProgramPayload program{};
    program.point_count = count;
    for (uint8_t i = 0; i < count; ++i)
    {
        program.points[i].x = static_cast<float>(i);
        program.points[i].y = static_cast<float>(i) * 2.0f;
        program.points[i].z = static_cast<float>(i) * -3.0f;
    }
    return program;
}

int main()
{
    // CRC known-answer test vector.
    {
        const char* input = "123456789";
        const uint16_t crc = tecomm::crc16CcittFalse(reinterpret_cast<const uint8_t*>(input), 9);
        if (crc != 0x29B1u)
        {
            return 1;
        }
    }

    // T1: roundtrip each message ID (empty + typed).
    {
        const uint8_t empty_ids[] = {
            static_cast<uint8_t>(tecomm::MessageId::CmdNop),
            static_cast<uint8_t>(tecomm::MessageId::CmdStop),
            static_cast<uint8_t>(tecomm::MessageId::CmdPause),
            static_cast<uint8_t>(tecomm::MessageId::CmdResume),
            static_cast<uint8_t>(tecomm::MessageId::CmdStart),
            static_cast<uint8_t>(tecomm::MessageId::CmdClearFault),
            static_cast<uint8_t>(tecomm::MessageId::CmdRequestStatus),
        };
        for (uint8_t id : empty_ids)
        {
            if (!roundtripEmpty(id))
            {
                return 10;
            }
        }

        tecomm::MoveToPayload move{};
        move.x = 1.0f;
        move.y = -2.0f;
        move.z = 3.5f;
        if (!roundtripMoveTo(move))
        {
            return 11;
        }

        tecomm::BoolPayload spreader{};
        spreader.value = 1;
        uint8_t frame[tecomm::kMaxFrameSize];
        if (!tecomm::encodeMessage(static_cast<uint8_t>(tecomm::MessageId::CmdSetSpreader), spreader, frame,
                                   sizeof(frame))
                 .ok)
        {
            return 12;
        }

        tecomm::StatusReportPayload status{};
        status.machine_state = tecomm::ProtocolMachineState::Active;
        status.motion_state = tecomm::ProtocolMotionState::Moving;
        status.process_state = tecomm::ProtocolProcessState::Running;
        status.x = 1.0f;
        status.y = 2.0f;
        status.z = 3.0f;
        status.fx = 0.1f;
        status.fy = 0.2f;
        status.fz = 0.3f;
        status.limits = (1u << tecomm::kLimitBitXMax);
        status.e_stop = 0;
        status.spreader_extend = 1;
        status.spreader_extended = 1;
        status.fixture_extend = 0;
        status.fixture_extended = 0;
        status.current_point_index = 2;
        status.program_point_count = 5;
        status.procedure_phase = 9;
        status.fault_reason = 6;
        status.spreader_state = 4;
        if (!tecomm::encodeMessage(static_cast<uint8_t>(tecomm::MessageId::MsgStatusReport), status, frame,
                                   sizeof(frame))
                 .ok)
        {
            return 13;
        }

        tecomm::DecodedFrame decoded{};
        std::size_t consumed = 0;
        tecomm::EncodeResult enc =
            tecomm::encodeMessage(static_cast<uint8_t>(tecomm::MessageId::MsgStatusReport), status, frame,
                                  sizeof(frame));
        if (tecomm::decodeFrame(frame, enc.bytes_written, decoded, consumed) != tecomm::DecodeResult::Ok)
        {
            return 14;
        }
        tecomm::StatusReportPayload parsed{};
        if (!tecomm::decodeStatusReportPayload(decoded.payload, decoded.payload_length, parsed) ||
            !statusEqual(status, parsed))
        {
            return 15;
        }
    }

    // Exact frame sizes for key payloads.
    {
        tecomm::MoveToPayload move{1.0f, 2.0f, 3.0f};
        uint8_t frame[tecomm::kMaxFrameSize];
        const tecomm::EncodeResult enc =
            tecomm::encodeMessage(static_cast<uint8_t>(tecomm::MessageId::CmdMoveTo), move, frame, sizeof(frame));
        if (!enc.ok || enc.bytes_written != 6 + tecomm::kMoveToPayloadWireSize + 2)
        {
            return 16;
        }

        tecomm::StatusReportPayload status{};
        status.machine_state = tecomm::ProtocolMachineState::Idle;
        status.motion_state = tecomm::ProtocolMotionState::Idle;
        status.process_state = tecomm::ProtocolProcessState::Idle;
        const tecomm::EncodeResult enc2 =
            tecomm::encodeMessage(static_cast<uint8_t>(tecomm::MessageId::MsgStatusReport), status, frame,
                                  sizeof(frame));
        if (!enc2.ok || enc2.bytes_written != 6 + tecomm::kStatusReportPayloadWireSize + 2)
        {
            return 17;
        }
    }

    // T2: LoadProgram count 0/1/32 and invalid 33.
    {
        const uint8_t counts[] = {0, 1, 32};
        for (uint8_t count : counts)
        {
            const tecomm::LoadProgramPayload program = makeLoadProgram(count);
            uint8_t frame[tecomm::kMaxFrameSize];
            const tecomm::EncodeResult enc = tecomm::encodeMessage(
                static_cast<uint8_t>(tecomm::MessageId::CmdLoadProgram), program, frame, sizeof(frame));
            if (!enc.ok)
            {
                return 20;
            }

            tecomm::DecodedFrame decoded{};
            std::size_t consumed = 0;
            if (tecomm::decodeFrame(frame, enc.bytes_written, decoded, consumed) != tecomm::DecodeResult::Ok)
            {
                return 21;
            }

            tecomm::LoadProgramPayload parsed{};
            if (!tecomm::decodeLoadProgramPayload(decoded.payload, decoded.payload_length, parsed))
            {
                return 22;
            }
            if (parsed.point_count != count)
            {
                return 23;
            }
        }

        // Invalid point_count 33 on wire.
        uint8_t bad_payload[2] = {33, 0};
        uint8_t frame[tecomm::kMaxFrameSize];
        const tecomm::EncodeResult enc = tecomm::encodeFrame(
            static_cast<uint8_t>(tecomm::MessageId::CmdLoadProgram), bad_payload, 1, frame, sizeof(frame));
        if (!enc.ok)
        {
            return 24;
        }
        tecomm::DecodedFrame decoded{};
        std::size_t consumed = 0;
        if (tecomm::decodeFrame(frame, enc.bytes_written, decoded, consumed) != tecomm::DecodeResult::InvalidPayload)
        {
            return 25;
        }

        tecomm::LoadProgramPayload bad_program = makeLoadProgram(33);
        if (tecomm::encodeMessage(static_cast<uint8_t>(tecomm::MessageId::CmdLoadProgram), bad_program, frame,
                                  sizeof(frame))
                .ok)
        {
            return 26;
        }
    }

    // T3: float boundary values.
    {
        tecomm::MoveToPayload move{};
        move.x = 0.0f;
        move.y = -123456.0f;
        move.z = 1e6f;
        if (!roundtripMoveTo(move))
        {
            return 30;
        }
    }

    // NaN/Inf rejected on encode.
    {
        tecomm::MoveToPayload move{};
        move.x = std::nanf("");
        move.y = 0.0f;
        move.z = 0.0f;
        uint8_t payload[tecomm::kMoveToPayloadWireSize];
        if (tecomm::encodeMoveToPayload(move, payload, sizeof(payload)))
        {
            return 31;
        }

        move.x = 0.0f;
        move.y = std::numeric_limits<float>::infinity();
        if (tecomm::encodeMoveToPayload(move, payload, sizeof(payload)))
        {
            return 32;
        }
    }

    // Invalid BoolPayload.
    {
        tecomm::BoolPayload bad{};
        bad.value = 2;
        uint8_t payload[tecomm::kBoolPayloadWireSize];
        if (tecomm::encodeBoolPayload(bad, payload, sizeof(payload)))
        {
            return 33;
        }

        uint8_t wire[1] = {2};
        tecomm::BoolPayload parsed{};
        if (tecomm::decodeBoolPayload(wire, 1, parsed))
        {
            return 34;
        }
    }

    // T4: bad magic.
    {
        uint8_t frame[tecomm::kMaxFrameSize];
        if (!tecomm::encodeEmptyMessage(static_cast<uint8_t>(tecomm::MessageId::CmdNop), frame, sizeof(frame)).ok)
        {
            return 40;
        }
        frame[0] = 0x00;
        tecomm::DecodedFrame decoded{};
        std::size_t consumed = 0;
        if (tecomm::decodeFrame(frame, tecomm::kMinFrameSize, decoded, consumed) !=
            tecomm::DecodeResult::InvalidFrame)
        {
            return 41;
        }
    }

    // T5: unknown message ID.
    {
        uint8_t frame[tecomm::kMaxFrameSize];
        const tecomm::EncodeResult enc = tecomm::encodeFrame(0x05, nullptr, 0, frame, sizeof(frame));
        if (!enc.ok)
        {
            return 42;
        }
        tecomm::DecodedFrame decoded{};
        std::size_t consumed = 0;
        if (tecomm::decodeFrame(frame, enc.bytes_written, decoded, consumed) !=
            tecomm::DecodeResult::UnknownMessageId)
        {
            return 43;
        }
    }

    // T6: payload_length > max.
    {
        uint8_t frame[16];
        frame[0] = tecomm::kMagic0;
        frame[1] = tecomm::kMagic1;
        frame[2] = tecomm::kProtocolVersion;
        frame[3] = static_cast<uint8_t>(tecomm::MessageId::CmdNop);
        frame[4] = 0x01;
        frame[5] = 0x02; // 513
        tecomm::DecodedFrame decoded{};
        std::size_t consumed = 0;
        if (tecomm::decodeFrame(frame, 16, decoded, consumed) != tecomm::DecodeResult::InvalidLength)
        {
            return 44;
        }
    }

    // T7: truncated frame.
    {
        uint8_t frame[tecomm::kMaxFrameSize];
        tecomm::MoveToPayload move{1.0f, 2.0f, 3.0f};
        const tecomm::EncodeResult enc =
            tecomm::encodeMessage(static_cast<uint8_t>(tecomm::MessageId::CmdMoveTo), move, frame, sizeof(frame));
        tecomm::DecodedFrame decoded{};
        std::size_t consumed = 0;
        if (tecomm::decodeFrame(frame, enc.bytes_written - 1, decoded, consumed) !=
            tecomm::DecodeResult::NeedMoreData)
        {
            return 45;
        }
    }

    // T8: CRC error.
    {
        uint8_t frame[tecomm::kMaxFrameSize];
        if (!tecomm::encodeEmptyMessage(static_cast<uint8_t>(tecomm::MessageId::CmdStop), frame, sizeof(frame)).ok)
        {
            return 46;
        }
        frame[7] ^= 0xFF;
        tecomm::DecodedFrame decoded{};
        std::size_t consumed = 0;
        if (tecomm::decodeFrame(frame, tecomm::kMinFrameSize, decoded, consumed) != tecomm::DecodeResult::CrcError)
        {
            return 47;
        }
    }

    // T9: unsupported version.
    {
        uint8_t frame[tecomm::kMaxFrameSize];
        if (!tecomm::encodeEmptyMessage(static_cast<uint8_t>(tecomm::MessageId::CmdPause), frame, sizeof(frame)).ok)
        {
            return 48;
        }
        frame[2] = 2;
        const uint16_t crc = tecomm::crc16CcittFalse(frame, 6);
        frame[6] = static_cast<uint8_t>(crc & 0xFFu);
        frame[7] = static_cast<uint8_t>((crc >> 8) & 0xFFu);
        tecomm::DecodedFrame decoded{};
        std::size_t consumed = 0;
        if (tecomm::decodeFrame(frame, tecomm::kMinFrameSize, decoded, consumed) !=
            tecomm::DecodeResult::UnsupportedVersion)
        {
            return 49;
        }
    }

    // T10: two frames back-to-back.
    {
        uint8_t buffer[tecomm::kMaxFrameSize * 2];
        tecomm::EncodeResult enc1 =
            tecomm::encodeEmptyMessage(static_cast<uint8_t>(tecomm::MessageId::CmdNop), buffer, tecomm::kMaxFrameSize);
        tecomm::EncodeResult enc2 = tecomm::encodeEmptyMessage(
            static_cast<uint8_t>(tecomm::MessageId::CmdStop), buffer + enc1.bytes_written,
            tecomm::kMaxFrameSize);
        if (!enc1.ok || !enc2.ok)
        {
            return 50;
        }

        const std::size_t total = enc1.bytes_written + enc2.bytes_written;
        std::size_t offset = 0;
        tecomm::DecodedFrame decoded{};
        std::size_t consumed = 0;
        if (tecomm::decodeFrame(buffer + offset, total - offset, decoded, consumed) != tecomm::DecodeResult::Ok ||
            decoded.message_id != static_cast<uint8_t>(tecomm::MessageId::CmdNop))
        {
            return 51;
        }
        offset += consumed;
        if (tecomm::decodeFrame(buffer + offset, total - offset, decoded, consumed) != tecomm::DecodeResult::Ok ||
            decoded.message_id != static_cast<uint8_t>(tecomm::MessageId::CmdStop))
        {
            return 52;
        }
    }

    // T11: typed StatusReport decode (covered in T1 partially) — invalid e_stop byte.
    {
        uint8_t payload[tecomm::kStatusReportPayloadWireSize] = {};
        payload[28] = 2;
        tecomm::StatusReportPayload parsed{};
        if (tecomm::decodeStatusReportPayload(payload, sizeof(payload), parsed))
        {
            return 60;
        }
    }

    // Wrong payload length for CMD_MOVE_TO.
    {
        uint8_t frame[tecomm::kMaxFrameSize];
        uint8_t payload[8] = {};
        const tecomm::EncodeResult enc = tecomm::encodeFrame(
            static_cast<uint8_t>(tecomm::MessageId::CmdMoveTo), payload, sizeof(payload), frame, sizeof(frame));
        tecomm::DecodedFrame decoded{};
        std::size_t consumed = 0;
        if (tecomm::decodeFrame(frame, enc.bytes_written, decoded, consumed) != tecomm::DecodeResult::InvalidPayload)
        {
            return 61;
        }
    }

    // T12: output buffer too small.
    {
        uint8_t frame[4];
        const tecomm::EncodeResult enc =
            tecomm::encodeEmptyMessage(static_cast<uint8_t>(tecomm::MessageId::CmdResume), frame, sizeof(frame));
        if (enc.ok || enc.error != tecomm::EncodeError::BufferTooSmall)
        {
            return 62;
        }
    }

    // ProgramSummary roundtrip.
    {
        tecomm::ProgramSummaryPayload summary{};
        summary.point_count = 4;
        summary.current_index = 1;
        summary.x_min = -1.0f;
        summary.x_max = 10.0f;
        summary.y_min = 0.0f;
        summary.y_max = 5.0f;
        summary.z_min = -2.0f;
        summary.z_max = 2.0f;
        uint8_t frame[tecomm::kMaxFrameSize];
        const tecomm::EncodeResult enc = tecomm::encodeMessage(
            static_cast<uint8_t>(tecomm::MessageId::MsgProgramSummary), summary, frame, sizeof(frame));
        if (!enc.ok || enc.bytes_written != 6 + tecomm::kProgramSummaryPayloadWireSize + 2)
        {
            return 70;
        }
        tecomm::DecodedFrame decoded{};
        std::size_t consumed = 0;
        if (tecomm::decodeFrame(frame, enc.bytes_written, decoded, consumed) != tecomm::DecodeResult::Ok)
        {
            return 71;
        }
        tecomm::ProgramSummaryPayload parsed{};
        if (!tecomm::decodeProgramSummaryPayload(decoded.payload, decoded.payload_length, parsed))
        {
            return 72;
        }
    }

    // ACK/NACK roundtrip.
    {
        tecomm::AckPayload ack{};
        ack.ref_message_id = static_cast<uint8_t>(tecomm::MessageId::CmdStart);
        ack.result_code = static_cast<uint8_t>(tecomm::AckResultCode::Ok);
        uint8_t frame[tecomm::kMaxFrameSize];
        const tecomm::EncodeResult enc =
            tecomm::encodeMessage(static_cast<uint8_t>(tecomm::MessageId::RspAck), ack, frame, sizeof(frame));
        if (!enc.ok)
        {
            return 80;
        }
        tecomm::DecodedFrame decoded{};
        std::size_t consumed = 0;
        if (tecomm::decodeFrame(frame, enc.bytes_written, decoded, consumed) != tecomm::DecodeResult::Ok)
        {
            return 81;
        }
        tecomm::AckPayload parsed{};
        if (!tecomm::decodeAckPayload(decoded.payload, decoded.payload_length, parsed))
        {
            return 82;
        }
    }

    // M17.2 MachineConfig / TestConfiguration / PlateOrigin roundtrip.
    {
        tecomm::MachineConfigPayload cfg{};
        cfg.v_approach = 10.0f;
        cfg.v_slow = 2.0f;
        cfg.v_measure = 1.0f;
        cfg.v_homing = 5.0f;
        cfg.acceleration = 0.0f;
        cfg.force_filter = 0.0f;
        cfg.xy_tolerance_N = 0.5f;
        cfg.xy_correction_step = 0.05f;
        cfg.xy_max_correction = 2.0f;
        cfg.insertion_buffer = 1.0f;
        cfg.collision_approach_N = 20.0f;
        cfg.collision_retract_N = 20.0f;
        cfg.collision_clamp_N = 20.0f;
        cfg.collision_unclamp_N = 20.0f;
        cfg.collision_contact_N = 40.0f;
        cfg.collision_measure_N = 40.0f;
        cfg.spreader_extend_time_s = 0.5f;
        cfg.spreader_retract_time_s = 0.5f;
        cfg.force_tolerance_N = 0.5f;
        cfg.z_correction_gain = 0.1f;
        cfg.z_correction_max = 1.0f;
        cfg.minimum_correction = 0.01f;
        cfg.max_optimization_iterations = 10;
        cfg.probe_search_speed = 2.0f;
        cfg.probe_search_distance = 10.0f;
        cfg.probe_backoff = 1.0f;
        cfg.home_offset_x = 0.0f;
        cfg.home_offset_y = 0.0f;
        cfg.home_offset_z = 0.0f;
        cfg.tool_offset_x = 0.25f;
        cfg.tool_offset_y = -0.5f;
        cfg.tool_offset_z = 1.0f;
        cfg.safe_z_buffer = 30.0f;
        cfg.sample_timeout_s = 0.5f;
        cfg.heartbeat_timeout_s = 0.3f;
        cfg.xy_correct_timeout_s = 2.0f;
        cfg.head_diameter = 0.0f;
        cfg.measuring_head_diameter = 8.0f;
        cfg.measuring_head_thickness = 3.0f;
        cfg.tool_diameter = 2.5f;
        cfg.search_depth = 5.0f;
        cfg.approach_distance = 10.0f;
        cfg.fine_search_speed = 1.0f;
        cfg.probe_offset_reference_x = 0.0f;
        cfg.probe_offset_reference_y = 0.0f;
        cfg.probe_offset_reference_z = 5.0f;
        cfg.tool_cylindrical_height = 15.0f;
        cfg.probe_xy_search_margin = 2.0f;
        cfg.insert_search_distance = 10.0f;
        cfg.num_fine_searches = 3;
        cfg.phase1_v_extract = 1.0f;
        cfg.phase1_v_insert = 1.0f;
        cfg.phase2_v_extract = 2.0f;
        cfg.phase2_v_insert = 3.0f;
        cfg.phase3_v_extract = 2.0f;
        cfg.phase3_v_insert = 2.0f;

        uint8_t wire[tecomm::kMachineConfigPayloadWireSize];
        if (!tecomm::encodeMachineConfigPayload(cfg, wire, sizeof(wire)))
        {
            return 90;
        }
        tecomm::MachineConfigPayload parsed{};
        if (!tecomm::decodeMachineConfigPayload(wire, sizeof(wire), parsed))
        {
            return 91;
        }
        if (parsed.tool_offset_z != cfg.tool_offset_z || parsed.safe_z_buffer != cfg.safe_z_buffer ||
            parsed.heartbeat_timeout_s != cfg.heartbeat_timeout_s ||
            parsed.max_optimization_iterations != 10 || parsed.v_homing != cfg.v_homing ||
            parsed.measuring_head_diameter != cfg.measuring_head_diameter ||
            parsed.num_fine_searches != 3 || parsed.probe_offset_reference_z != cfg.probe_offset_reference_z ||
            parsed.tool_cylindrical_height != cfg.tool_cylindrical_height ||
            parsed.probe_xy_search_margin != cfg.probe_xy_search_margin ||
            parsed.insert_search_distance != cfg.insert_search_distance ||
            parsed.phase2_v_insert != cfg.phase2_v_insert)
        {
            return 92;
        }

        uint8_t frame[tecomm::kMaxFrameSize];
        const tecomm::EncodeResult enc = tecomm::encodeMessage(
            static_cast<uint8_t>(tecomm::MessageId::CmdSetMachineConfig), cfg, frame, sizeof(frame));
        if (!enc.ok || enc.bytes_written != 6 + tecomm::kMachineConfigPayloadWireSize + 2)
        {
            return 93;
        }
        tecomm::DecodedFrame decoded{};
        std::size_t consumed = 0;
        if (tecomm::decodeFrame(frame, enc.bytes_written, decoded, consumed) != tecomm::DecodeResult::Ok)
        {
            return 94;
        }

        const tecomm::EncodeResult get_enc = tecomm::encodeEmptyMessage(
            static_cast<uint8_t>(tecomm::MessageId::CmdGetMachineConfig), frame, sizeof(frame));
        if (!get_enc.ok)
        {
            return 95;
        }
        if (tecomm::decodeFrame(frame, get_enc.bytes_written, decoded, consumed) != tecomm::DecodeResult::Ok)
        {
            return 96;
        }

        cfg.v_approach = std::numeric_limits<float>::infinity();
        if (tecomm::encodeMachineConfigPayload(cfg, wire, sizeof(wire)))
        {
            return 97;
        }
    }

    {
        tecomm::TestConfigurationPayload test{};
        test.mode = 1;
        test.target_force_N = 12.5f;
        test.iterations = 7;
        test.active_point_index = 3;
        uint8_t wire[tecomm::kTestConfigurationPayloadWireSize];
        if (!tecomm::encodeTestConfigurationPayload(test, wire, sizeof(wire)))
        {
            return 100;
        }
        tecomm::TestConfigurationPayload parsed{};
        if (!tecomm::decodeTestConfigurationPayload(wire, sizeof(wire), parsed) || parsed.mode != 1 ||
            parsed.iterations != 7 || parsed.active_point_index != 3)
        {
            return 101;
        }
        test.mode = 2;
        if (tecomm::encodeTestConfigurationPayload(test, wire, sizeof(wire)))
        {
            return 102;
        }

        uint8_t frame[tecomm::kMaxFrameSize];
        test.mode = 0;
        const tecomm::EncodeResult enc = tecomm::encodeMessage(
            static_cast<uint8_t>(tecomm::MessageId::CmdSetTestConfiguration), test, frame, sizeof(frame));
        tecomm::DecodedFrame decoded{};
        std::size_t consumed = 0;
        if (!enc.ok ||
            tecomm::decodeFrame(frame, enc.bytes_written, decoded, consumed) != tecomm::DecodeResult::Ok)
        {
            return 103;
        }
    }

    {
        tecomm::MoveToPayload origin{1.5f, 2.5f, 3.5f};
        uint8_t frame[tecomm::kMaxFrameSize];
        const tecomm::EncodeResult enc = tecomm::encodeMessage(
            static_cast<uint8_t>(tecomm::MessageId::CmdSetPlateOrigin), origin, frame, sizeof(frame));
        tecomm::DecodedFrame decoded{};
        std::size_t consumed = 0;
        if (!enc.ok ||
            tecomm::decodeFrame(frame, enc.bytes_written, decoded, consumed) != tecomm::DecodeResult::Ok)
        {
            return 104;
        }
        tecomm::MoveToPayload parsed{};
        if (!tecomm::decodeMoveToPayload(decoded.payload, decoded.payload_length, parsed) ||
            parsed.x != origin.x || parsed.z != origin.z)
        {
            return 105;
        }
    }

    {
        if (!roundtripEmpty(static_cast<uint8_t>(tecomm::MessageId::CmdStartProbeCalib)))
        {
            return 110;
        }

        tecomm::ForcePathChunkPayload chunk{};
        chunk.point_index = 2;
        chunk.iteration = 3;
        chunk.sequence = 9;
        chunk.sample_count = 2;
        chunk.dropped = 4;
        chunk.z[0] = 1.25f;
        chunk.fz[0] = 8.5f;
        chunk.z[1] = 1.50f;
        chunk.fz[1] = 9.0f;

        uint8_t wire[tecomm::kForcePathChunkMaxPayloadWireSize];
        std::size_t bytes_written = 0;
        if (!tecomm::encodeForcePathChunkPayload(chunk, wire, sizeof(wire), bytes_written) ||
            bytes_written != tecomm::forcePathChunkWireSize(2))
        {
            return 111;
        }
        tecomm::ForcePathChunkPayload parsed{};
        if (!tecomm::decodeForcePathChunkPayload(wire, bytes_written, parsed) ||
            parsed.point_index != 2 || parsed.iteration != 3 || parsed.dropped != 4 ||
            parsed.sample_count != 2 || parsed.z[1] != chunk.z[1] || parsed.fz[0] != chunk.fz[0])
        {
            return 112;
        }

        uint8_t frame[tecomm::kMaxFrameSize];
        const tecomm::EncodeResult enc = tecomm::encodeMessage(
            static_cast<uint8_t>(tecomm::MessageId::MsgForcePathChunk), chunk, frame, sizeof(frame));
        tecomm::DecodedFrame decoded{};
        std::size_t consumed = 0;
        if (!enc.ok ||
            tecomm::decodeFrame(frame, enc.bytes_written, decoded, consumed) != tecomm::DecodeResult::Ok)
        {
            return 113;
        }

        chunk.sample_count = static_cast<uint8_t>(tecomm::kForcePathChunkMaxSamples + 1);
        if (tecomm::encodeForcePathChunkPayload(chunk, wire, sizeof(wire), bytes_written))
        {
            return 114;
        }

        tecomm::StatusReportPayload status{};
        status.procedure_phase = 11;
        status.fault_reason = 4;
        status.spreader_state = 4;
        uint8_t status_wire[tecomm::kStatusReportPayloadWireSize];
        if (!tecomm::encodeStatusReportPayload(status, status_wire, sizeof(status_wire)))
        {
            return 115;
        }
        tecomm::StatusReportPayload status_parsed{};
        if (!tecomm::decodeStatusReportPayload(status_wire, sizeof(status_wire), status_parsed) ||
            status_parsed.procedure_phase != 11 || status_parsed.fault_reason != 4 ||
            status_parsed.spreader_state != 4)
        {
            return 116;
        }
        if (tecomm::decodeStatusReportPayload(status_wire, 39, status_parsed))
        {
            return 117;
        }
    }

    return 0;
}
