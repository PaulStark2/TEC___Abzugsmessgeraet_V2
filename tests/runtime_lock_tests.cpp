#include "CommandDispatcher.hpp"
#include "ConfigMapping.hpp"
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

static tecomm::DecodedFrame makeDecoded(uint8_t message_id, const uint8_t* payload, std::size_t payload_length)
{
    tecomm::DecodedFrame frame{};
    frame.protocol_version = tecomm::kProtocolVersion;
    frame.message_id = message_id;
    frame.payload = payload;
    frame.payload_length = payload_length;
    return frame;
}

static bool ackCode(const simbridge::OutboundFrame& frame, tecomm::AckResultCode expected)
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

static void seed(Machine& machine, TestExecutor& ex)
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
    // Core lock: MachineConfig / TestConfiguration / PlateOrigin rejected while Running.
    {
        Machine machine;
        TestExecutor ex(machine);
        seed(machine, ex);
        if (!ex.start())
        {
            return 1;
        }
        if (!machine.runtimeConfigLocked())
        {
            return 2;
        }

        MachineConfig cfg = machine.machineConfig();
        const float original_safe_z = cfg.motion.safe_z;
        cfg.motion.safe_z = 20.0f;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (machine.trySetMachineConfig(cfg, err) || err != ConfigValidationError::RuntimeLocked)
        {
            return 3;
        }
        if (machine.machineConfig().motion.safe_z != original_safe_z)
        {
            return 4;
        }

        TestConfiguration tcfg = machine.testConfiguration();
        tcfg.target_force_N = 12.0f;
        if (machine.trySetTestConfiguration(tcfg, err) || err != ConfigValidationError::RuntimeLocked)
        {
            return 5;
        }
        if (machine.testConfiguration().target_force_N == 12.0f)
        {
            return 6;
        }

        machine.setPlateOrigin(TestPoint{4.0f, 5.0f, 6.0f});
        if (machine.calibrationSet().plate_origin.x == 4.0f)
        {
            return 7;
        }

        ex.stop();
        cfg.motion.safe_z = 20.0f;
        if (!machine.trySetMachineConfig(cfg, err))
        {
            return 8;
        }
        if (machine.machineConfig().motion.safe_z != 20.0f)
        {
            return 9;
        }
    }

    // Host commands NACK InvalidState while Running.
    {
        Machine machine;
        TestExecutor ex(machine);
        seed(machine, ex);
        simbridge::CommandDispatcher dispatcher(machine, ex);
        dispatcher.setProgramPointCount(1);
        if (!ex.start())
        {
            return 10;
        }

        tecomm::MachineConfigPayload cfg = toMachineConfigPayload(machine.machineConfig());
        uint8_t cfg_wire[tecomm::kMachineConfigPayloadWireSize];
        tecomm::encodeMachineConfigPayload(cfg, cfg_wire, sizeof(cfg_wire));
        std::vector<simbridge::OutboundFrame> out;
        dispatcher.dispatch(makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdSetMachineConfig), cfg_wire,
                                        sizeof(cfg_wire)),
                            tecomm::DecodeResult::Ok, out);
        if (out.empty() || !ackCode(out[0], tecomm::AckResultCode::InvalidState))
        {
            return 11;
        }

        tecomm::TestConfigurationPayload test = toTestConfigurationPayload(machine.testConfiguration());
        uint8_t test_wire[tecomm::kTestConfigurationPayloadWireSize];
        tecomm::encodeTestConfigurationPayload(test, test_wire, sizeof(test_wire));
        out.clear();
        dispatcher.dispatch(makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdSetTestConfiguration),
                                        test_wire, sizeof(test_wire)),
                            tecomm::DecodeResult::Ok, out);
        if (out.empty() || !ackCode(out[0], tecomm::AckResultCode::InvalidState))
        {
            return 12;
        }

        tecomm::MoveToPayload origin{1.0f, 2.0f, 3.0f};
        uint8_t origin_wire[tecomm::kMoveToPayloadWireSize];
        tecomm::encodeMoveToPayload(origin, origin_wire, sizeof(origin_wire));
        out.clear();
        dispatcher.dispatch(makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdSetPlateOrigin), origin_wire,
                                        sizeof(origin_wire)),
                            tecomm::DecodeResult::Ok, out);
        if (out.empty() || !ackCode(out[0], tecomm::AckResultCode::InvalidState))
        {
            return 13;
        }

        tecomm::BoolPayload spreader{};
        spreader.value = 1;
        uint8_t bool_wire[tecomm::kBoolPayloadWireSize];
        tecomm::encodeBoolPayload(spreader, bool_wire, sizeof(bool_wire));
        out.clear();
        dispatcher.dispatch(makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdSetSpreader), bool_wire,
                                        sizeof(bool_wire)),
                            tecomm::DecodeResult::Ok, out);
        if (out.empty() || !ackCode(out[0], tecomm::AckResultCode::InvalidState))
        {
            return 14;
        }

        ex.stop();
        out.clear();
        dispatcher.dispatch(makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdSetPlateOrigin), origin_wire,
                                        sizeof(origin_wire)),
                            tecomm::DecodeResult::Ok, out);
        if (out.empty() || !ackCode(out[0], tecomm::AckResultCode::Ok))
        {
            return 15;
        }
        if (machine.calibrationSet().plate_origin.x != 1.0f)
        {
            return 16;
        }
    }

    return 0;
}
