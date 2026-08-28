// MCS define vs reference; plant home; procedure start with persisted-style calib.
#include "SimProtocolBridge.hpp"

#include "MessageIds.hpp"
#include "MotionPlanner.hpp"
#include "PayloadCodec.hpp"
#include "Payloads.hpp"
#include "SafeZ.hpp"
#include "Types.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

static tecomm::DecodedFrame makeDecoded(uint8_t message_id, const uint8_t* payload = nullptr,
                                        std::size_t payload_length = 0)
{
    tecomm::DecodedFrame frame{};
    frame.protocol_version = tecomm::kProtocolVersion;
    frame.message_id = message_id;
    frame.payload = payload;
    frame.payload_length = payload_length;
    return frame;
}

static bool nearEqual(float a, float b, float eps = 5.0e-2f)
{
    return std::fabs(a - b) <= eps;
}

static bool homeViaPlant(simbridge::SimProtocolBridge& bridge, int max_ticks = 20000)
{
    std::vector<simbridge::OutboundFrame> out;
    const uint8_t cmd = bridge.machine().machineCoordinateSystemValid()
                            ? static_cast<uint8_t>(tecomm::MessageId::CmdHome)
                            : static_cast<uint8_t>(tecomm::MessageId::CmdDefineMcs);
    bridge.handleFrame(makeDecoded(cmd), tecomm::DecodeResult::Ok, out);
    for (int i = 0; i < max_ticks; ++i)
    {
        bridge.tick(0.05f);
        if (bridge.machine().isHomed() && bridge.machine().homingState() == HomingState::Homed)
        {
            return true;
        }
        if (bridge.machine().homingState() == HomingState::HomingError)
        {
            return false;
        }
    }
    return false;
}

static bool loadAndStart(simbridge::SimProtocolBridge& bridge, const TestPoint& plate_point)
{
    tecomm::LoadProgramPayload load{};
    load.point_count = 1;
    load.points[0] = tecomm::Point3f{plate_point.x, plate_point.y, plate_point.z};
    uint8_t payload[tecomm::kLoadProgramMaxPayloadWireSize];
    std::size_t bytes_written = 0;
    tecomm::encodeLoadProgramPayload(load, payload, sizeof(payload), bytes_written);
    std::vector<simbridge::OutboundFrame> out;
    bridge.handleFrame(makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdLoadProgram), payload,
                                   bytes_written),
                       tecomm::DecodeResult::Ok, out);
    out.clear();
    bridge.handleFrame(makeDecoded(static_cast<uint8_t>(tecomm::MessageId::CmdStart)),
                       tecomm::DecodeResult::Ok, out);
    return bridge.executor().processState() == ProcessState::Running;
}

static int testA_defineMcsPlantHome()
{
    simbridge::SimProtocolBridge bridge;
    Machine& m = bridge.machine();
    if (!homeViaPlant(bridge))
    {
        return 10;
    }
    if (!m.machineCoordinateSystemValid() || !m.isHomed())
    {
        return 11;
    }
    // Switch-origin define: home = (-R,-R,-R), offset ≈ plant +max (raw at switch).
    const float r = 2.0f;
    if (!nearEqual(m.x(), -r, 0.5f) || !nearEqual(m.y(), -r, 0.5f) || !nearEqual(m.z(), -r, 0.5f))
    {
        std::fprintf(stderr, "A machine=(%.3f,%.3f,%.3f) expected~(-%.3f,-%.3f,-%.3f)\n", m.x(),
                     m.y(), m.z(), r, r, r);
        return 12;
    }
    const float ox = bridge.plant().limits().x_max;
    const float oy = bridge.plant().limits().y_max;
    const float oz = bridge.plant().limits().z_max;
    if (std::fabs(m.referenceOffsetX() - ox) > 0.5f || std::fabs(m.referenceOffsetY() - oy) > 0.5f ||
        std::fabs(m.referenceOffsetZ() - oz) > 0.5f)
    {
        std::fprintf(stderr, "A offset=(%.3f,%.3f,%.3f) expected ~(%.3f,%.3f,%.3f)\n",
                     m.referenceOffsetX(), m.referenceOffsetY(), m.referenceOffsetZ(), ox, oy, oz);
        return 13;
    }
    return 0;
}

static int testB_normalHomeKeepsCalib()
{
    simbridge::SimProtocolBridge bridge;
    Machine& m = bridge.machine();
    if (!homeViaPlant(bridge))
    {
        return 20;
    }
    const TestPoint home = m.calibrationSet().home_offset;
    m.setPlateOrigin(TestPoint{-45.0f, -43.0f, -33.0f});
    m.setProbeCenter(TestPoint{-60.0f, -60.0f, -40.0f});
    m.setToolOffset(TestPoint{-0.0001f, -0.0001f, 0.0001f});

    m.moveTo(home.x - 5.0f, home.y - 5.0f, home.z - 5.0f);
    for (int i = 0; i < 2000 && m.motionState() == MotionState::Moving; ++i)
    {
        bridge.tick(0.05f);
    }
    if (m.isHomed())
    {
        return 21;
    }
    if (!homeViaPlant(bridge))
    {
        return 22;
    }
    if (!nearEqual(m.x(), home.x, 0.5f) || !nearEqual(m.y(), home.y, 0.5f) ||
        !nearEqual(m.z(), home.z, 0.5f))
    {
        return 23;
    }
    if (!nearEqual(m.calibrationSet().plate_origin.x, -45.0f) ||
        !nearEqual(m.calibrationSet().probe_center.x, -60.0f) ||
        !m.calibrationSet().plate_origin_valid || !m.calibrationSet().probe_center_valid)
    {
        return 24;
    }
    return 0;
}

static int testC_stepLoss()
{
    simbridge::SimProtocolBridge bridge;
    Machine& m = bridge.machine();
    if (!homeViaPlant(bridge))
    {
        return 30;
    }
    const TestPoint home = m.calibrationSet().home_offset;
    m.setPlateOrigin(TestPoint{-45.0f, -43.0f, -33.0f});
    m.setProbeCenter(TestPoint{-60.0f, -60.0f, -40.0f});
    m.shiftRawPose(5.0f, -3.0f, 4.0f);
    m.invalidateHoming();
    if (!homeViaPlant(bridge))
    {
        return 31;
    }
    if (!nearEqual(m.x(), home.x, 0.6f) || !nearEqual(m.y(), home.y, 0.6f) ||
        !nearEqual(m.z(), home.z, 0.6f))
    {
        std::fprintf(stderr, "C after step-loss home pose=(%.3f,%.3f,%.3f) want=(%.3f,%.3f,%.3f)\n",
                     m.x(), m.y(), m.z(), home.x, home.y, home.z);
        return 32;
    }
    if (!nearEqual(m.calibrationSet().probe_center.x, -60.0f) ||
        !nearEqual(m.calibrationSet().plate_origin.x, -45.0f))
    {
        return 33;
    }
    m.moveTo(home.x - 10.0f, home.y, home.z);
    for (int i = 0; i < 3000 && m.motionState() == MotionState::Moving; ++i)
    {
        bridge.tick(0.05f);
    }
    if (!nearEqual(m.x(), home.x - 10.0f, 0.5f))
    {
        return 34;
    }
    return 0;
}

static int testD_procedureStartWithPersistedStyleCalib()
{
    simbridge::SimProtocolBridge bridge;
    Machine& m = bridge.machine();
    if (!homeViaPlant(bridge))
    {
        return 40;
    }
    // Plate/probe in machine frame (workspace is negative; origin at +max switches).
    m.setPlateOrigin(TestPoint{-45.0f, -43.0f, -33.0f});
    m.setProbeCenter(TestPoint{-60.0f, -60.0f, -40.0f});
    if (!loadAndStart(bridge, TestPoint{5.0f, 2.0f, 0.0f}))
    {
        std::fprintf(stderr, "D start failed process=%u\n",
                     static_cast<unsigned>(bridge.executor().processState()));
        return 41;
    }
    const float z0 = m.z();
    bool moved = false;
    for (int i = 0; i < 300; ++i)
    {
        bridge.tick(0.05f);
        if (bridge.executor().processState() == ProcessState::Aborted)
        {
            return 42;
        }
        if (std::fabs(m.z() - z0) > 0.2f || m.motionState() == MotionState::Moving)
        {
            moved = true;
            break;
        }
    }
    if (!moved)
    {
        return 43;
    }
    return 0;
}

static int testE_offsetMath()
{
    MotionPlanner planner;
    planner.setCurrentPose(10.0f, 20.0f, 30.0f);
    planner.referenceMachinePose(0.0f, 0.0f, 0.0f);
    const float rx = planner.rawX();
    planner.setMotionParams(50.0f, 0.0f);
    planner.moveTo(20.0f, 0.0f, 0.0f);
    for (int i = 0; i < 2000 && planner.motionState() == MotionState::Moving; ++i)
    {
        planner.tick(0.05f);
    }
    if (!nearEqual(planner.x(), 20.0f, 0.1f) || !nearEqual(planner.rawX(), rx + 20.0f, 0.1f))
    {
        return 50;
    }
    return 0;
}

int main()
{
    if (const int rc = testA_defineMcsPlantHome())
    {
        return rc;
    }
    if (const int rc = testB_normalHomeKeepsCalib())
    {
        return rc;
    }
    if (const int rc = testC_stepLoss())
    {
        return rc;
    }
    if (const int rc = testD_procedureStartWithPersistedStyleCalib())
    {
        return rc;
    }
    if (const int rc = testE_offsetMath())
    {
        return rc;
    }
    std::printf("procedure_start_homing_regression_tests OK\n");
    return 0;
}
