#include "CommandDispatcher.hpp"
#include "Decoder.hpp"
#include "ForceSample.hpp"
#include "Machine.hpp"
#include "MessageIds.hpp"
#include "PayloadCodec.hpp"
#include "Payloads.hpp"
#include "ProtocolSpec.hpp"
#include "SpreaderTimer.hpp"
#include "TestExecutor.hpp"
#include "TestPoint.hpp"
#include "TestProgram.hpp"
#include "Types.hpp"

#include <vector>

static tecomm::DecodedFrame makeDecoded(uint8_t message_id, const uint8_t* payload, std::size_t payload_length)
{
    tecomm::DecodedFrame frame{};
    frame.protocol_version = tecomm::kProtocolVersion;
    frame.message_id = message_id;
    frame.payload = payload;
    frame.payload_length = payload_length;
    return frame;
}

static bool ackIs(const simbridge::OutboundFrame& frame, tecomm::AckResultCode expected)
{
    tecomm::DecodedFrame decoded{};
    std::size_t consumed = 0;
    if (tecomm::decodeFrame(frame.data, frame.length, decoded, consumed) != tecomm::DecodeResult::Ok)
    {
        return false;
    }
    tecomm::AckPayload ack{};
    if (!tecomm::decodeAckPayload(decoded.payload, decoded.payload_length, ack))
    {
        return false;
    }
    return ack.result_code == static_cast<uint8_t>(expected);
}

static void seedProcess(Machine& machine, TestExecutor& ex)
{
    machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
    TestConfiguration cfg = machine.testConfiguration();
    cfg.target_force_N = 0.1f;
    cfg.mode = TestMode::Aging;
    cfg.iterations = 1u;
    ConfigValidationError err = ConfigValidationError::Ok;
    machine.trySetTestConfiguration(cfg, err);
    homeMachine(machine);
    (void)calibrateProbeCenter(machine);
    TestProgram program;
    program.add(TestPoint{2.0f, 0.0f, 0.0f});
    ex.setProgram(program);
}

int main()
{
    // Timer: STOP during extend freezes the open command without confirming extended.
    {
        SpreaderTimer timer;
        timer.commandExtend(0.4f);
        timer.tick(0.1f);
        timer.enterSafeState();
        if (!timer.command() || timer.extended() || timer.isBusy())
        {
            return 1;
        }
        if (timer.state() != SpreaderState::SafeDeenergized)
        {
            return 2;
        }
        timer.tick(0.5f);
        if (timer.extended() || !timer.command())
        {
            return 3;
        }
    }

    // Timer: STOP during retract does not claim retracted.
    {
        SpreaderTimer timer;
        timer.commandExtend(0.1f);
        timer.tick(0.1f);
        if (!timer.extended())
        {
            return 4;
        }
        timer.commandRetract(0.4f);
        timer.tick(0.1f);
        if (!timer.extended() || timer.command())
        {
            return 5;
        }
        timer.enterSafeState();
        if (timer.command() || !timer.extended() || timer.isBusy())
        {
            return 6;
        }
        if (timer.state() != SpreaderState::SafeDeenergized)
        {
            return 7;
        }
    }

    // Machine STOP during extend.
    {
        Machine m;
        MachineConfig cfg = m.machineConfig();
        cfg.spreader.extend_time_s = 0.4f;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!m.trySetMachineConfig(cfg, err))
        {
            return 10;
        }
        m.setSpreaderExtend(true);
        tickWithForce(m, 0.1f);
        if (!m.hostOutputs().spreaderExtend || m.hostOutputs().spreaderExtended)
        {
            return 11;
        }
        m.stop();
        if (!m.hostOutputs().spreaderExtend || m.spreaderBusy() || m.hostOutputs().spreaderExtended)
        {
            return 12;
        }
        if (m.spreaderState() != SpreaderState::SafeDeenergized)
        {
            return 13;
        }
        tickWithForce(m, 0.5f);
        if (m.hostOutputs().spreaderExtended || !m.hostOutputs().spreaderExtend)
        {
            return 14;
        }
    }

    // Machine STOP during retract: close command already active, latch stays extended.
    {
        Machine m;
        MachineConfig cfg = m.machineConfig();
        cfg.spreader.extend_time_s = 0.1f;
        cfg.spreader.retract_time_s = 0.4f;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!m.trySetMachineConfig(cfg, err))
        {
            return 20;
        }
        m.setSpreaderExtend(true);
        tickWithForce(m, 0.1f);
        if (!m.hostOutputs().spreaderExtended)
        {
            return 21;
        }
        m.setSpreaderExtend(false);
        tickWithForce(m, 0.1f);
        if (m.hostOutputs().spreaderExtend || !m.hostOutputs().spreaderExtended)
        {
            return 22;
        }
        m.stop();
        if (m.hostOutputs().spreaderExtend || !m.hostOutputs().spreaderExtended || m.spreaderBusy())
        {
            return 23;
        }
        if (m.spreaderState() != SpreaderState::SafeDeenergized)
        {
            return 24;
        }
        tickWithForce(m, 0.5f);
        if (!m.hostOutputs().spreaderExtended)
        {
            return 25;
        }
    }

    // E-Stop / Fault during extend.
    {
        Machine m;
        m.setSpreaderExtend(true);
        tickWithForce(m, 0.05f);
        HostInputs in{};
        in.e_stop = true;
        m.setHostInputs(withFreshForce(in));
        m.tick(0.05f);
        if (m.machineState() != MachineState::Fault)
        {
            return 30;
        }
        if (!m.hostOutputs().spreaderExtend || m.hostOutputs().spreaderExtended || m.spreaderBusy())
        {
            return 31;
        }
        if (m.spreaderState() != SpreaderState::SafeDeenergized)
        {
            return 32;
        }
        m.setSpreaderExtend(false);
        if (!m.hostOutputs().spreaderExtend)
        {
            return 33;
        }
    }

    // Procedure STOP during Clamp.
    {
        Machine machine;
        TestExecutor ex(machine);
        seedProcess(machine, ex);
        if (!ex.start())
        {
            return 40;
        }
        for (int i = 0; i < 2500 && ex.procedurePhase() != ProcedurePhase::Clamp; ++i)
        {
            machine.setHostInputs(withPlantInputs(machine));
            ex.tick(0.05f);
        }
        if (ex.procedurePhase() != ProcedurePhase::Clamp)
        {
            return 41;
        }
        if (!machine.hostOutputs().spreaderExtend)
        {
            return 42;
        }
        ex.stop();
        if (ex.processState() != ProcessState::Aborted)
        {
            return 43;
        }
        if (!machine.hostOutputs().spreaderExtend || machine.spreaderBusy())
        {
            return 44;
        }
        if (machine.spreaderState() != SpreaderState::SafeDeenergized)
        {
            return 45;
        }
        if (machine.machineState() != MachineState::Stopped)
        {
            return 46;
        }
    }

    // Procedure E-Stop during Clamp.
    {
        Machine machine;
        TestExecutor ex(machine);
        seedProcess(machine, ex);
        if (!ex.start())
        {
            return 50;
        }
        for (int i = 0; i < 2500 && ex.procedurePhase() != ProcedurePhase::Clamp; ++i)
        {
            machine.setHostInputs(withPlantInputs(machine));
            ex.tick(0.05f);
        }
        if (ex.procedurePhase() != ProcedurePhase::Clamp)
        {
            return 51;
        }
        HostInputs in{};
        in.e_stop = true;
        machine.setHostInputs(withFreshForce(in));
        ex.tick(0.05f);
        if (machine.machineState() != MachineState::Fault || ex.processState() != ProcessState::Aborted)
        {
            return 52;
        }
        if (!machine.hostOutputs().spreaderExtend || machine.spreaderBusy())
        {
            return 53;
        }
        if (machine.spreaderState() != SpreaderState::SafeDeenergized)
        {
            return 54;
        }
    }

    // CmdSetSpreader during Running → InvalidState; valve unchanged.
    {
        Machine machine;
        TestExecutor ex(machine);
        seedProcess(machine, ex);
        simbridge::CommandDispatcher dispatcher(machine, ex);
        dispatcher.setProgramPointCount(1);
        if (!ex.start())
        {
            return 60;
        }
        const bool valve_before = machine.hostOutputs().spreaderExtend;
        tecomm::BoolPayload value{};
        value.value = 1;
        uint8_t payload[tecomm::kBoolPayloadWireSize];
        tecomm::encodeBoolPayload(value, payload, sizeof(payload));
        std::vector<simbridge::OutboundFrame> out;
        dispatcher.dispatch(makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdSetSpreader), payload,
                                        sizeof(payload)),
                            tecomm::DecodeResult::Ok, out);
        if (out.empty() || !ackIs(out[0], tecomm::AckResultCode::InvalidState))
        {
            return 61;
        }
        if (machine.hostOutputs().spreaderExtend != valve_before)
        {
            return 62;
        }
        ex.stop();
        out.clear();
        dispatcher.dispatch(makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdSetSpreader), payload,
                                        sizeof(payload)),
                            tecomm::DecodeResult::Ok, out);
        if (out.empty() || !ackIs(out[0], tecomm::AckResultCode::Ok))
        {
            return 63;
        }
        if (!machine.hostOutputs().spreaderExtend)
        {
            return 64;
        }
    }

    return 0;
}
