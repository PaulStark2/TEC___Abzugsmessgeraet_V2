// Homing: DefineMcs vs Reference-only home(); restore; no auto-define.
// X homes at limitXMin (−seek); Y/Z at +Max. Home = (+R,−R,−R).
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

static void applyHomingLimitBits(Machine& m, HostInputs& in, float trip_at)
{
    // Y/Z: trip +Max when raw >= trip. X: trip Min when raw <= −trip.
    in.limitZMax = m.homingState() == HomingState::HomingZ && m.rawZ() >= trip_at;
    in.limitYMax = m.homingState() == HomingState::HomingY && m.rawY() >= trip_at;
    in.limitXMin = m.homingState() == HomingState::HomingX && m.rawX() <= -trip_at;
    in.limitXMax = false;
    in.limitYMin = false;
    in.limitZMin = false;
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
        applyHomingLimitBits(m, in, trip_at);
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

    // A — home() without MCS is rejected; DefineMcs home = (+R,−R,−R).
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
        if (!nearEqual(home.x, kR, 1e-3f) || !nearEqual(home.y, -kR, 1e-3f) ||
            !nearEqual(home.z, -kR, 1e-3f))
        {
            std::fprintf(stderr, "A home=(%.4f,%.4f,%.4f) expected ~(%.4f,%.4f,%.4f)\n", home.x,
                         home.y, home.z, kR, -kR, -kR);
            return 5;
        }
        // X switch at raw ≈ −trip; Y/Z at +trip.
        if (std::fabs(m.referenceOffsetX() - (-kTrip)) > 0.5f ||
            std::fabs(m.referenceOffsetY() - kTrip) > 0.5f ||
            std::fabs(m.referenceOffsetZ() - kTrip) > 0.5f)
        {
            std::fprintf(stderr, "A offset=(%.4f,%.4f,%.4f) expected ~(%.4f,%.4f,%.4f)\n",
                         m.referenceOffsetX(), m.referenceOffsetY(), m.referenceOffsetZ(), -kTrip,
                         kTrip, kTrip);
            return 5;
        }
        if (m.calibrationSet().plate_origin_valid || m.calibrationSet().probe_center_valid)
        {
            return 6;
        }
    }

    // B — no raw teleport at switch latch during DefineMcs (Z still +Max).
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
            applyHomingLimitBits(m, in, kTrip);
            in.forceSampleSequence++;
            m.setHostInputs(in);
            m.tick(kDt);
        }
        if (!m.isHomed() || !m.machineCoordinateSystemValid())
        {
            return 16;
        }
        const TestPoint home = m.calibrationSet().home_offset;
        if (!nearEqual(home.z, -kR, 1e-3f) || !nearEqual(m.z(), -kR, 0.2f))
        {
            std::fprintf(stderr, "B home.z=%.4f machine.z=%.4f expected ~%.4f\n", home.z, m.z(),
                         -kR);
            return 17;
        }
        if (!nearEqual(home.x, kR, 1e-3f))
        {
            std::fprintf(stderr, "B home.x=%.4f expected ~%.4f\n", home.x, kR);
            return 18;
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
            applyHomingLimitBits(m, in, kTrip);
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
    }

    // G — changing R updates derived home without re-defining MCS.
    {
        Machine m;
        if (!runHomingToHomed(m, true, kTrip, kDt))
        {
            return 70;
        }
        m.setPlateOrigin(TestPoint{-10.0f, -11.0f, -12.0f});
        MachineConfig cfg = m.machineConfig();
        cfg.calibration.home_offset = TestPoint{5.0f, 5.0f, 5.0f};
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!m.trySetMachineConfig(cfg, err))
        {
            return 71;
        }
        if (!m.machineCoordinateSystemValid())
        {
            return 72;
        }
        const TestPoint home = m.calibrationSet().home_offset;
        if (!nearEqual(home.x, 5.0f) || !nearEqual(home.y, -5.0f) || !nearEqual(home.z, -5.0f))
        {
            std::fprintf(stderr, "G home=(%.3f,%.3f,%.3f) expected (5,-5,-5)\n", home.x, home.y,
                         home.z);
            return 73;
        }
        if (!m.calibrationSet().plate_origin_valid)
        {
            return 74;
        }
        if (m.isHomed())
        {
            std::fprintf(stderr, "G isHomed should clear when TCP is not at new home\n");
            return 75;
        }
        Machine m2;
        if (!m2.restoreMachineCoordinateSystem(TestPoint{99.0f, 99.0f, 99.0f}))
        {
            return 76;
        }
        if (!nearEqual(m2.calibrationSet().home_offset.x, kR) ||
            !nearEqual(m2.calibrationSet().home_offset.y, -kR) ||
            !nearEqual(m2.calibrationSet().home_offset.z, -kR))
        {
            return 77;
        }
    }

    // H — Reference after jog: session trust enables fast approach (isHomed may be false).
    {
        Machine m;
        if (!runHomingToHomed(m, true, kTrip, kDt))
        {
            return 80;
        }
        MachineConfig cfg = m.machineConfig();
        cfg.motion.v_homing = 5.0f;
        cfg.motion.v_homing_fast = 50.0f;
        cfg.motion.homing_approach_distance = 50.0f;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!m.trySetMachineConfig(cfg, err))
        {
            return 81;
        }
        const TestPoint home = m.calibrationSet().home_offset;
        HostInputs in = makeSafeInputs();
        m.setHostInputs(in);
        // Jog away: clears isHomed; session trust remains.
        m.moveTo(home.x, home.y, home.z - 150.0f);
        for (int step = 0; step < 20000 && m.motionState() != MotionState::AtTarget; ++step)
        {
            in.forceSampleSequence++;
            m.setHostInputs(in);
            m.tick(kDt);
        }
        if (m.isHomed())
        {
            std::fprintf(stderr, "H expected !isHomed after jog\n");
            return 82;
        }
        const float approach_z = home.z - 50.0f;
        if (!m.home())
        {
            return 83;
        }
        int steps_to_approach = -1;
        for (int step = 0; step < 20000 && m.homingState() == HomingState::HomingZ; ++step)
        {
            applyHomingLimitBits(m, in, kTrip);
            in.forceSampleSequence++;
            m.setHostInputs(in);
            m.tick(kDt);
            if (steps_to_approach < 0 && m.z() >= approach_z - 0.5f)
            {
                steps_to_approach = step;
                break;
            }
        }
        // 100 mm at 50 mm/s ≈ 100 steps; slow seek alone would need ~1000.
        if (steps_to_approach < 0 || steps_to_approach > 250)
        {
            std::fprintf(stderr, "H fast approach steps=%d (expected <=250)\n", steps_to_approach);
            return 84;
        }
        for (int step = 0; step < 20000 && m.homingState() != HomingState::Homed; ++step)
        {
            applyHomingLimitBits(m, in, kTrip);
            in.forceSampleSequence++;
            m.setHostInputs(in);
            m.tick(kDt);
        }
        if (!m.isHomed())
        {
            return 85;
        }
    }

    // I — after invalidateHoming (Soft-STOP path): session trust keeps fast approach.
    {
        Machine m;
        if (!runHomingToHomed(m, true, kTrip, kDt))
        {
            return 90;
        }
        MachineConfig cfg = m.machineConfig();
        cfg.motion.v_homing = 5.0f;
        cfg.motion.v_homing_fast = 50.0f;
        cfg.motion.homing_approach_distance = 50.0f;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!m.trySetMachineConfig(cfg, err))
        {
            return 91;
        }
        m.shiftRawPose(0.0f, 0.0f, -150.0f);
        m.invalidateHoming();
        if (m.homingState() == HomingState::Homed)
        {
            std::fprintf(stderr, "I expected HomingState cleared by invalidateHoming\n");
            return 92;
        }
        const float approach_z = m.calibrationSet().home_offset.z - 50.0f;
        HostInputs in = makeSafeInputs();
        m.setHostInputs(in);
        if (!m.home())
        {
            return 93;
        }
        int steps_to_approach = -1;
        for (int step = 0; step < 20000 && m.homingState() == HomingState::HomingZ; ++step)
        {
            applyHomingLimitBits(m, in, kTrip);
            in.forceSampleSequence++;
            m.setHostInputs(in);
            m.tick(kDt);
            if (steps_to_approach < 0 && m.z() >= approach_z - 0.5f)
            {
                steps_to_approach = step;
                break;
            }
        }
        if (steps_to_approach < 0 || steps_to_approach > 250)
        {
            std::fprintf(stderr, "I fast after invalidate steps=%d (expected <=250)\n",
                         steps_to_approach);
            return 94;
        }
        for (int step = 0; step < 20000 && m.homingState() != HomingState::Homed; ++step)
        {
            applyHomingLimitBits(m, in, kTrip);
            in.forceSampleSequence++;
            m.setHostInputs(in);
            m.tick(kDt);
        }
        if (!m.isHomed())
        {
            return 95;
        }
    }

    // J — DefineMcs while Homed: always blind seek (no fast approach).
    {
        Machine m;
        if (!runHomingToHomed(m, true, kTrip, kDt))
        {
            return 100;
        }
        MachineConfig cfg = m.machineConfig();
        cfg.motion.v_homing = 5.0f;
        cfg.motion.v_homing_fast = 50.0f;
        cfg.motion.homing_approach_distance = 50.0f;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!m.trySetMachineConfig(cfg, err))
        {
            return 101;
        }
        m.shiftRawPose(0.0f, 0.0f, -150.0f);
        const float approach_z = m.calibrationSet().home_offset.z - 50.0f;
        HostInputs in = makeSafeInputs();
        m.setHostInputs(in);
        if (!m.defineMachineCoordinateSystem())
        {
            return 102;
        }
        int steps_to_approach = -1;
        for (int step = 0; step < 20000 && m.homingState() == HomingState::HomingZ; ++step)
        {
            applyHomingLimitBits(m, in, kTrip);
            in.forceSampleSequence++;
            m.setHostInputs(in);
            m.tick(kDt);
            if (steps_to_approach < 0 && m.z() >= approach_z - 0.5f)
            {
                steps_to_approach = step;
                break;
            }
        }
        if (steps_to_approach < 500)
        {
            std::fprintf(stderr, "J DefineMcs blind steps=%d (expected >=500)\n", steps_to_approach);
            return 103;
        }
    }

    // K — config rejects non-positive fast speed / approach distance.
    {
        Machine m;
        MachineConfig cfg = m.machineConfig();
        ConfigValidationError err = ConfigValidationError::Ok;
        cfg.motion.v_homing_fast = 0.0f;
        if (m.trySetMachineConfig(cfg, err) || err != ConfigValidationError::VelocityNotPositive)
        {
            return 110;
        }
        cfg = m.machineConfig();
        cfg.motion.homing_approach_distance = 0.0f;
        if (m.trySetMachineConfig(cfg, err) || err != ConfigValidationError::BufferNegative)
        {
            return 111;
        }
    }

    // L — safety Fault clears session trust → next Reference home is blind.
    {
        Machine m;
        if (!runHomingToHomed(m, true, kTrip, kDt))
        {
            return 120;
        }
        MachineConfig cfg = m.machineConfig();
        cfg.motion.v_homing = 5.0f;
        cfg.motion.v_homing_fast = 50.0f;
        cfg.motion.homing_approach_distance = 50.0f;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!m.trySetMachineConfig(cfg, err))
        {
            return 121;
        }
        m.shiftRawPose(0.0f, 0.0f, -150.0f);
        HostInputs in = makeSafeInputs();
        in.e_stop = true;
        in.forceSampleSequence++;
        m.setHostInputs(in);
        m.tick(kDt);
        if (m.machineState() != MachineState::Fault)
        {
            std::fprintf(stderr, "L expected Fault after e_stop\n");
            return 122;
        }
        in.e_stop = false;
        in.forceSampleSequence++;
        m.setHostInputs(in);
        m.clearFault();
        if (m.machineState() != MachineState::Idle)
        {
            std::fprintf(stderr, "L expected Idle after clearFault\n");
            return 123;
        }
        const float approach_z = m.calibrationSet().home_offset.z - 50.0f;
        if (!m.home())
        {
            return 124;
        }
        int steps_to_approach = -1;
        for (int step = 0; step < 20000 && m.homingState() == HomingState::HomingZ; ++step)
        {
            applyHomingLimitBits(m, in, kTrip);
            in.forceSampleSequence++;
            m.setHostInputs(in);
            m.tick(kDt);
            if (steps_to_approach < 0 && m.z() >= approach_z - 0.5f)
            {
                steps_to_approach = step;
                break;
            }
        }
        if (steps_to_approach < 500)
        {
            std::fprintf(stderr, "L blind after Fault steps=%d (expected >=500)\n",
                         steps_to_approach);
            return 125;
        }
    }

    std::printf("homing_tests: ok\n");
    return 0;
}
