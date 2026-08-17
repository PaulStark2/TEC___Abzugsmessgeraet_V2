// Milestone 1: SafetyLimits, MachineConfig, TestConfiguration, CalibrationSet.
#include "CalibrationSet.hpp"
#include "MachineConfig.hpp"
#include "SafetyLimits.hpp"
#include "TestConfiguration.hpp"

#include <cmath>

static bool nearEqual(float a, float b, float eps = 1e-5f)
{
    return std::fabs(a - b) <= eps;
}

int main()
{
    // Default MachineConfig is valid and below SafetyLimits.
    {
        const MachineConfig cfg{};
        if (validateMachineConfig(cfg) != ConfigValidationError::Ok)
        {
            return 1;
        }
        if (cfg.motion.v_approach > SafetyLimits::kMaxVelocity)
        {
            return 2;
        }
        if (cfg.collision.approach_N > SafetyLimits::kMaxFzAbs_N)
        {
            return 3;
        }
        if (cfg.collisionThreshold(CollisionContext::Measure) != cfg.collision.measure_N)
        {
            return 4;
        }
    }

    // Reject velocity above safety; dest unchanged (no silent clamp).
    {
        MachineConfig dest{};
        const float original = dest.motion.v_approach;
        MachineConfig incoming = dest;
        incoming.motion.v_approach = SafetyLimits::kMaxVelocity + 1.0f;
        ConfigValidationError error = ConfigValidationError::Ok;
        if (tryApplyMachineConfig(dest, incoming, error))
        {
            return 10;
        }
        if (error != ConfigValidationError::VelocityExceedsSafety)
        {
            return 11;
        }
        if (!nearEqual(dest.motion.v_approach, original))
        {
            return 12;
        }
    }

    // Reject non-positive velocity.
    {
        MachineConfig incoming{};
        incoming.motion.v_slow = 0.0f;
        if (validateMachineConfig(incoming) != ConfigValidationError::VelocityNotPositive)
        {
            return 13;
        }
    }

    // Reject force threshold above ±50 N safety.
    {
        MachineConfig incoming{};
        incoming.collision.contact_N = SafetyLimits::kMaxFzAbs_N + 0.1f;
        if (validateMachineConfig(incoming) != ConfigValidationError::ForceExceedsSafety)
        {
            return 14;
        }
    }

    // Reject negative spreader time; keep previous.
    {
        MachineConfig dest{};
        const float original = dest.spreader.extend_time_s;
        MachineConfig incoming = dest;
        incoming.spreader.extend_time_s = 0.0f;
        ConfigValidationError error = ConfigValidationError::Ok;
        if (tryApplyMachineConfig(dest, incoming, error))
        {
            return 15;
        }
        if (error != ConfigValidationError::TimeNotPositive)
        {
            return 16;
        }
        if (!nearEqual(dest.spreader.extend_time_s, original))
        {
            return 17;
        }
    }

    // Accept a valid replacement.
    {
        MachineConfig dest{};
        MachineConfig incoming = dest;
        incoming.motion.v_measure = 1.5f;
        incoming.spreader.retract_time_s = 0.8f;
        ConfigValidationError error = ConfigValidationError::Ok;
        if (!tryApplyMachineConfig(dest, incoming, error))
        {
            return 18;
        }
        if (error != ConfigValidationError::Ok)
        {
            return 19;
        }
        if (!nearEqual(dest.motion.v_measure, 1.5f) || !nearEqual(dest.spreader.retract_time_s, 0.8f))
        {
            return 20;
        }
    }

    // Acceleration 0 (constant velocity) is valid; negative is not.
    {
        MachineConfig cfg{};
        cfg.motion.acceleration = 0.0f;
        if (validateMachineConfig(cfg) != ConfigValidationError::Ok)
        {
            return 21;
        }
        cfg.motion.acceleration = -1.0f;
        if (validateMachineConfig(cfg) != ConfigValidationError::AccelerationNegative)
        {
            return 22;
        }
        cfg.motion.acceleration = SafetyLimits::kMaxAcceleration + 1.0f;
        if (validateMachineConfig(cfg) != ConfigValidationError::AccelerationExceedsSafety)
        {
            return 23;
        }
    }

    // Default TestConfiguration is valid.
    {
        const TestConfiguration cfg{};
        if (validateTestConfiguration(cfg) != ConfigValidationError::Ok)
        {
            return 30;
        }
        if (cfg.mode != TestMode::Test)
        {
            return 31;
        }
    }

    // Reject target force above safety; dest unchanged.
    {
        TestConfiguration dest{};
        const float original = dest.target_force_N;
        TestConfiguration incoming = dest;
        incoming.target_force_N = SafetyLimits::kMaxFzAbs_N + 1.0f;
        ConfigValidationError error = ConfigValidationError::Ok;
        if (tryApplyTestConfiguration(dest, incoming, error))
        {
            return 32;
        }
        if (error != ConfigValidationError::TargetForceInvalid)
        {
            return 33;
        }
        if (!nearEqual(dest.target_force_N, original))
        {
            return 34;
        }
    }

    // Reject zero iterations.
    {
        TestConfiguration incoming{};
        incoming.iterations = 0;
        if (validateTestConfiguration(incoming) != ConfigValidationError::IterationsZero)
        {
            return 35;
        }
    }

    // Accept aging mode with valid force.
    {
        TestConfiguration dest{};
        TestConfiguration incoming{};
        incoming.mode = TestMode::Aging;
        incoming.target_force_N = 8.0f;
        incoming.iterations = 20;
        incoming.active_point_index = 3;
        ConfigValidationError error = ConfigValidationError::Ok;
        if (!tryApplyTestConfiguration(dest, incoming, error))
        {
            return 36;
        }
        if (dest.mode != TestMode::Aging || dest.iterations != 20 || dest.active_point_index != 3)
        {
            return 37;
        }
    }

    // CalibrationSet: plate origin overwrite + valid flag.
    {
        CalibrationSet set{};
        if (set.plate_origin_valid || set.probe_center_valid)
        {
            return 40;
        }
        if (!nearEqual(set.tool_offset.x, 0.0f) || !nearEqual(set.tool_offset.y, 0.0f) ||
            !nearEqual(set.tool_offset.z, 0.0f))
        {
            return 41;
        }
        setPlateOrigin(set, TestPoint{10.0f, 20.0f, 5.0f});
        if (!set.plate_origin_valid || !nearEqual(set.plate_origin.x, 10.0f) ||
            !nearEqual(set.plate_origin.y, 20.0f) || !nearEqual(set.plate_origin.z, 5.0f))
        {
            return 42;
        }
        setPlateOrigin(set, TestPoint{1.0f, 2.0f, 3.0f});
        if (!nearEqual(set.plate_origin.x, 1.0f))
        {
            return 43;
        }
        setProbeCenter(set, TestPoint{4.0f, 5.0f, 6.0f});
        if (!set.probe_center_valid || !nearEqual(set.probe_center.z, 6.0f))
        {
            return 44;
        }
        set.tool_offset = TestPoint{0.5f, -0.25f, 1.0f};
        if (!nearEqual(set.tool_offset.x, 0.5f) || !nearEqual(set.tool_offset.z, 1.0f))
        {
            return 45;
        }
    }

    // SafetyLimits are compile-time constants (sensor range).
    {
        if (SafetyLimits::kMaxFxAbs_N != 50.0f || SafetyLimits::kMaxFyAbs_N != 50.0f ||
            SafetyLimits::kMaxFzAbs_N != 50.0f)
        {
            return 50;
        }
    }

    return 0;
}
