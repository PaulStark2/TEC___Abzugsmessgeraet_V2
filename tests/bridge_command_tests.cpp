// Unit tests for M13 CommandDispatcher (all M12 commands + ACK/NACK semantics).
#include "CommandDispatcher.hpp"

#include "Decoder.hpp"
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

    return 0;
}
