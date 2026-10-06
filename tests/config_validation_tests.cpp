// Milestone 1: SafetyLimits, MachineConfig, TestConfiguration, CalibrationSet.
#include "CalibrationSet.hpp"
#include "Machine.hpp"
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
        if (cfg.motion.v_approach_testpoints > SafetyLimits::kMaxVelocity)
        {
            return 2;
        }
        if (cfg.collision.approach_N > SafetyLimits::kMaxFzAbs_N)
        {
            return 3;
        }
        if (!nearEqual(cfg.probe.measuring_head_diameter, 8.0f) ||
            !nearEqual(cfg.probe.measuring_head_thickness, 3.0f) ||
            !nearEqual(cfg.probe.tool_diameter, 2.5f) ||
            !nearEqual(cfg.probe.tool_cylindrical_height, 15.0f) ||
            !nearEqual(cfg.probe.xy_search_margin, 2.0f) ||
            !nearEqual(cfg.force.insert_search_distance, 10.0f) ||
            !nearEqual(cfg.probe.search_distance, 10.0f) || !nearEqual(cfg.probe.search_depth, 5.0f))
        {
            return 5;
        }
        if (cfg.collisionThreshold(CollisionContext::Measure) != cfg.collision.measure_N)
        {
            return 4;
        }
    }

    // Reject velocity above safety; dest unchanged (no silent clamp).
    {
        MachineConfig dest{};
        const float original = dest.motion.v_approach_testpoints;
        MachineConfig incoming = dest;
        incoming.motion.v_approach_testpoints = SafetyLimits::kMaxVelocity + 1.0f;
        ConfigValidationError error = ConfigValidationError::Ok;
        if (tryApplyMachineConfig(dest, incoming, error))
        {
            return 10;
        }
        if (error != ConfigValidationError::VelocityExceedsSafety)
        {
            return 11;
        }
        if (!nearEqual(dest.motion.v_approach_testpoints, original))
        {
            return 12;
        }
    }

    // Reject non-positive velocity.
    {
        MachineConfig incoming{};
        incoming.motion.v_fine_testpoints = 0.0f;
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
        incoming.motion.v_unclamp = 1.5f;
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
        if (!nearEqual(dest.motion.v_unclamp, 1.5f) || !nearEqual(dest.spreader.retract_time_s, 0.8f))
        {
            return 20;
        }
    }

    // Acceleration 0 (constant velocity) is valid; negative is not.
    {
        MachineConfig cfg{};
        cfg.motion.a_testpoints = 0.0f;
        if (validateMachineConfig(cfg) != ConfigValidationError::Ok)
        {
            return 21;
        }
        cfg.motion.a_testpoints = -1.0f;
        if (validateMachineConfig(cfg) != ConfigValidationError::AccelerationNegative)
        {
            return 22;
        }
        cfg.motion.a_testpoints = SafetyLimits::kMaxAcceleration + 1.0f;
        if (validateMachineConfig(cfg) != ConfigValidationError::AccelerationExceedsSafety)
        {
            return 23;
        }
    }

    // Corner blend inconsistent when R > 0 but a == 0, or R < v^2/(2a).
    {
        MachineConfig cfg{};
        cfg.motion.a_testpoints = 0.0f;
        cfg.motion.corner_testpoints = 1.0f;
        if (validateMachineConfig(cfg) != ConfigValidationError::CornerInconsistent)
        {
            return 24;
        }
        cfg.motion.a_testpoints = 10.0f;
        cfg.motion.v_approach_testpoints = 10.0f;
        cfg.motion.corner_testpoints = 1.0f; // need >= 5
        if (validateMachineConfig(cfg) != ConfigValidationError::CornerInconsistent)
        {
            return 25;
        }
        cfg.motion.corner_testpoints = 5.0f;
        if (validateMachineConfig(cfg) != ConfigValidationError::Ok)
        {
            return 26;
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

    // Force sample timeout must stay inside SafetyLimits.
    {
        MachineConfig incoming{};
        incoming.force.sample_timeout_s = 0.0f;
        if (validateMachineConfig(incoming) != ConfigValidationError::TimeNotPositive)
        {
            return 46;
        }
        incoming.force.sample_timeout_s = SafetyLimits::kMaxForceSampleTimeout_s + 0.1f;
        if (validateMachineConfig(incoming) != ConfigValidationError::TimeExceedsSafety)
        {
            return 47;
        }
    }

    // SafetyLimits are the 500 N typo brake. The sensor range is a parameter.
    {
        if (SafetyLimits::kMaxFxAbs_N != 500.0f || SafetyLimits::kMaxFyAbs_N != 500.0f ||
            SafetyLimits::kMaxFzAbs_N != 500.0f)
        {
            return 50;
        }
        MachineConfig defaults{};
        if (defaults.force.range_N != 30.0f || defaults.force.hard_limit_N != 30.0f ||
            defaults.collision.measure_N != 30.0f)
        {
            return 51;
        }
        MachineConfig hard{};
        hard.force.hard_limit_N = 40.0f;
        if (validateMachineConfig(hard) != ConfigValidationError::ForceExceedsSafety)
        {
            return 52;
        }
        MachineConfig raised{};
        raised.force.range_N = 100.0f;
        raised.force.hard_limit_N = 80.0f;
        raised.collision.retract_N = 90.0f;
        raised.collision.contact_N = 90.0f;
        raised.collision.measure_N = 90.0f;
        if (validateMachineConfig(raised) != ConfigValidationError::Ok)
        {
            return 53;
        }
        raised.force.range_N = 501.0f;
        raised.force.hard_limit_N = 80.0f;
        if (validateMachineConfig(raised) != ConfigValidationError::ForceExceedsSafety)
        {
            return 54;
        }
        Machine machine;
        TestConfiguration run{};
        run.target_force_N = 40.0f;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (machine.trySetTestConfiguration(run, err) || err != ConfigValidationError::TargetForceInvalid)
        {
            return 55;
        }
        MachineConfig wide = machine.machineConfig();
        wide.force.range_N = 50.0f;
        wide.force.hard_limit_N = 50.0f;
        if (!machine.trySetMachineConfig(wide, err))
        {
            return 56;
        }
        if (!machine.trySetTestConfiguration(run, err))
        {
            return 57;
        }
    }

    // Probe XY search margin must stay below the probe radius (inner stop > 0).
    {
        MachineConfig incoming{};
        incoming.probe.xy_search_margin = 4.0f;
        if (validateMachineConfig(incoming) != ConfigValidationError::BufferNegative)
        {
            return 51;
        }
        incoming.probe.xy_search_margin = 2.0f;
        incoming.probe.tool_cylindrical_height = 0.0f;
        if (validateMachineConfig(incoming) != ConfigValidationError::BufferNegative)
        {
            return 52;
        }
        incoming = MachineConfig{};
        incoming.probe.tool_cylindrical_height = SafetyLimits::kMaxToolCylindricalHeight + 1.0f;
        if (validateMachineConfig(incoming) != ConfigValidationError::BufferExceedsSafety)
        {
            return 53;
        }
        incoming = MachineConfig{};
        incoming.force.insert_search_distance = SafetyLimits::kMaxApproachDistance + 1.0f;
        if (validateMachineConfig(incoming) != ConfigValidationError::BufferExceedsSafety)
        {
            return 54;
        }
    }

    return 0;
}
