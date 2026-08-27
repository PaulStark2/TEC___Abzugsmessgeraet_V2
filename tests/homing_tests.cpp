// Homing: DefineMcs vs Reference-only home(); restore; no auto-define.
// DefineMcs uses fast seek + creep flank find (bit only), then retract R.
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

static bool runHomingToHomed(Machine& m, bool define, float trip_at, float dt = 0.02f)
{
    HostInputs in = makeSafeInputs();
    m.setHostInputs(in);
    const bool started = define ? m.defineMachineCoordinateSystem() : m.home();
    if (!started)
    {
        return false;
    }
    for (int step = 0; step < 20000 && m.homingState() != HomingState::Homed; ++step)
    {
        // Only the +max bit — no plant plane coordinates (architecture: sensor only).
        in.limitZMax = m.homingState() == HomingState::HomingZ && m.rawZ() >= trip_at;
        in.limitXMax = m.homingState() == HomingState::HomingX && m.rawX() >= trip_at;
        in.limitYMax = m.homingState() == HomingState::HomingY && m.rawY() >= trip_at;
        in.forceSampleSequence++;
        m.setHostInputs(in);
        m.tick(dt);
        if (m.homingState() == HomingState::HomingError)
        {
            return false;
        }
    }
    return m.isHomed() && m.homingState() == HomingState::Homed;
}

int main()
{
    constexpr float kDt = 0.02f;
    constexpr float kR = 2.0f;
    constexpr float kTrip = 5.0f;

    // A — home() without MCS is rejected; DefineMcs establishes MCS near trip − R.
    {
        Machine m;
        m.setCurrentPose(1.0f, 1.0f, 1.0f);
        if (m.home())
        {
            std::fprintf(stderr, "A: home() must reject without MCS\n");
            return 1;
        }
        if (!runHomingToHomed(m, true, kTrip, kDt))
        {
            return 2;
        }
        if (!m.machineCoordinateSystemValid())
        {
            return 3;
        }
        const TestPoint home = m.calibrationSet().home_offset;
        if (!nearEqual(m.x(), home.x, 0.2f) || !nearEqual(m.y(), home.y, 0.2f) ||
            !nearEqual(m.z(), home.z, 0.2f))
        {
            return 4;
        }
        // Creep rising-edge residual ≪ one fast-seek step (v_homing·dt).
        const float ideal = kTrip - kR;
        const float creep_v = m.machineConfig().motion.v_homing * 0.1f;
        const float creep_step = ((creep_v > 0.2f) ? creep_v : 0.2f) * kDt + 1e-3f;
        // Midpoint of creep rising edge → residual ≤ ~½ creep step.
        const float tol = 0.5f * creep_step + 1e-3f;
        if (!nearEqual(home.x, ideal, tol) || !nearEqual(home.y, ideal, tol) ||
            !nearEqual(home.z, ideal, tol))
        {
            std::fprintf(stderr, "A home=(%.4f,%.4f,%.4f) expected ~%.4f (tol=%.4f)\n", home.x,
                         home.y, home.z, ideal, tol);
            return 5;
        }
        if (m.calibrationSet().plate_origin_valid || m.calibrationSet().probe_center_valid)
        {
            return 6;
        }
    }

    // B — no raw teleport at switch latch during DefineMcs.
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
        bool saw_limit = false;
        float raw_at_first_limit = -1.0f;
        for (int step = 0; step < 5000 && m.homingState() == HomingState::HomingZ; ++step)
        {
            const bool at_limit = m.rawZ() >= kTrip;
            in.limitZMax = at_limit;
            in.forceSampleSequence++;
            m.setHostInputs(in);
            const float raw_before = m.rawZ();
            m.tick(kDt);
            if (at_limit && !saw_limit)
            {
                // Raw must not teleport when the fast-seek limit is first seen.
                if (!nearEqual(m.rawZ(), raw_before, 0.05f))
                {
                    return 12;
                }
                raw_at_first_limit = m.rawZ();
                saw_limit = true;
            }
            if (saw_limit && m.homingState() != HomingState::HomingZ)
            {
                break;
            }
        }
        if (!saw_limit || raw_at_first_limit < 0.0f)
        {
            return 15;
        }
        for (int step = 0; step < 20000 && m.homingState() != HomingState::Homed; ++step)
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
        const float ideal = kTrip - kR;
        const float creep_v = cfg.motion.v_homing * 0.1f;
        const float creep_step = ((creep_v > 0.2f) ? creep_v : 0.2f) * kDt + 1e-3f;
        const TestPoint home = m.calibrationSet().home_offset;
        if (!nearEqual(home.z, ideal, creep_step))
        {
            std::fprintf(stderr, "B home.z=%.4f expected ~%.4f\n", home.z, ideal);
            return 17;
        }
    }

    // C — Reference home after MCS: same home; probe/plate/tool unchanged; step loss fixed.
    {
        Machine m;
        if (!runHomingToHomed(m, true, kTrip, kDt))
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

        m.shiftRawPose(3.0f, -2.0f, 4.0f);
        m.invalidateHoming();

        HostInputs in = makeSafeInputs();
        m.setHostInputs(in);
        if (!m.home())
        {
            return 21;
        }
        for (int step = 0; step < 20000 && m.homingState() != HomingState::Homed; ++step)
        {
            in.limitZMax = m.homingState() == HomingState::HomingZ && m.rawZ() >= kTrip;
            in.limitXMax = m.homingState() == HomingState::HomingX && m.rawX() >= kTrip;
            in.limitYMax = m.homingState() == HomingState::HomingY && m.rawY() >= kTrip;
            in.forceSampleSequence++;
            m.setHostInputs(in);
            m.tick(kDt);
        }
        if (!m.isHomed())
        {
            return 22;
        }
        if (!nearEqual(m.calibrationSet().home_offset.x, home.x) ||
            !nearEqual(m.calibrationSet().home_offset.y, home.y) ||
            !nearEqual(m.calibrationSet().home_offset.z, home.z))
        {
            return 23;
        }
        if (!nearEqual(m.calibrationSet().plate_origin.x, plate.x) ||
            !nearEqual(m.calibrationSet().probe_center.x, probe.x) ||
            !nearEqual(m.calibrationSet().tool_offset.x, tool.x))
        {
            return 24;
        }
        if (!nearEqual(m.x(), home.x, 0.25f) || !nearEqual(m.y(), home.y, 0.25f) ||
            !nearEqual(m.z(), home.z, 0.25f))
        {
            return 25;
        }
    }

    // D — restore MCS without motion.
    {
        Machine m;
        if (!runHomingToHomed(m, true, kTrip, kDt))
        {
            return 30;
        }
        const TestPoint home = m.calibrationSet().home_offset;
        Machine m2;
        m2.setCurrentPose(0.0f, 0.0f, 0.0f);
        if (!m2.restoreMachineCoordinateSystem(home))
        {
            return 31;
        }
        if (!m2.machineCoordinateSystemValid())
        {
            return 32;
        }
        if (!nearEqual(m2.calibrationSet().home_offset.x, home.x) ||
            !nearEqual(m2.calibrationSet().home_offset.y, home.y) ||
            !nearEqual(m2.calibrationSet().home_offset.z, home.z))
        {
            return 33;
        }
        if (m2.isHomed())
        {
            return 34;
        }
    }

    // D — repeated Reference home → same home point.
    {
        Machine m;
        if (!runHomingToHomed(m, true, kTrip, kDt))
        {
            return 40;
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
                return 41;
            }
            if (!runHomingToHomed(m, false, kTrip, kDt))
            {
                return 42;
            }
            if (!nearEqual(m.x(), home.x, 0.25f) || !nearEqual(m.y(), home.y, 0.25f) ||
                !nearEqual(m.z(), home.z, 0.25f))
            {
                return 43;
            }
        }
    }

    // E — moveTo after home uses machine frame relative to MCS home.
    {
        Machine m;
        if (!runHomingToHomed(m, true, kTrip, kDt))
        {
            return 50;
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
            return 51;
        }
    }

    // F — redefine MCS clears plate/probe; restore keeps them; home never defines.
    {
        Machine m;
        if (!runHomingToHomed(m, true, kTrip, kDt))
        {
            return 60;
        }
        m.setPlateOrigin(TestPoint{7.0f, 8.0f, 9.0f});
        m.setProbeCenter(TestPoint{4.0f, 5.0f, 6.0f});
        m.setToolOffset(TestPoint{1.0f, 1.0f, 1.0f});
        const TestPoint home = m.calibrationSet().home_offset;

        if (!runHomingToHomed(m, true, kTrip, kDt))
        {
            return 61;
        }
        if (m.calibrationSet().plate_origin_valid || m.calibrationSet().probe_center_valid)
        {
            return 62;
        }
        if (!nearEqual(m.calibrationSet().tool_offset.x, 0.0f))
        {
            return 63;
        }

        Machine m2;
        if (!m2.restoreMachineCoordinateSystem(home))
        {
            return 64;
        }
        if (!m2.machineCoordinateSystemValid())
        {
            return 65;
        }
        if (m2.isHomed())
        {
            return 66;
        }
        m2.setPlateOrigin(TestPoint{7.0f, 8.0f, 9.0f});
        m2.setProbeCenter(TestPoint{4.0f, 5.0f, 6.0f});
        if (!m2.calibrationSet().plate_origin_valid || !m2.calibrationSet().probe_center_valid)
        {
            return 67;
        }
        if (!runHomingToHomed(m2, false, kTrip, kDt))
        {
            return 68;
        }
        if (!nearEqual(m2.calibrationSet().home_offset.x, home.x) ||
            !nearEqual(m2.calibrationSet().plate_origin.x, 7.0f))
        {
            return 69;
        }
    }

    std::printf("homing_tests: ok\n");
    return 0;
}
