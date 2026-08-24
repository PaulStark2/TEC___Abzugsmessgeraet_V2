#include "ForceSample.hpp"
#include "HostInputs.hpp"
#include "Machine.hpp"
#include "ProbeGeometry.hpp"
#include "ProbeSearch.hpp"

#include <cmath>

static bool nearEqual(float a, float b, float eps = 1.0e-3f)
{
    return std::fabs(a - b) <= eps;
}

static HostInputs withDiscTrigger(const Machine& machine, const TestPoint& disc, HostInputs in = HostInputs{})
{
    const MachineConfig::Probe& probe = machine.machineConfig().probe;
    if (toolContactsProbeDisc(machine.x(), machine.y(), machine.z(), disc.x, disc.y, disc.z,
                              probe.measuring_head_diameter, probe.measuring_head_thickness,
                              probe.tool_diameter, probe.tool_cylindrical_height))
    {
        in.probeTriggered = true;
        in.probeTriggerPoseValid = true;
        in.probeTriggerX = machine.x();
        in.probeTriggerY = machine.y();
        in.probeTriggerZ = machine.z();
        in.probeTriggerSequence = 1;
    }
    return withFreshForce(in);
}

static ProbeSearch::Request zRequest()
{
    ProbeSearch::Request r{};
    r.reference = TestPoint{0.0f, 0.0f, 5.0f};
    r.dir = ProbeSearchDir::NegZ;
    r.approach_distance = 10.0f;
    r.search_distance = 10.0f;
    r.search_depth = 8.0f;
    r.lateral_offset = 15.25f;
    r.xy_inner_radius = 2.0f;
    r.backoff = 1.0f;
    r.approach_speed = 20.0f;
    r.fast_speed = 10.0f;
    r.fine_speed = 5.0f;
    r.num_fine_searches = 0;
    return r;
}

static bool runUntilDone(ProbeSearch& search, Machine& machine, const TestPoint& disc, int max_ticks)
{
    for (int i = 0; i < max_ticks && !search.isComplete() && !search.isError(); ++i)
    {
        machine.tick(0.05f);
        machine.setHostInputs(withDiscTrigger(machine, disc));
        search.tick(0.05f, machine);
    }
    return search.isComplete() && !search.isError();
}

static void tickNoDisc(Machine& machine, ProbeSearch& search)
{
    machine.setHostInputs(withFreshForce(HostInputs{}));
    machine.tick(0.05f);
    search.tick(0.05f, machine);
}

int main()
{
    const TestPoint disc{0.0f, 0.0f, 5.0f};

    // Trigger pose is the host-latched pose, not a later TCP.
    {
        Machine m;
        HostInputs in{};
        in.probeTriggered = true;
        in.probeTriggerPoseValid = true;
        in.probeTriggerX = 1.0f;
        in.probeTriggerY = 2.0f;
        in.probeTriggerZ = 3.0f;
        in.probeTriggerSequence = 7;
        m.setHostInputs(withFreshForce(in));
        m.moveTo(9.0f, 9.0f, 9.0f);
        m.tick(0.05f);
        TestPoint pose{};
        if (!m.consumeProbeTrigger(pose))
        {
            return 1;
        }
        if (!nearEqual(pose.x, 1.0f) || !nearEqual(pose.y, 2.0f) || !nearEqual(pose.z, 3.0f))
        {
            return 2;
        }
        if (m.consumeProbeTrigger(pose))
        {
            return 3;
        }
    }

    // Z search hits the disc top and reports that TCP Z.
    {
        Machine m;
        ProbeSearch search;
        if (!search.start(m, zRequest()))
        {
            return 10;
        }
        if (!runUntilDone(search, m, disc, 800))
        {
            return 11;
        }
        const float top = disc.z + 0.5f * m.machineConfig().probe.measuring_head_thickness;
        if (!nearEqual(search.result().z, top, 0.6f))
        {
            return 12;
        }
        if (!nearEqual(search.result().x, 0.0f, 0.6f) || !nearEqual(search.result().y, 0.0f, 0.6f))
        {
            return 13;
        }
    }

    // No trigger before the search limit → error.
    {
        Machine m;
        ProbeSearch search;
        ProbeSearch::Request r = zRequest();
        r.reference = TestPoint{80.0f, 80.0f, 5.0f};
        search.start(m, r);
        for (int i = 0; i < 800 && !search.isComplete() && !search.isError(); ++i)
        {
            tickNoDisc(m, search);
        }
        if (!search.isError() || search.isComplete())
        {
            return 20;
        }
    }

    // Abort while searching is an error without a result.
    {
        Machine m;
        ProbeSearch search;
        search.start(m, zRequest());
        m.setHostInputs(withFreshForce(HostInputs{}));
        m.tick(0.05f);
        search.tick(0.05f, m);
        search.abort();
        if (!search.isError() || search.isComplete())
        {
            return 30;
        }
    }

    // Fault during search is an error.
    {
        Machine m;
        ProbeSearch search;
        search.start(m, zRequest());
        HostInputs in{};
        in.e_stop = true;
        m.setHostInputs(withFreshForce(in));
        m.tick(0.05f);
        search.tick(0.05f, m);
        if (!search.isError())
        {
            return 40;
        }
    }

    // Fine search averages multiple triggers.
    {
        Machine m;
        ProbeSearch search;
        ProbeSearch::Request r = zRequest();
        r.num_fine_searches = 3;
        r.fine_speed = 5.0f;
        if (!search.start(m, r) || !runUntilDone(search, m, disc, 2000))
        {
            return 50;
        }
        const float top = disc.z + 0.5f * m.machineConfig().probe.measuring_head_thickness;
        if (!nearEqual(search.result().z, top, 0.6f))
        {
            return 51;
        }
    }

    // +X lateral search contacts the +X rim.
    {
        Machine m;
        ProbeSearch search;
        ProbeSearch::Request r = zRequest();
        r.dir = ProbeSearchDir::PosX;
        r.num_fine_searches = 0;
        if (!search.start(m, r) || !runUntilDone(search, m, disc, 1200))
        {
            return 60;
        }
        const float r_sum = expectedXyContactRadius(8.0f, 2.5f);
        if (!nearEqual(search.result().x, disc.x + r_sum, 0.6f))
        {
            return 61;
        }
    }

    // XY search without trigger stops at the inner radius, not the probe axis.
    {
        Machine m;
        ProbeSearch search;
        ProbeSearch::Request r = zRequest();
        r.dir = ProbeSearchDir::PosX;
        r.reference = TestPoint{40.0f, 40.0f, 10.0f};
        r.search_depth = 5.0f;
        r.num_fine_searches = 0;
        search.start(m, r);
        float min_search_r = 1.0e9f;
        for (int i = 0; i < 800 && !search.isComplete() && !search.isError(); ++i)
        {
            tickNoDisc(m, search);
            if (search.state() == ProbeSearch::State::FastSearch ||
                search.state() == ProbeSearch::State::FineSearch)
            {
                const float dx = m.x() - r.reference.x;
                const float dy = m.y() - r.reference.y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist < min_search_r)
                {
                    min_search_r = dist;
                }
            }
        }
        if (!search.isError() || search.isComplete())
        {
            return 70;
        }
        if (min_search_r + 0.05f < r.xy_inner_radius)
        {
            return 71;
        }
        const float dx = m.x() - r.reference.x;
        const float dy = m.y() - r.reference.y;
        const float end_r = std::sqrt(dx * dx + dy * dy);
        if (end_r + 0.05f < r.xy_inner_radius)
        {
            return 72;
        }
        if (end_r > r.xy_inner_radius + 1.0f)
        {
            return 73;
        }
    }

    // Descend (Z prep) uses approach_speed, not fine_speed.
    {
        Machine m;
        ProbeSearch search;
        ProbeSearch::Request r = zRequest();
        r.dir = ProbeSearchDir::PosX;
        r.approach_speed = 20.0f;
        r.fine_speed = 5.0f;
        r.num_fine_searches = 0;
        if (!search.start(m, r))
        {
            return 80;
        }
        float peak_descend_vz = 0.0f;
        for (int i = 0; i < 800 && !search.isComplete() && !search.isError(); ++i)
        {
            tickNoDisc(m, search);
            if (search.state() == ProbeSearch::State::Descend)
            {
                const float vz = std::fabs(m.zVelocity());
                if (vz > peak_descend_vz)
                {
                    peak_descend_vz = vz;
                }
            }
            if (search.state() == ProbeSearch::State::FastSearch)
            {
                break;
            }
        }
        if (peak_descend_vz < 15.0f)
        {
            return 81; // should be ~approach_speed, not ~fine_speed
        }
    }

    // Retreat clears to startPose (lateral_offset), not merely raise Z at contact XY.
    {
        Machine m;
        ProbeSearch search;
        ProbeSearch::Request r = zRequest();
        r.dir = ProbeSearchDir::PosX;
        r.num_fine_searches = 0;
        if (!search.start(m, r) || !runUntilDone(search, m, disc, 1200))
        {
            return 90;
        }
        const float expected_x = disc.x + r.lateral_offset;
        const float expected_z = disc.z + r.approach_distance;
        if (!nearEqual(m.x(), expected_x, 0.8f) || !nearEqual(m.y(), disc.y, 0.8f) ||
            !nearEqual(m.z(), expected_z, 0.8f))
        {
            return 91;
        }
    }

    // Symmetry: +Y retreat also ends at its startPose.
    {
        Machine m;
        ProbeSearch search;
        ProbeSearch::Request r = zRequest();
        r.dir = ProbeSearchDir::PosY;
        r.num_fine_searches = 0;
        if (!search.start(m, r) || !runUntilDone(search, m, disc, 1200))
        {
            return 92;
        }
        const float expected_y = disc.y + r.lateral_offset;
        const float expected_z = disc.z + r.approach_distance;
        if (!nearEqual(m.y(), expected_y, 0.8f) || !nearEqual(m.x(), disc.x, 0.8f) ||
            !nearEqual(m.z(), expected_z, 0.8f))
        {
            return 93;
        }
    }

    return 0;
}
