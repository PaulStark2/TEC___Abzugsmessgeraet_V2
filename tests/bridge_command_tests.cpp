// Unit tests for M13 CommandDispatcher (all M12 commands + ACK/NACK semantics).
#include "CommandDispatcher.hpp"

#include "ConfigMapping.hpp"
#include "Decoder.hpp"
#include "ForceSample.hpp"
#include "MessageIds.hpp"
#include "PayloadCodec.hpp"
#include "Payloads.hpp"
#include "ProtocolSpec.hpp"

#include "Machine.hpp"
#include "TestExecutor.hpp"
#include "Types.hpp"

#include <cstring>
#include <vector>

static bool decodeOutboundAck(const simbridge::OutboundFrame& frame, tecomm::AckPayload& out)
{
    tecomm::DecodedFrame decoded{};
    std::size_t consumed = 0;
    if (tecomm::decodeFrame(frame.data, frame.length, decoded, consumed) != tecomm::DecodeResult::Ok)
    {
        return false;
    }
    if (decoded.message_id != static_cast<uint8_t>(tecomm::MessageId::RspAck))
    {
        return false;
    }
    return tecomm::decodeAckPayload(decoded.payload, decoded.payload_length, out);
}

static bool decodeOutboundNack(const simbridge::OutboundFrame& frame, tecomm::NackPayload& out)
{
    tecomm::DecodedFrame decoded{};
    std::size_t consumed = 0;
    if (tecomm::decodeFrame(frame.data, frame.length, decoded, consumed) != tecomm::DecodeResult::Ok)
    {
        return false;
    }
    if (decoded.message_id != static_cast<uint8_t>(tecomm::MessageId::RspNack))
    {
        return false;
    }
    return tecomm::decodeNackPayload(decoded.payload, decoded.payload_length, out);
}

static tecomm::DecodedFrame makeDecoded(uint8_t message_id, const uint8_t* payload, std::size_t payload_length)
{
    tecomm::DecodedFrame frame{};
    frame.protocol_version = tecomm::kProtocolVersion;
    frame.message_id = message_id;
    frame.payload = payload;
    frame.payload_length = payload_length;
    return frame;
}

static bool dispatchEmpty(simbridge::CommandDispatcher& dispatcher, tecomm::MessageId id,
                          tecomm::AckResultCode expected, std::vector<simbridge::OutboundFrame>& out)
{
    const tecomm::DecodedFrame frame = makeDecoded(static_cast<uint8_t>(id), nullptr, 0);
    dispatcher.dispatch(frame, tecomm::DecodeResult::Ok, out);
    if (out.size() != 1)
    {
        return false;
    }
    tecomm::AckPayload ack{};
    if (!decodeOutboundAck(out[0], ack))
    {
        return false;
    }
    return ack.ref_message_id == static_cast<uint8_t>(id) &&
           ack.result_code == static_cast<uint8_t>(expected);
}

static bool hasMessageId(const std::vector<simbridge::OutboundFrame>& out, tecomm::MessageId id)
{
    for (const simbridge::OutboundFrame& frame : out)
    {
        tecomm::DecodedFrame decoded{};
        std::size_t consumed = 0;
        if (tecomm::decodeFrame(frame.data, frame.length, decoded, consumed) == tecomm::DecodeResult::Ok &&
            decoded.message_id == static_cast<uint8_t>(id))
        {
            return true;
        }
    }
    return false;
}

int main()
{
    Machine machine;
    machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
    if (!homeMachine(machine))
    {
        return 90;
    }
    (void)calibrateProbeCenter(machine);
    TestExecutor executor(machine);
    simbridge::CommandDispatcher dispatcher(machine, executor);

    // CMD_NOP -> ACK Ok
    {
        std::vector<simbridge::OutboundFrame> out;
        if (!dispatchEmpty(dispatcher, tecomm::MessageId::CmdNop, tecomm::AckResultCode::Ok, out))
        {
            return 1;
        }
    }

    // CMD_MOVE_TO -> ACK Ok and motion starts
    {
        tecomm::MoveToPayload move{5.0f, 0.0f, 0.0f};
        uint8_t payload[tecomm::kMoveToPayloadWireSize];
        tecomm::encodeMoveToPayload(move, payload, sizeof(payload));

        std::vector<simbridge::OutboundFrame> out;
        const tecomm::DecodedFrame frame =
            makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdMoveTo), payload, sizeof(payload));
        dispatcher.dispatch(frame, tecomm::DecodeResult::Ok, out);

        tecomm::AckPayload ack{};
        if (!decodeOutboundAck(out[0], ack) || ack.result_code != static_cast<uint8_t>(tecomm::AckResultCode::Ok))
        {
            return 2;
        }
        if (machine.machineState() != MachineState::Active)
        {
            return 3;
        }
    }

    // CMD_PAUSE / CMD_RESUME
    {
        std::vector<simbridge::OutboundFrame> out;
        if (!dispatchEmpty(dispatcher, tecomm::MessageId::CmdPause, tecomm::AckResultCode::Ok, out))
        {
            return 4;
        }
        if (machine.machineState() != MachineState::Paused)
        {
            return 5;
        }

        out.clear();
        if (!dispatchEmpty(dispatcher, tecomm::MessageId::CmdResume, tecomm::AckResultCode::Ok, out))
        {
            return 6;
        }
        if (machine.machineState() != MachineState::Active)
        {
            return 7;
        }
    }

    // CMD_STOP outside Running -> Machine Stopped
    {
        std::vector<simbridge::OutboundFrame> out;
        if (!dispatchEmpty(dispatcher, tecomm::MessageId::CmdStop, tecomm::AckResultCode::Ok, out))
        {
            return 8;
        }
        if (machine.machineState() != MachineState::Stopped)
        {
            return 9;
        }
    }

    // CMD_STOP again -> Rejected
    {
        std::vector<simbridge::OutboundFrame> out;
        if (!dispatchEmpty(dispatcher, tecomm::MessageId::CmdStop, tecomm::AckResultCode::Rejected, out))
        {
            return 10;
        }
    }

    // LOAD_PROGRAM + START + InvalidState MOVE_TO while Running
    {
        tecomm::LoadProgramPayload load{};
        load.point_count = 2;
        load.points[0] = tecomm::Point3f{1.0f, 0.0f, 0.0f};
        load.points[1] = tecomm::Point3f{2.0f, 0.0f, 0.0f};

        uint8_t payload[tecomm::kLoadProgramMaxPayloadWireSize];
        std::size_t bytes_written = 0;
        tecomm::encodeLoadProgramPayload(load, payload, sizeof(payload), bytes_written);

        std::vector<simbridge::OutboundFrame> out;
        tecomm::DecodedFrame frame = makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdLoadProgram),
                                                 payload, bytes_written);
        dispatcher.dispatch(frame, tecomm::DecodeResult::Ok, out);

        tecomm::AckPayload ack{};
        if (!decodeOutboundAck(out[0], ack) || ack.result_code != static_cast<uint8_t>(tecomm::AckResultCode::Ok))
        {
            return 20;
        }
        if (!hasMessageId(out, tecomm::MessageId::MsgProgramSummary))
        {
            return 21;
        }

        // MOVE_TO / STOP above clear isHomed — re-home before CmdStart.
        if (!homeMachine(machine))
        {
            return 215;
        }

        out.clear();
        if (!dispatchEmpty(dispatcher, tecomm::MessageId::CmdStart, tecomm::AckResultCode::Ok, out))
        {
            return 22;
        }
        if (executor.processState() != ProcessState::Running)
        {
            return 23;
        }

        tecomm::MoveToPayload move{99.0f, 0.0f, 0.0f};
        uint8_t move_payload[tecomm::kMoveToPayloadWireSize];
        tecomm::encodeMoveToPayload(move, move_payload, sizeof(move_payload));
        frame = makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdMoveTo), move_payload,
                            sizeof(move_payload));
        out.clear();
        dispatcher.dispatch(frame, tecomm::DecodeResult::Ok, out);
        if (!decodeOutboundAck(out[0], ack) ||
            ack.result_code != static_cast<uint8_t>(tecomm::AckResultCode::InvalidState))
        {
            return 24;
        }

        out.clear();
        if (!dispatchEmpty(dispatcher, tecomm::MessageId::CmdStop, tecomm::AckResultCode::Ok, out))
        {
            return 25;
        }
        if (executor.processState() != ProcessState::Aborted)
        {
            return 26;
        }
    }

    // START empty program -> ProgramEmpty
    {
        Machine m2;
        TestExecutor ex2(m2);
        simbridge::CommandDispatcher d2(m2, ex2);

        tecomm::LoadProgramPayload empty{};
        empty.point_count = 0;
        uint8_t payload[1];
        std::size_t bytes_written = 0;
        tecomm::encodeLoadProgramPayload(empty, payload, sizeof(payload), bytes_written);

        std::vector<simbridge::OutboundFrame> out;
        tecomm::DecodedFrame frame = makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdLoadProgram),
                                                 payload, bytes_written);
        d2.dispatch(frame, tecomm::DecodeResult::Ok, out);

        out.clear();
        if (!dispatchEmpty(d2, tecomm::MessageId::CmdStart, tecomm::AckResultCode::ProgramEmpty, out))
        {
            return 30;
        }
    }

    // CmdStart with program but probe_center_valid == false -> InvalidState (not ProgramEmpty)
    {
        Machine m;
        m.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
        if (!homeMachine(m))
        {
            return 270;
        }
        if (m.calibrationSet().probe_center_valid)
        {
            return 271;
        }
        TestExecutor ex(m);
        simbridge::CommandDispatcher d(m, ex);

        tecomm::LoadProgramPayload load{};
        load.point_count = 1;
        load.points[0] = tecomm::Point3f{1.0f, 0.0f, 0.0f};

        uint8_t payload[tecomm::kLoadProgramMaxPayloadWireSize];
        std::size_t bytes_written = 0;
        tecomm::encodeLoadProgramPayload(load, payload, sizeof(payload), bytes_written);

        std::vector<simbridge::OutboundFrame> out;
        tecomm::DecodedFrame frame = makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdLoadProgram),
                                                 payload, bytes_written);
        d.dispatch(frame, tecomm::DecodeResult::Ok, out);
        if (out.empty())
        {
            return 272;
        }
        tecomm::AckPayload ack{};
        if (!decodeOutboundAck(out[0], ack) || ack.result_code != static_cast<uint8_t>(tecomm::AckResultCode::Ok))
        {
            return 273;
        }

        out.clear();
        if (!dispatchEmpty(d, tecomm::MessageId::CmdStart, tecomm::AckResultCode::InvalidState, out))
        {
            return 274;
        }
    }

    // CmdStart with valid probe_center -> Ok
    {
        Machine m;
        m.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
        if (!homeMachine(m))
        {
            return 280;
        }
        (void)calibrateProbeCenter(m);
        if (!m.calibrationSet().probe_center_valid)
        {
            return 281;
        }

        TestExecutor ex(m);
        simbridge::CommandDispatcher d(m, ex);

        tecomm::LoadProgramPayload load{};
        load.point_count = 1;
        load.points[0] = tecomm::Point3f{1.0f, 0.0f, 0.0f};

        uint8_t payload[tecomm::kLoadProgramMaxPayloadWireSize];
        std::size_t bytes_written = 0;
        tecomm::encodeLoadProgramPayload(load, payload, sizeof(payload), bytes_written);

        std::vector<simbridge::OutboundFrame> out;
        tecomm::DecodedFrame frame = makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdLoadProgram),
                                                 payload, bytes_written);
        d.dispatch(frame, tecomm::DecodeResult::Ok, out);
        if (out.empty())
        {
            return 282;
        }
        tecomm::AckPayload ack{};
        if (!decodeOutboundAck(out[0], ack) || ack.result_code != static_cast<uint8_t>(tecomm::AckResultCode::Ok))
        {
            return 283;
        }

        out.clear();
        if (!dispatchEmpty(d, tecomm::MessageId::CmdStart, tecomm::AckResultCode::Ok, out))
        {
            return 284;
        }
    }

    // CmdStart with plate_origin.z high still succeeds: Safe-Z is program-relative.
    {
        Machine m;
        m.setPlateOrigin(TestPoint{0.0f, 0.0f, 17.0f});
        if (!homeMachine(m))
        {
            return 285;
        }
        (void)calibrateProbeCenter(m);
        if (!m.calibrationSet().probe_center_valid)
        {
            return 286;
        }

        TestExecutor ex(m);
        simbridge::CommandDispatcher d(m, ex);

        tecomm::LoadProgramPayload load{};
        load.point_count = 1;
        load.points[0] = tecomm::Point3f{1.0f, 0.0f, 0.0f};

        uint8_t payload[tecomm::kLoadProgramMaxPayloadWireSize];
        std::size_t bytes_written = 0;
        tecomm::encodeLoadProgramPayload(load, payload, sizeof(payload), bytes_written);

        std::vector<simbridge::OutboundFrame> out;
        tecomm::DecodedFrame frame = makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdLoadProgram),
                                                 payload, bytes_written);
        d.dispatch(frame, tecomm::DecodeResult::Ok, out);
        if (out.empty())
        {
            return 288;
        }
        tecomm::AckPayload ack{};
        if (!decodeOutboundAck(out[0], ack) || ack.result_code != static_cast<uint8_t>(tecomm::AckResultCode::Ok))
        {
            return 289;
        }

        out.clear();
        if (!dispatchEmpty(d, tecomm::MessageId::CmdStart, tecomm::AckResultCode::Ok, out))
        {
            return 2891;
        }
    }

    // CmdSetProbeCenter rehydrates Core: probe_center_valid must be true afterwards.
    {
        Machine m;
        m.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
        if (!homeMachine(m))
        {
            return 290;
        }
        if (m.calibrationSet().probe_center_valid)
        {
            return 291;
        }

        TestExecutor ex(m);
        simbridge::CommandDispatcher d(m, ex);

        const TestPoint stored_center{73.0f, 31.0f, 14.0f};
        tecomm::MoveToPayload pose{stored_center.x, stored_center.y, stored_center.z};
        uint8_t pose_payload[tecomm::kMoveToPayloadWireSize];
        tecomm::encodeMoveToPayload(pose, pose_payload, sizeof(pose_payload));

        std::vector<simbridge::OutboundFrame> out;
        tecomm::DecodedFrame frame = makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdSetProbeCenter),
                                                 pose_payload, sizeof(pose_payload));
        d.dispatch(frame, tecomm::DecodeResult::Ok, out);

        if (out.empty())
        {
            return 292;
        }
        tecomm::AckPayload ack{};
        if (!decodeOutboundAck(out[0], ack) || ack.result_code != static_cast<uint8_t>(tecomm::AckResultCode::Ok))
        {
            return 293;
        }

        if (!m.calibrationSet().probe_center_valid)
        {
            return 294;
        }
        if (m.calibrationSet().probe_center.x != stored_center.x ||
            m.calibrationSet().probe_center.y != stored_center.y ||
            m.calibrationSet().probe_center.z != stored_center.z)
        {
            return 295;
        }
    }

    // Reconnect auto-start: persisted probe_center via CmdSetProbeCenter (no M19), then CmdStart.
    {
        Machine m;
        m.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
        if (!homeMachine(m))
        {
            return 296;
        }
        if (m.calibrationSet().probe_center_valid)
        {
            return 297;
        }

        TestExecutor ex(m);
        simbridge::CommandDispatcher d(m, ex);

        const TestPoint stored_center{73.0f, 31.0f, 14.0f};
        tecomm::MoveToPayload pose{stored_center.x, stored_center.y, stored_center.z};
        uint8_t pose_payload[tecomm::kMoveToPayloadWireSize];
        tecomm::encodeMoveToPayload(pose, pose_payload, sizeof(pose_payload));

        std::vector<simbridge::OutboundFrame> out;
        tecomm::DecodedFrame frame = makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdSetProbeCenter),
                                                 pose_payload, sizeof(pose_payload));
        d.dispatch(frame, tecomm::DecodeResult::Ok, out);
        tecomm::AckPayload ack{};
        if (out.empty() || !decodeOutboundAck(out[0], ack) ||
            ack.result_code != static_cast<uint8_t>(tecomm::AckResultCode::Ok))
        {
            return 298;
        }
        if (!m.calibrationSet().probe_center_valid)
        {
            return 299;
        }

        // ProcedureController raises to dynamic Safe-Z during approach legs.
        tecomm::LoadProgramPayload load{};
        load.point_count = 1;
        load.points[0] = tecomm::Point3f{1.0f, 0.0f, 0.0f};
        uint8_t payload[tecomm::kLoadProgramMaxPayloadWireSize];
        std::size_t bytes_written = 0;
        tecomm::encodeLoadProgramPayload(load, payload, sizeof(payload), bytes_written);
        out.clear();
        frame = makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdLoadProgram), payload, bytes_written);
        d.dispatch(frame, tecomm::DecodeResult::Ok, out);

        out.clear();
        if (!dispatchEmpty(d, tecomm::MessageId::CmdStart, tecomm::AckResultCode::Ok, out))
        {
            return 300;
        }
        if (ex.processState() != ProcessState::Running)
        {
            return 301;
        }
        if (!m.calibrationSet().probe_center_valid)
        {
            return 302;
        }
        if (m.calibrationSet().probe_center.x != stored_center.x ||
            m.calibrationSet().probe_center.y != stored_center.y ||
            m.calibrationSet().probe_center.z != stored_center.z)
        {
            return 303;
        }
    }

    // SET_SPREADER / SET_FIXTURE
    {
        tecomm::BoolPayload value{};
        value.value = 1;
        uint8_t payload[tecomm::kBoolPayloadWireSize];
        tecomm::encodeBoolPayload(value, payload, sizeof(payload));

        std::vector<simbridge::OutboundFrame> out;
        tecomm::DecodedFrame frame = makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdSetSpreader),
                                                 payload, sizeof(payload));
        dispatcher.dispatch(frame, tecomm::DecodeResult::Ok, out);
        tecomm::AckPayload ack{};
        if (!decodeOutboundAck(out[0], ack) || ack.result_code != static_cast<uint8_t>(tecomm::AckResultCode::Ok))
        {
            return 40;
        }
        if (!machine.hostOutputs().spreaderExtend)
        {
            return 41;
        }

        frame = makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdSetFixture), payload, sizeof(payload));
        out.clear();
        dispatcher.dispatch(frame, tecomm::DecodeResult::Ok, out);
        if (!decodeOutboundAck(out[0], ack) || ack.result_code != static_cast<uint8_t>(tecomm::AckResultCode::Ok))
        {
            return 42;
        }
    }

    // REQUEST_STATUS -> ACK + StatusReport
    {
        std::vector<simbridge::OutboundFrame> out;
        const tecomm::DecodedFrame frame =
            makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdRequestStatus), nullptr, 0);
        dispatcher.dispatch(frame, tecomm::DecodeResult::Ok, out);

        tecomm::AckPayload ack{};
        if (out.size() < 2 || !decodeOutboundAck(out[0], ack) ||
            ack.result_code != static_cast<uint8_t>(tecomm::AckResultCode::Ok))
        {
            return 50;
        }
        if (!hasMessageId(out, tecomm::MessageId::MsgStatusReport))
        {
            return 51;
        }
    }

    // Unknown message ID -> NACK Unsupported
    {
        std::vector<simbridge::OutboundFrame> out;
        tecomm::DecodedFrame frame = makeDecoded(0xEE, nullptr, 0);
        dispatcher.dispatch(frame, tecomm::DecodeResult::UnknownMessageId, out);
        tecomm::NackPayload nack{};
        if (!decodeOutboundNack(out[0], nack) ||
            nack.error_code != static_cast<uint8_t>(tecomm::NackErrorCode::Unsupported))
        {
            return 60;
        }
    }

    // Invalid payload -> NACK InvalidPayload
    {
        std::vector<simbridge::OutboundFrame> out;
        tecomm::DecodedFrame frame = makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdMoveTo), nullptr, 0);
        dispatcher.dispatch(frame, tecomm::DecodeResult::InvalidPayload, out);
        tecomm::NackPayload nack{};
        if (!decodeOutboundNack(out[0], nack) ||
            nack.error_code != static_cast<uint8_t>(tecomm::NackErrorCode::InvalidPayload))
        {
            return 61;
        }
    }
    {
        Machine m3;
        TestExecutor ex3(m3);
        simbridge::CommandDispatcher d3(m3, ex3);

        HostInputs inputs{};
        inputs.e_stop = true;
        m3.setHostInputs(inputs);
        m3.tick(0.05f);
        if (m3.machineState() != MachineState::Fault)
        {
            return 70;
        }

        tecomm::MoveToPayload move{1.0f, 0.0f, 0.0f};
        uint8_t payload[tecomm::kMoveToPayloadWireSize];
        tecomm::encodeMoveToPayload(move, payload, sizeof(payload));
        std::vector<simbridge::OutboundFrame> out;
        tecomm::DecodedFrame frame =
            makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdMoveTo), payload, sizeof(payload));
        d3.dispatch(frame, tecomm::DecodeResult::Ok, out);
        tecomm::AckPayload ack{};
        if (!decodeOutboundAck(out[0], ack) ||
            ack.result_code != static_cast<uint8_t>(tecomm::AckResultCode::FaultLatched))
        {
            return 71;
        }

        out.clear();
        if (!dispatchEmpty(d3, tecomm::MessageId::CmdClearFault, tecomm::AckResultCode::Rejected, out))
        {
            return 72;
        }

        inputs.e_stop = false;
        m3.setHostInputs(inputs);
        m3.tick(0.05f);
        if (m3.machineState() != MachineState::Fault)
        {
            return 725;
        }

        out.clear();
        const tecomm::DecodedFrame clear_frame =
            makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdClearFault), nullptr, 0);
        d3.dispatch(clear_frame, tecomm::DecodeResult::Ok, out);
        if (out.size() != 1 || !decodeOutboundAck(out[0], ack) ||
            ack.result_code != static_cast<uint8_t>(tecomm::AckResultCode::Ok))
        {
            return 73;
        }
        if (m3.machineState() != MachineState::Idle)
        {
            return 74;
        }
    }

    // PAUSE/RESUME while Running via executor
    {
        Machine m4;
        m4.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
        if (!homeMachine(m4))
        {
            return 79;
        }
        (void)calibrateProbeCenter(m4);
        TestExecutor ex4(m4);
        simbridge::CommandDispatcher d4(m4, ex4);

        tecomm::LoadProgramPayload load{};
        load.point_count = 1;
        load.points[0] = tecomm::Point3f{50.0f, 0.0f, 0.0f};
        uint8_t payload[tecomm::kLoadProgramMaxPayloadWireSize];
        std::size_t bytes_written = 0;
        tecomm::encodeLoadProgramPayload(load, payload, sizeof(payload), bytes_written);

        std::vector<simbridge::OutboundFrame> out;
        tecomm::DecodedFrame frame = makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdLoadProgram),
                                                 payload, bytes_written);
        d4.dispatch(frame, tecomm::DecodeResult::Ok, out);
        out.clear();
        dispatchEmpty(d4, tecomm::MessageId::CmdStart, tecomm::AckResultCode::Ok, out);

        HostInputs inputs{};
        m4.setHostInputs(inputs);
        m4.tick(0.05f);

        out.clear();
        if (!dispatchEmpty(d4, tecomm::MessageId::CmdPause, tecomm::AckResultCode::Ok, out))
        {
            return 80;
        }
        if (m4.machineState() != MachineState::Paused)
        {
            return 81;
        }

        out.clear();
        if (!dispatchEmpty(d4, tecomm::MessageId::CmdResume, tecomm::AckResultCode::Ok, out))
        {
            return 82;
        }
    }

    // M17.2 set/get machine config and plate origin.
    {
        Machine m;
        TestExecutor ex(m);
        simbridge::CommandDispatcher d(m, ex);

        tecomm::MachineConfigPayload cfg = toMachineConfigPayload(MachineConfig{});
        uint8_t payload[tecomm::kMachineConfigPayloadWireSize];
        if (!tecomm::encodeMachineConfigPayload(cfg, payload, sizeof(payload)))
        {
            return 90;
        }
        std::vector<simbridge::OutboundFrame> out;
        tecomm::DecodedFrame frame = makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdSetMachineConfig),
                                                 payload, sizeof(payload));
        d.dispatch(frame, tecomm::DecodeResult::Ok, out);
        tecomm::AckPayload ack{};
        if (out.size() != 1 || !decodeOutboundAck(out[0], ack) ||
            ack.result_code != static_cast<uint8_t>(tecomm::AckResultCode::Ok))
        {
            return 91;
        }

        cfg.collision_contact_N = 80.0f;
        if (!tecomm::encodeMachineConfigPayload(cfg, payload, sizeof(payload)))
        {
            return 92;
        }
        out.clear();
        frame = makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdSetMachineConfig), payload, sizeof(payload));
        d.dispatch(frame, tecomm::DecodeResult::Ok, out);
        tecomm::NackPayload nack{};
        if (out.size() != 1 || !decodeOutboundNack(out[0], nack))
        {
            return 93;
        }
        if (m.machineConfig().collision.contact_N > 50.0f)
        {
            return 94;
        }

        tecomm::MoveToPayload origin{9.0f, 8.0f, 7.0f};
        uint8_t origin_wire[tecomm::kMoveToPayloadWireSize];
        tecomm::encodeMoveToPayload(origin, origin_wire, sizeof(origin_wire));
        out.clear();
        frame = makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdSetPlateOrigin), origin_wire,
                            sizeof(origin_wire));
        d.dispatch(frame, tecomm::DecodeResult::Ok, out);
        if (!m.calibrationSet().plate_origin_valid || m.calibrationSet().plate_origin.x != 9.0f)
        {
            return 95;
        }
    }

    // CmdMoveTo installs a_manual even after a slow automatic profile.
    {
        Machine m;
        TestExecutor ex(m);
        simbridge::CommandDispatcher d(m, ex);
        MachineConfig cfg = m.machineConfig();
        cfg.motion.a_testpoints = 1.0f;
        cfg.motion.a_manual = 500.0f;
        ConfigValidationError err{};
        if (!m.trySetMachineConfig(cfg, err))
        {
            return 96;
        }
        m.setActiveMotionProfile(40.0f, cfg.motion.a_testpoints, 0.0f);

        tecomm::MoveToPayload move{80.0f, 0.0f, 0.0f};
        uint8_t payload[tecomm::kMoveToPayloadWireSize];
        tecomm::encodeMoveToPayload(move, payload, sizeof(payload));
        std::vector<simbridge::OutboundFrame> out;
        tecomm::DecodedFrame frame =
            makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdMoveTo), payload, sizeof(payload));
        d.dispatch(frame, tecomm::DecodeResult::Ok, out);
        tickWithForce(m, 0.05f);
        // a=500 mm/s² from rest over 50 ms → 0.625 mm. a=1 mm/s² → 0.00125 mm.
        if (!(m.x() > 0.4f) || m.x() > 1.0f)
        {
            return 97;
        }

        // A following procedure profile is not stuck on a_manual.
        m.stop();
        m.setActiveMotionProfile(40.0f, cfg.motion.a_testpoints, 0.0f);
        const float x_before = m.x();
        m.moveTo(x_before + 80.0f, 0.0f, 0.0f);
        tickWithForce(m, 0.05f);
        if (m.x() - x_before > 0.05f)
        {
            return 98; // automatic moveTo must keep a_testpoints
        }
    }

    // CmdForceTare only while axes are still and no procedure is running.
    {
        Machine m;
        TestExecutor ex(m);
        simbridge::CommandDispatcher d(m, ex);
        std::vector<simbridge::OutboundFrame> out;
        if (!dispatchEmpty(d, tecomm::MessageId::CmdForceTare, tecomm::AckResultCode::Ok, out))
        {
            return 100;
        }
        if (!m.forceTareActive())
        {
            return 101;
        }
        out.clear();
        if (!dispatchEmpty(d, tecomm::MessageId::CmdForceTare, tecomm::AckResultCode::InvalidState, out))
        {
            return 102;
        }
        m.stop();
        m.moveTo(10.0f, 0.0f, 0.0f);
        out.clear();
        if (m.motionState() != MotionState::Moving)
        {
            return 103;
        }
        if (!dispatchEmpty(d, tecomm::MessageId::CmdForceTare, tecomm::AckResultCode::InvalidState, out))
        {
            return 104;
        }
    }

    return 0;
}
