// After switch-origin MCS, SimPlant latches probe triggers in raw. The bridge
// must convert to machine-frame before Core backoff, otherwise NegZ retracts
// toward +max (positive machine Z / the switch) instead of +backoff millimetres.
#include "HostInputs.hpp"
#include "MessageIds.hpp"
#include "SimProtocolBridge.hpp"
#include "Types.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{

tecomm::DecodedFrame emptyCmd(uint8_t id)
{
    tecomm::DecodedFrame f{};
    f.protocol_version = tecomm::kProtocolVersion;
    f.message_id = id;
    return f;
}

bool homeViaPlant(simbridge::SimProtocolBridge& b)
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

bool nearEqual(float a, float b, float eps)
{
    return std::fabs(a - b) <= eps;
}

} // namespace

int main()
{
    simbridge::SimProtocolBridge bridge;
    Machine& m = bridge.machine();
    const auto disc = bridge.plant().probeDisc();

    simplant::PlantLimits lim = bridge.plant().limits();
    lim.z_max = 80.0f;
    lim.z_min = -20.0f;
    lim.x_min = -20.0f;
    lim.y_min = -20.0f;
    bridge.plant().setLimits(lim);

    if (!homeViaPlant(bridge))
    {
        std::fprintf(stderr, "home fail\n");
        return 1;
    }
    if (!(m.referenceOffsetZ() > 20.0f))
    {
        std::fprintf(stderr, "expected non-trivial Z offset after MCS, got %.3f\n",
                     m.referenceOffsetZ());
        return 2;
    }

    const TestPoint disc_m{disc.center.x - m.referenceOffsetX(), disc.center.y - m.referenceOffsetY(),
                           disc.center.z - m.referenceOffsetZ()};
    const float hub_z = disc_m.z + 8.0f;
    m.moveTo(disc_m.x, disc_m.y, hub_z);
    for (int i = 0; i < 8000 && m.motionState() == MotionState::Moving; ++i)
    {
        bridge.tick(0.05f);
    }
    if (!nearEqual(m.z(), hub_z, 0.2f))
    {
        std::fprintf(stderr, "failed to reach hub z=%.3f (at %.3f)\n", hub_z, m.z());
        return 3;
    }

    if (!bridge.executor().startProbeCalibration())
    {
        std::fprintf(stderr, "startProbeCalibration failed homingState=%u\n",
                     static_cast<unsigned>(m.homingState()));
        return 4;
    }

    const float backoff = m.machineConfig().probe.backoff;
    const float max_raise = backoff + 4.0f;
    bool saw_first = false;
    float contact_z = 0.0f;
    float max_z_after_contact = 0.0f;
    std::uint32_t first_seq = 0;
    bool saw_second = false;

    for (int i = 0; i < 40000 && bridge.executor().processState() == ProcessState::Running; ++i)
    {
        bridge.tick(0.05f);
        const HostInputs& in = m.hostInputs();
        if (!saw_first && in.probeTriggered && in.probeTriggerPoseValid)
        {
            const float expected_machine_z = disc.center.z - m.referenceOffsetZ();
            if (std::fabs(in.probeTriggerZ - disc.center.z) < 5.0f)
            {
                std::fprintf(stderr, "trigger Z still raw (%.3f), disc raw z=%.3f\n",
                             in.probeTriggerZ, disc.center.z);
                return 8;
            }
            if (std::fabs(in.probeTriggerZ - expected_machine_z) > 5.0f)
            {
                std::fprintf(stderr, "trigger Z=%.3f not machine (expected ~%.3f)\n",
                             in.probeTriggerZ, expected_machine_z);
                return 9;
            }
            saw_first = true;
            first_seq = in.probeTriggerSequence;
            contact_z = m.z();
            max_z_after_contact = contact_z;
            continue;
        }
        if (!saw_first)
        {
            continue;
        }
        if (m.z() > max_z_after_contact)
        {
            max_z_after_contact = m.z();
        }
        if (max_z_after_contact > contact_z + max_raise)
        {
            std::fprintf(stderr,
                         "Z backoff overshoot: contact=%.3f max=%.3f raise=%.3f "
                         "(raw trigger used as machine target?)\n",
                         contact_z, max_z_after_contact, max_z_after_contact - contact_z);
            return 5;
        }
        if (in.probeTriggered && in.probeTriggerSequence != 0 && in.probeTriggerSequence != first_seq)
        {
            saw_second = true;
            break;
        }
    }

    if (!saw_first)
    {
        std::fprintf(stderr, "never saw first Z contact\n");
        return 6;
    }
    if (!saw_second)
    {
        std::fprintf(stderr, "never saw fine-search re-contact after backoff\n");
        return 7;
    }
    return 0;
}
