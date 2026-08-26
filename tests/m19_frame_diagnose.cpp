// Isolated M19 after plant home — prove measured probe_center frame.
#include "SimProtocolBridge.hpp"
#include "MessageIds.hpp"
#include "Types.hpp"
#include <cstdio>
#include <vector>

static tecomm::DecodedFrame emptyCmd(uint8_t id)
{
    tecomm::DecodedFrame f{};
    f.protocol_version = tecomm::kProtocolVersion;
    f.message_id = id;
    return f;
}

static bool homeViaPlant(simbridge::SimProtocolBridge& b)
{
    std::vector<simbridge::OutboundFrame> out;
    const uint8_t cmd = b.machine().machineCoordinateSystemValid()
                            ? static_cast<uint8_t>(tecomm::MessageId::CmdHome)
                            : static_cast<uint8_t>(tecomm::MessageId::CmdDefineMcs);
    b.handleFrame(emptyCmd(cmd), tecomm::DecodeResult::Ok, out);
    for (int i = 0; i < 25000; ++i)
    {
        b.tick(0.05f);
        if (b.machine().isHomed() && b.machine().homingState() == HomingState::Homed)
        {
            return true;
        }
        if (b.machine().homingState() == HomingState::HomingError)
        {
            return false;
        }
    }
    return false;
}

int main()
{
    simbridge::SimProtocolBridge bridge;
    Machine& m = bridge.machine();
    const auto disc = bridge.plant().probeDisc();

    // Extra +Z room so M19 approach/search cannot immediately hit plant z_max=50
    // while still using plant-home offsets (same frame issue). Keep x/y max.
    simplant::PlantLimits lim = bridge.plant().limits();
    lim.z_max = 80.0f;
    lim.z_min = -20.0f;
    lim.x_min = -20.0f;
    lim.y_min = -20.0f;
    bridge.plant().setLimits(lim);

    if (!homeViaPlant(bridge))
    {
        std::printf("home fail\n");
        return 1;
    }
    std::printf("after home: machine=(%.3f,%.3f,%.3f) raw=(%.3f,%.3f,%.3f) offset=(%.3f,%.3f,%.3f)\n",
                m.x(), m.y(), m.z(), m.rawX(), m.rawY(), m.rawZ(), m.referenceOffsetX(),
                m.referenceOffsetY(), m.referenceOffsetZ());

    const TestPoint disc_m{disc.center.x - m.referenceOffsetX(), disc.center.y - m.referenceOffsetY(),
                           disc.center.z - m.referenceOffsetZ()};
    std::printf("disc RAW=(%.3f,%.3f,%.3f) => MACHINE=(%.3f,%.3f,%.3f)\n", disc.center.x, disc.center.y,
                disc.center.z, disc_m.x, disc_m.y, disc_m.z);
    std::printf("probe_offset_reference MACHINE=(%.3f,%.3f,%.3f)\n",
                m.machineConfig().probe.probe_offset_reference.x,
                m.machineConfig().probe.probe_offset_reference.y,
                m.machineConfig().probe.probe_offset_reference.z);

    m.moveTo(disc_m.x, disc_m.y, disc_m.z + 8.0f);
    for (int i = 0; i < 8000 && m.motionState() == MotionState::Moving; ++i)
    {
        bridge.tick(0.05f);
    }
    std::printf("M19 start TCP machine=(%.3f,%.3f,%.3f) raw=(%.3f,%.3f,%.3f)\n", m.x(), m.y(), m.z(),
                m.rawX(), m.rawY(), m.rawZ());

    if (!bridge.executor().startProbeCalibration())
    {
        std::printf("startProbeCalibration failed homingState=%u\n",
                    static_cast<unsigned>(m.homingState()));
        return 2;
    }
    for (int i = 0; i < 80000 && bridge.executor().processState() == ProcessState::Running; ++i)
    {
        bridge.tick(0.05f);
    }
    std::printf("after M19: process=%u phase=%u probe_valid=%d probe=(%.3f,%.3f,%.3f)\n",
                static_cast<unsigned>(bridge.executor().processState()),
                static_cast<unsigned>(bridge.executor().procedurePhase()),
                m.calibrationSet().probe_center_valid ? 1 : 0, m.calibrationSet().probe_center.x,
                m.calibrationSet().probe_center.y, m.calibrationSet().probe_center.z);
    std::printf("tool_offset=(%.3f,%.3f,%.3f)\n", m.calibrationSet().tool_offset.x,
                m.calibrationSet().tool_offset.y, m.calibrationSet().tool_offset.z);
    std::printf("pose machine=(%.3f,%.3f,%.3f) raw=(%.3f,%.3f,%.3f)\n", m.x(), m.y(), m.z(), m.rawX(),
                m.rawY(), m.rawZ());
    return 0;
}
