// Integration tests for M13 SimProtocolBridge (LOAD/START/tick/Completed, pause/resume, pneumatics).
#include "SimProtocolBridge.hpp"

#include "Decoder.hpp"
#include "MessageIds.hpp"
#include "PayloadCodec.hpp"
#include "Payloads.hpp"
#include "SimPlant.hpp"

#include "Types.hpp"

#include <cmath>

static tecomm::DecodedFrame makeDecoded(uint8_t message_id, const uint8_t* payload, std::size_t payload_length)
{
    tecomm::DecodedFrame frame{};
    frame.protocol_version = tecomm::kProtocolVersion;
    frame.message_id = message_id;
    frame.payload = payload;
    frame.payload_length = payload_length;
    return frame;
}

static bool nearEqual(float a, float b, float eps = 1e-3f)
{
    return std::fabs(a - b) <= eps;
}

static bool dispatchEmpty(simbridge::SimProtocolBridge& bridge, tecomm::MessageId id,
                          std::vector<simbridge::OutboundFrame>& out)
{
    const tecomm::DecodedFrame frame = makeDecoded(static_cast<uint8_t>(id), nullptr, 0);
    bridge.handleFrame(frame, tecomm::DecodeResult::Ok, out);
    return !out.empty();
}

int main()
{
    // LOAD -> START -> tick until Completed
    {
        simbridge::SimProtocolBridge bridge;
        bridge.machine().setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});

        simplant::PlantLimits limits{};
        limits.z_min = -100.0f;
        bridge.plant().setLimits(limits);

        TestConfiguration cfg = bridge.machine().testConfiguration();
        cfg.target_force_N = 0.1f;
        cfg.mode = TestMode::Aging;
        cfg.iterations = 1u;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!bridge.machine().trySetTestConfiguration(cfg, err))
        {
            return 99;
        }

        tecomm::LoadProgramPayload load{};
        load.point_count = 3;
        load.points[0] = tecomm::Point3f{10.0f, 0.0f, 0.0f};
        load.points[1] = tecomm::Point3f{20.0f, 5.0f, 0.0f};
        load.points[2] = tecomm::Point3f{30.0f, 5.0f, -5.0f};

        uint8_t payload[tecomm::kLoadProgramMaxPayloadWireSize];
        std::size_t bytes_written = 0;
        tecomm::encodeLoadProgramPayload(load, payload, sizeof(payload), bytes_written);

        std::vector<simbridge::OutboundFrame> out;
        tecomm::DecodedFrame frame = makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdLoadProgram),
                                                 payload, bytes_written);
        bridge.handleFrame(frame, tecomm::DecodeResult::Ok, out);

        out.clear();
        if (!dispatchEmpty(bridge, tecomm::MessageId::CmdStart, out))
        {
            return 1;
        }
        if (bridge.executor().processState() != ProcessState::Running)
        {
            return 2;
        }

        constexpr float kDt = 0.05f;
        constexpr int kMaxTicks = 5000;
        int ticks = 0;
        while (bridge.executor().processState() == ProcessState::Running && ticks < kMaxTicks)
        {
            bridge.applyHostInputs();
            bridge.tick(kDt);
            ++ticks;
        }

        if (bridge.executor().processState() != ProcessState::Completed)
        {
            return 3;
        }
        if (!nearEqual(bridge.machine().x(), 0.0f) || !nearEqual(bridge.machine().y(), 0.0f) ||
            !nearEqual(bridge.machine().z(), 0.0f))
        {
            return 4;
        }
    }

    // PAUSE -> RESUME during manual move
    {
        simbridge::SimProtocolBridge bridge;

        tecomm::MoveToPayload move{15.0f, 0.0f, 0.0f};
        uint8_t move_payload[tecomm::kMoveToPayloadWireSize];
        tecomm::encodeMoveToPayload(move, move_payload, sizeof(move_payload));

        std::vector<simbridge::OutboundFrame> out;
        tecomm::DecodedFrame frame =
            makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdMoveTo), move_payload, sizeof(move_payload));
        bridge.handleFrame(frame, tecomm::DecodeResult::Ok, out);

        bridge.applyHostInputs();
        bridge.tick(0.05f);
        const float x_before_pause = bridge.machine().x();

        out.clear();
        dispatchEmpty(bridge, tecomm::MessageId::CmdPause, out);
        bridge.applyHostInputs();
        bridge.tick(0.05f);
        const float x_while_paused = bridge.machine().x();
        if (!nearEqual(x_before_pause, x_while_paused))
        {
            return 10;
        }

        out.clear();
        dispatchEmpty(bridge, tecomm::MessageId::CmdResume, out);
        bridge.applyHostInputs();
        for (int i = 0; i < 500 && bridge.machine().machineState() != MachineState::Idle; ++i)
        {
            bridge.tick(0.05f);
            bridge.applyHostInputs();
        }
        if (!nearEqual(bridge.machine().x(), 15.0f))
        {
            return 11;
        }
    }

    // STOP during manual move
    {
        simbridge::SimProtocolBridge bridge;

        tecomm::MoveToPayload move{25.0f, 0.0f, 0.0f};
        uint8_t move_payload[tecomm::kMoveToPayloadWireSize];
        tecomm::encodeMoveToPayload(move, move_payload, sizeof(move_payload));

        std::vector<simbridge::OutboundFrame> out;
        tecomm::DecodedFrame frame =
            makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdMoveTo), move_payload, sizeof(move_payload));
        bridge.handleFrame(frame, tecomm::DecodeResult::Ok, out);

        out.clear();
        dispatchEmpty(bridge, tecomm::MessageId::CmdStop, out);
        if (bridge.machine().machineState() != MachineState::Stopped)
        {
            return 20;
        }
    }

    // Pneumatic status in push snapshot
    {
        simbridge::SimProtocolBridge bridge;

        tecomm::BoolPayload value{};
        value.value = 1;
        uint8_t bool_payload[tecomm::kBoolPayloadWireSize];
        tecomm::encodeBoolPayload(value, bool_payload, sizeof(bool_payload));

        std::vector<simbridge::OutboundFrame> out;
        tecomm::DecodedFrame frame = makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdSetSpreader),
                                                 bool_payload, sizeof(bool_payload));
        bridge.handleFrame(frame, tecomm::DecodeResult::Ok, out);

        bridge.applyHostInputs();
        out.clear();
        bridge.appendStatusPush(out);

        tecomm::DecodedFrame decoded{};
        std::size_t consumed = 0;
        if (tecomm::decodeFrame(out[0].data, out[0].length, decoded, consumed) != tecomm::DecodeResult::Ok)
        {
            return 30;
        }
        tecomm::StatusReportPayload status{};
        if (!tecomm::decodeStatusReportPayload(decoded.payload, decoded.payload_length, status))
        {
            return 31;
        }
        if (status.spreader_extend != 1)
        {
            return 32;
        }
    }

    return 0;
}
