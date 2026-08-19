#include "CommandDispatcher.hpp"
#include "Decoder.hpp"
#include "ForceSample.hpp"
#include "Machine.hpp"
#include "MessageIds.hpp"
#include "PayloadCodec.hpp"
#include "Payloads.hpp"
#include "ProtocolSpec.hpp"
#include "TestExecutor.hpp"
#include "TestPoint.hpp"
#include "TestProgram.hpp"
#include "Types.hpp"

#include <vector>

int main()
{
    // Automatic start without homing is rejected.
    {
        Machine machine;
        machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{1.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        if (machine.isHomed())
        {
            return 1;
        }
        if (ex.start())
        {
            return 2;
        }
        if (ex.processState() != ProcessState::Idle)
        {
            return 3;
        }
    }

    // Service CmdMoveTo remains allowed without homing.
    {
        Machine machine;
        TestExecutor ex(machine);
        simbridge::CommandDispatcher dispatcher(machine, ex);
        tecomm::MoveToPayload move{3.0f, 0.0f, 0.0f};
        uint8_t payload[tecomm::kMoveToPayloadWireSize];
        tecomm::encodeMoveToPayload(move, payload, sizeof(payload));
        tecomm::DecodedFrame frame{};
        frame.protocol_version = tecomm::kProtocolVersion;
        frame.message_id = static_cast<uint8_t>(tecomm::MessageId::CmdMoveTo);
        frame.payload = payload;
        frame.payload_length = sizeof(payload);
        std::vector<simbridge::OutboundFrame> out;
        dispatcher.dispatch(frame, tecomm::DecodeResult::Ok, out);
        if (machine.machineState() != MachineState::Active && machine.machineState() != MachineState::Idle)
        {
            return 10;
        }
        if (machine.isHomed())
        {
            return 11;
        }
    }

    // CmdStart without homing → InvalidState.
    {
        Machine machine;
        machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
        TestExecutor ex(machine);
        simbridge::CommandDispatcher dispatcher(machine, ex);
        TestProgram program;
        program.add(TestPoint{1.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        dispatcher.setProgramPointCount(1);
        tecomm::DecodedFrame frame{};
        frame.protocol_version = tecomm::kProtocolVersion;
        frame.message_id = static_cast<uint8_t>(tecomm::MessageId::CmdStart);
        std::vector<simbridge::OutboundFrame> out;
        dispatcher.dispatch(frame, tecomm::DecodeResult::Ok, out);
        tecomm::DecodedFrame decoded{};
        std::size_t consumed = 0;
        if (out.empty() ||
            tecomm::decodeFrame(out[0].data, out[0].length, decoded, consumed) != tecomm::DecodeResult::Ok)
        {
            return 20;
        }
        tecomm::AckPayload ack{};
        if (!tecomm::decodeAckPayload(decoded.payload, decoded.payload_length, ack))
        {
            return 21;
        }
        if (ack.result_code != static_cast<uint8_t>(tecomm::AckResultCode::InvalidState))
        {
            return 22;
        }
    }

    // After homing, automatic start is allowed.
    {
        Machine machine;
        machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
        if (!homeMachine(machine))
        {
            return 30;
        }
        (void)calibrateProbeCenter(machine);
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{2.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        if (!ex.start())
        {
            return 31;
        }
        if (ex.processState() != ProcessState::Running)
        {
            return 32;
        }
        ex.stop();
    }

    // Starting a new homing clears isHomed; automatic start is forbidden again.
    {
        Machine machine;
        machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
        if (!homeMachine(machine))
        {
            return 40;
        }
        if (!machine.home())
        {
            return 41;
        }
        if (machine.isHomed())
        {
            return 42;
        }
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{1.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        if (ex.start())
        {
            return 43;
        }
    }

    // Homing itself is allowed without a prior home.
    {
        Machine machine;
        if (machine.isHomed())
        {
            return 50;
        }
        if (!machine.home())
        {
            return 51;
        }
        if (machine.machineState() != MachineState::Homing)
        {
            return 52;
        }
    }

    return 0;
}
