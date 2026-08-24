// Diagnostic reproduction of the GUI procedure-start sequence with persisted
// calibration numbers (plate_origin.z=17, safe_z_buffer=30). Diagnosis only.
#include "CommandDispatcher.hpp"
#include "ConfigMapping.hpp"

#include "Decoder.hpp"
#include "ForceSample.hpp"
#include "MessageIds.hpp"
#include "PayloadCodec.hpp"
#include "Payloads.hpp"
#include "ProtocolSpec.hpp"
#include "SafeZ.hpp"

#include "Machine.hpp"
#include "TestExecutor.hpp"
#include "Types.hpp"

#include <cstdio>
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

static tecomm::DecodedFrame makeDecoded(uint8_t message_id, const uint8_t* payload,
                                        std::size_t payload_length)
{
    tecomm::DecodedFrame frame{};
    frame.protocol_version = tecomm::kProtocolVersion;
    frame.message_id = message_id;
    frame.payload = payload;
    frame.payload_length = payload_length;
    return frame;
}

static const char* ackName(uint8_t code)
{
    switch (code)
    {
    case static_cast<uint8_t>(tecomm::AckResultCode::Ok):
        return "Ok";
    case static_cast<uint8_t>(tecomm::AckResultCode::InvalidState):
        return "InvalidState";
    case static_cast<uint8_t>(tecomm::AckResultCode::ProgramEmpty):
        return "ProgramEmpty";
    case static_cast<uint8_t>(tecomm::AckResultCode::Rejected):
        return "Rejected";
    case static_cast<uint8_t>(tecomm::AckResultCode::FaultLatched):
        return "FaultLatched";
    default:
        return "?";
    }
}

int main()
{
    const TestPoint plate_origin{55.0f, 57.0f, 17.0f};
    const TestPoint probe_center{40.0f, 40.0f, 10.0f};
    const TestPoint tool_offset{-0.000072f, -0.000061f, 0.000049f};
    const float safe_z_buffer = 30.0f;
    const TestPoint program_point{1.0f, 0.0f, 0.0f};

    Machine machine;
    TestExecutor executor(machine);
    simbridge::CommandDispatcher dispatcher(machine, executor);

    std::printf("=== Diagnose GUI-like CmdStart sequence ===\n");

    if (!homeMachine(machine))
    {
        std::printf("FAIL: homeMachine\n");
        return 1;
    }
    std::printf("after home: isHomed=%d pose.z=%.3f process=%u\n", machine.isHomed() ? 1 : 0,
                machine.z(), static_cast<unsigned>(executor.processState()));

    MachineConfig cfg = machine.machineConfig();
    cfg.motion.safe_z_buffer = safe_z_buffer;
    cfg.calibration.tool_offset = tool_offset;
    cfg.calibration.home_offset = TestPoint{0.0f, 0.0f, 0.0f};
    ConfigValidationError err = ConfigValidationError::Ok;
    if (!machine.trySetMachineConfig(cfg, err))
    {
        std::printf("FAIL: trySetMachineConfig err=%d\n", static_cast<int>(err));
        return 2;
    }
    std::printf("after setMachineConfig: safe_z_buffer=%.3f tool_offset.z=%.3f "
                "home_offset=(%.3f,%.3f,%.3f) runtime_locked=%d\n",
                machine.machineConfig().motion.safe_z_buffer, machine.calibrationSet().tool_offset.z,
                machine.calibrationSet().home_offset.x, machine.calibrationSet().home_offset.y,
                machine.calibrationSet().home_offset.z, machine.runtimeConfigLocked() ? 1 : 0);

    machine.setPlateOrigin(plate_origin);
    machine.setProbeCenter(probe_center);
    std::printf("after plate/probe: plate_valid=%d probe_valid=%d plate.z=%.3f\n",
                machine.calibrationSet().plate_origin_valid ? 1 : 0,
                machine.calibrationSet().probe_center_valid ? 1 : 0,
                machine.calibrationSet().plate_origin.z);

    TestProgram program;
    program.add(program_point);
    float max_z = 0.0f;
    float safe_z_plate = 0.0f;
    float safe_z_machine = 0.0f;
    const bool ok_plate = computeSafeZPlate(program, safe_z_buffer, max_z, safe_z_plate);
    const bool ok_mach =
        ok_plate && computeSafeZMachine(machine.calibrationSet(), safe_z_plate, safe_z_machine);
    std::printf("Safe-Z: ok_plate=%d ok_machine=%d max_z=%.3f safe_z_plate=%.3f "
                "safe_z_machine=%.3f point_machine_z=%.3f\n",
                ok_plate ? 1 : 0, ok_mach ? 1 : 0, max_z, safe_z_plate, safe_z_machine,
                plate_origin.z + program_point.z + tool_offset.z);

    tecomm::LoadProgramPayload load{};
    load.point_count = 1;
    load.points[0] = tecomm::Point3f{program_point.x, program_point.y, program_point.z};
    uint8_t payload[tecomm::kLoadProgramMaxPayloadWireSize];
    std::size_t bytes_written = 0;
    tecomm::encodeLoadProgramPayload(load, payload, sizeof(payload), bytes_written);

    std::vector<simbridge::OutboundFrame> out;
    tecomm::DecodedFrame frame =
        makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdLoadProgram), payload, bytes_written);
    dispatcher.dispatch(frame, tecomm::DecodeResult::Ok, out);
    tecomm::AckPayload ack{};
    if (out.empty() || !decodeOutboundAck(out[0], ack))
    {
        std::printf("FAIL: CmdLoadProgram no ack\n");
        return 3;
    }
    std::printf("CmdLoadProgram ack=%s\n", ackName(ack.result_code));

    out.clear();
    frame = makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdStart), nullptr, 0);
    dispatcher.dispatch(frame, tecomm::DecodeResult::Ok, out);
    if (out.empty() || !decodeOutboundAck(out[0], ack))
    {
        std::printf("FAIL: CmdStart no ack\n");
        return 4;
    }
    std::printf("CmdStart ack=%s process_state=%u\n", ackName(ack.result_code),
                static_cast<unsigned>(executor.processState()));

    if (ack.result_code != static_cast<uint8_t>(tecomm::AckResultCode::Ok))
    {
        std::printf("RESULT: GUI-like sequence FAILED with %s\n", ackName(ack.result_code));
        return 10;
    }
    std::printf("RESULT: GUI-like sequence SUCCEEDED with persisted calibration numbers\n");

    // Show that a second start-path command while Running yields the same GUI string.
    const tecomm::MachineConfigPayload cfg_payload =
        toMachineConfigPayload(machine.machineConfig());
    uint8_t cfg_bytes[tecomm::kMachineConfigPayloadWireSize];
    if (!tecomm::encodeMachineConfigPayload(cfg_payload, cfg_bytes, sizeof(cfg_bytes)))
    {
        std::printf("WARN: encodeMachineConfigPayload failed\n");
        return 0;
    }
    out.clear();
    frame = makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdSetMachineConfig), cfg_bytes,
                        sizeof(cfg_bytes));
    dispatcher.dispatch(frame, tecomm::DecodeResult::Ok, out);
    if (!out.empty() && decodeOutboundAck(out[0], ack))
    {
        std::printf("While process Running, CmdSetMachineConfig ack=%s "
                    "(identical GUI text 'Invalid state for command' before CmdStart)\n",
                    ackName(ack.result_code));
    }

    return 0;
}
