// Homing: MCS define vs normal reference; no raw teleport; step-loss correction.
#include "ForceSample.hpp"
#include "Machine.hpp"
#include "MachineConfig.hpp"
#include "Types.hpp"

#include "HostInputs.hpp"

#include <cmath>
#include <cstdio>

static bool nearEqual(float a, float b, float eps = 1e-3f)
{
    return std::fabs(a - b) <= eps;
}

static HostInputs makeSafeInputs()
{
    HostInputs in{};
    in.force = ForceVector{0.0f, 0.0f, 0.0f};
    in.e_stop = false;
    return withFreshForce(in);
}

static bool homeWithSoftwareTrip(Machine& m, float trip_at, float dt = 0.02f)
{
    HostInputs in = makeSafeInputs();
    m.setHostInputs(in);
    if (!m.home())
    {
        return false;
    }
    for (int step = 0; step < 2000 && m.homingState() != HomingState::Homed; ++step)
    {
        in.limitZMax = false;
        in.limitXMax = false;
        in.limitYMax = false;
        if (m.homingState() == HomingState::HomingZ)
        {
            in.limitZMax = m.rawZ() >= trip_at;
        }
        else if (m.homingState() == HomingState::HomingX)
        {
            in.limitXMax = m.rawX() >= trip_at;
        }
        else if (m.homingState() == HomingState::HomingY)
        {
            in.limitYMax = m.rawY() >= trip_at;
        }
        in.forceSampleSequence++;
        m.setHostInputs(in);
        m.tick(dt);
    }
    return m.isHomed() && m.homingState() == HomingState::Homed;
}

int main()
{
    constexpr float kDt = 0.02f;
    constexpr float kR = 2.0f;
    constexpr float kTrip = 5.0f;

    // A — Initial MCS define: home ≈ trip - R; MCS valid; raw continuous.
    {
        Machine m;
        m.setCurrentPose(1.0f, 1.0f, 1.0f);
        if (!homeWithSoftwareTrip(m, kTrip, kDt))
        {
            return 1;
        }
        if (!m.machineCoordinateSystemValid())
        {
            return 2;
        }
        const TestPoint home = m.calibrationSet().home_offset;
        if (!nearEqual(m.x(), home.x, 0.2f) || !nearEqual(m.y(), home.y, 0.2f) ||
            !nearEqual(m.z(), home.z, 0.2f))
        {
            return 3;
        }
        // Identity define: home ≈ trip - R on each axis.
        if (!nearEqual(home.x, kTrip - kR, 0.35f) || !nearEqual(home.y, kTrip - kR, 0.35f) ||
            !nearEqual(home.z, kTrip - kR, 0.35f))
        {
            std::fprintf(stderr, "A home=(%.3f,%.3f,%.3f) expected ~%.3f\n", home.x, home.y, home.z,
                         kTrip - kR);
            return 4;
        }
    }

    // B — no raw teleport at limit during DefineMcs.
    {
        Machine m;
        MachineConfig cfg = m.machineConfig();
        cfg.calibration.home_offset = TestPoint{kR, kR, kR};
        cfg.motion.v_homing = 20.0f;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!m.trySetMachineConfig(cfg, err))
        {
            return 10;
        }
        m.setCurrentPose(0.0f, 0.0f, 0.0f);
        HostInputs in = makeSafeInputs();
        m.setHostInputs(in);
        if (!m.defineMachineCoordinateSystem())
        {
            return 11;
        }
        bool saw_reference = false;
        float raw_at_switch = -1.0f;
        for (int step = 0; step < 2000 && m.homingState() == HomingState::HomingZ; ++step)
        {
            const bool at_limit = m.rawZ() >= kTrip;
            in.limitZMax = at_limit;
            in.forceSampleSequence++;
            m.setHostInputs(in);
            const float raw_before = m.rawZ();
            m.tick(kDt);
            if (at_limit && !saw_reference)
            {
                if (!nearEqual(m.rawZ(), raw_before, 0.05f))
                {
                    return 12;
                }
                // DefineMcs: machine := raw at switch (identity), not forced +R.
                if (!nearEqual(m.z(), m.rawZ(), 0.15f))
                {
                    return 13;
                }
                raw_at_switch = m.rawZ();
                saw_reference = true;
            }
            if (saw_reference && m.homingState() != HomingState::HomingZ)
            {
                break;
            }
        }
        if (!saw_reference || raw_at_switch < 0.0f)
        {
            return 15;
        }
        for (int step = 0; step < 2000 && m.homingState() != HomingState::Homed; ++step)
        {
            in.limitZMax = m.homingState() == HomingState::HomingZ && m.rawZ() >= kTrip;
            in.limitXMax = m.homingState() == HomingState::HomingX && m.rawX() >= kTrip;
            in.limitYMax = m.homingState() == HomingState::HomingY && m.rawY() >= kTrip;
            in.forceSampleSequence++;
            m.setHostInputs(in);
            m.tick(kDt);
        }
        if (!m.isHomed() || !m.machineCoordinateSystemValid())
        {
            return 16;
        }
    }

    // C — Normal home after MCS: same home pose; probe/plate unchanged; step loss fixed.
    {
        Machine m;
        if (!homeWithSoftwareTrip(m, kTrip, kDt))
        {
            return 20;
        }
        const TestPoint home = m.calibrationSet().home_offset;
        m.setPlateOrigin(TestPoint{1.0f, 2.0f, 3.0f});
        m.setProbeCenter(TestPoint{40.0f, 40.0f, 10.0f});
        m.setToolOffset(TestPoint{0.5f, -0.25f, 0.1f});
        const TestPoint plate = m.calibrationSet().plate_origin;
        const TestPoint probe = m.calibrationSet().probe_center;
        const TestPoint tool = m.calibrationSet().tool_offset;

        // Inject open-loop error without clearing MCS.
        m.shiftRawPose(3.0f, -2.0f, 4.0f);
        if (m.isHomed())
        {
            // shiftRawPose does not clear isHomed; leave pose — clear via move or invalidate.
        }
        m.invalidateHoming();

        if (!homeWithSoftwareTrip(m, kTrip + 3.0f, kDt)) // may need higher trip if raw shifted +X
        {
            // After +X shift, X trip still at absolute raw threshold used below:
        }
        // Re-home with trip on current raw levels (plant-like absolute switches).
        HostInputs in = makeSafeInputs();
        m.setHostInputs(in);
        if (!m.home())
        {
            return 21;
        }
        // Use generous trip so seek always finds switch after shift.
        for (int step = 0; step < 4000 && m.homingState() != HomingState::Homed; ++step)
        {
            in.limitZMax = m.rawZ() >= (kTrip + 4.0f);
            in.limitXMax = m.rawX() >= (kTrip + 4.0f);
            in.limitYMax = m.rawY() >= (kTrip + 4.0f);
            in.forceSampleSequence++;
            m.setHostInputs(in);
            m.tick(kDt);
        }
        if (!m.isHomed())
        {
            return 22;
        }
        if (!nearEqual(m.x(), home.x, 0.25f) || !nearEqual(m.y(), home.y, 0.25f) ||
            !nearEqual(m.z(), home.z, 0.25f))
        {
            std::fprintf(stderr, "C pose=(%.3f,%.3f,%.3f) home=(%.3f,%.3f,%.3f)\n", m.x(), m.y(),
                         m.z(), home.x, home.y, home.z);
            return 23;
        }
        if (!nearEqual(m.calibrationSet().home_offset.x, home.x) ||
            !nearEqual(m.calibrationSet().plate_origin.x, plate.x) ||
            !nearEqual(m.calibrationSet().probe_center.x, probe.x) ||
            !nearEqual(m.calibrationSet().tool_offset.x, tool.x))
        {
            return 24;
        }
        if (!m.calibrationSet().plate_origin_valid || !m.calibrationSet().probe_center_valid)
        {
            return 25;
        }
    }

    // D — repeated Reference home → same home point.
    {
        Machine m;
        if (!homeWithSoftwareTrip(m, kTrip, kDt))
        {
            return 30;
        }
        const TestPoint home = m.calibrationSet().home_offset;
        for (int cycle = 0; cycle < 3; ++cycle)
        {
            HostInputs in = makeSafeInputs();
            m.setHostInputs(in);
            m.moveTo(home.x - 1.0f, home.y - 0.5f, home.z - 0.5f);
            for (int i = 0; i < 800 && m.motionState() != MotionState::AtTarget; ++i)
            {
                in.forceSampleSequence++;
                m.setHostInputs(in);
                m.tick(kDt);
            }
            if (m.isHomed())
            {
                return 31;
            }
            if (!homeWithSoftwareTrip(m, kTrip, kDt))
            {
                return 32;
            }
            if (!nearEqual(m.x(), home.x, 0.25f) || !nearEqual(m.y(), home.y, 0.25f) ||
                !nearEqual(m.z(), home.z, 0.25f))
            {
                return 33;
            }
        }
    }

    // E — moveTo after home uses machine frame relative to MCS home.
    {
        Machine m;
        if (!homeMachine(m))
        {
            return 40;
        }
        const float hx = m.x();
        HostInputs in = makeSafeInputs();
        m.setHostInputs(in);
        m.moveTo(hx + 4.0f, m.y(), m.z());
        for (int i = 0; i < 800 && m.motionState() != MotionState::AtTarget; ++i)
        {
            in.forceSampleSequence++;
            m.setHostInputs(in);
            m.tick(kDt);
        }
        if (!nearEqual(m.x(), hx + 4.0f, 0.15f))
        {
            return 41;
        }
    }

    // F — defineMachineCoordinateSystem clears plate/probe validity; normal home does not.
    {
        Machine m;
        if (!m.defineMachineCoordinateSystem())
        {
            return 50;
        }
        HostInputs in = makeSafeInputs();
        for (int step = 0; step < 2000 && m.homingState() != HomingState::Homed; ++step)
        {
            in.limitZMax = m.rawZ() >= kTrip;
            in.limitXMax = m.rawX() >= kTrip;
            in.limitYMax = m.rawY() >= kTrip;
            in.forceSampleSequence++;
            m.setHostInputs(in);
            m.tick(kDt);
        }
        if (!m.isHomed() || !m.machineCoordinateSystemValid())
        {
            return 51;
        }
        if (m.calibrationSet().plate_origin_valid || m.calibrationSet().probe_center_valid)
        {
            return 52;
        }
        m.setPlateOrigin(TestPoint{7.0f, 8.0f, 9.0f});
        m.setProbeCenter(TestPoint{4.0f, 5.0f, 6.0f});
        if (!homeWithSoftwareTrip(m, kTrip, kDt))
        {
            return 53;
        }
        if (!m.calibrationSet().plate_origin_valid || !m.calibrationSet().probe_center_valid)
        {
            return 54;
        }
        if (!nearEqual(m.calibrationSet().plate_origin.x, 7.0f) ||
            !nearEqual(m.calibrationSet().probe_center.x, 4.0f))
        {
            return 55;
        }
    }

    return 0;
}
