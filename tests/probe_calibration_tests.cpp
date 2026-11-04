#include "ForceSample.hpp"
#include "HostInputs.hpp"
#include "Machine.hpp"
#include "ProbeCalibration.hpp"
#include "TestPoint.hpp"

#include <cmath>

static bool nearEqual(float a, float b, float eps = 0.8f)
{
    return std::fabs(a - b) <= eps;
}

// Synthetic bore: rising |F| when past wall/floor thresholds around center.
static HostInputs withBoreForce(const Machine& machine, const TestPoint& center, float radius,
                                float wall_N, float floor_N)
{
    HostInputs in{};
    const float dx = machine.x() - center.x;
    const float dy = machine.y() - center.y;
    const float r = std::hypot(dx, dy);
    if (r > radius)
    {
        const float pen = r - radius;
        const float k = 20.0f;
        in.force.fx = k * pen * (dx / r);
        in.force.fy = k * pen * (dy / r);
        if (std::fabs(in.force.fx) < wall_N && std::fabs(in.force.fy) < wall_N)
        {
            // ensure threshold crossed when clearly outside
            in.force.fx = (dx >= 0.0f ? 1.0f : -1.0f) * (wall_N + 0.2f);
            in.force.fy = (dy >= 0.0f ? 1.0f : -1.0f) * (wall_N + 0.2f);
        }
    }
    if (machine.z() < center.z - 0.05f)
    {
        in.force.fz = floor_N + 0.5f;
    }
    return withFreshForce(in);
}

static bool runCalib(ProbeCalibration& calib, Machine& machine, const TestPoint& center, int max_ticks)
{
    const float wall = machine.machineConfig().probe.wall_force_N;
    const float floor = machine.machineConfig().probe.floor_force_N;
    const float radius = 3.0f;
    for (int i = 0; i < max_ticks && !calib.isComplete() && !calib.isError(); ++i)
    {
        machine.setHostInputs(withBoreForce(machine, center, radius, wall, floor));
        machine.tick(0.05f);
        calib.tick(0.05f, machine);
    }
    return calib.isComplete() && !calib.isError();
}

int main()
{
    const TestPoint bore{40.0f, 40.0f, 5.0f};

    // Install stores probe_center and zero tool_offset.
    {
        Machine m;
        homeMachine(m);
        m.moveTo(bore.x, bore.y, bore.z + 8.0f);
        for (int i = 0; i < 400 && m.motionState() != MotionState::AtTarget; ++i)
        {
            m.setHostInputs(withFreshForce(HostInputs{}));
            m.tick(0.05f);
        }
        ProbeCalibration calib;
        if (!calib.start(m, TestPoint{m.x(), m.y(), m.z()}, ProbeCalibration::StorePolicy::Install))
        {
            return 1;
        }
        if (!runCalib(calib, m, bore, 8000))
        {
            return 2;
        }
        if (!m.calibrationSet().probe_center_valid)
        {
            return 3;
        }
        const TestPoint& c = m.calibrationSet().probe_center;
        if (!nearEqual(c.x, bore.x, 1.5f) || !nearEqual(c.y, bore.y, 1.5f) ||
            !nearEqual(c.z, bore.z, 1.5f))
        {
            return 4;
        }
        const TestPoint& to = m.calibrationSet().tool_offset;
        if (!nearEqual(to.x, 0.0f) || !nearEqual(to.y, 0.0f) || !nearEqual(to.z, 0.0f))
        {
            return 5;
        }
    }

    // OffsetOnly updates tool_offset only.
    {
        Machine m;
        homeMachine(m);
        m.setProbeCenter(bore);
        m.setToolOffset(TestPoint{0.0f, 0.0f, 0.0f});
        m.moveTo(bore.x + 0.5f, bore.y - 0.3f, bore.z + 8.0f);
        for (int i = 0; i < 400 && m.motionState() != MotionState::AtTarget; ++i)
        {
            m.setHostInputs(withFreshForce(HostInputs{}));
            m.tick(0.05f);
        }
        ProbeCalibration calib;
        if (!calib.start(m, TestPoint{m.x(), m.y(), m.z()}, ProbeCalibration::StorePolicy::OffsetOnly) ||
            !runCalib(calib, m, bore, 8000))
        {
            return 10;
        }
        if (!nearEqual(m.calibrationSet().probe_center.x, bore.x) ||
            !nearEqual(m.calibrationSet().probe_center.y, bore.y) ||
            !nearEqual(m.calibrationSet().probe_center.z, bore.z))
        {
            return 11;
        }
    }

    // OffsetOnly without valid center fails.
    {
        Machine m;
        homeMachine(m);
        ProbeCalibration calib;
        if (calib.start(m, TestPoint{0, 0, 10}, ProbeCalibration::StorePolicy::OffsetOnly))
        {
            return 20;
        }
    }

    return 0;
}
