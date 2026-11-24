#include "MachineConfig.hpp"

#include <cmath>

static bool forceAbsOk(float value_N, float max_abs_N)
{
    if (!(value_N > 0.0f))
    {
        return false;
    }
    return value_N <= max_abs_N;
}

static bool velocityOk(float value)
{
    if (!(value > 0.0f))
    {
        return false;
    }
    return value <= SafetyLimits::kMaxVelocity;
}

static bool accelerationOk(float value)
{
    if (value < 0.0f)
    {
        return false;
    }
    return value <= SafetyLimits::kMaxAcceleration;
}

static bool cornerOk(float value)
{
    if (value < 0.0f)
    {
        return false;
    }
    return value <= SafetyLimits::kMaxCornerRadius;
}

// Blend requires enough distance to decelerate/accelerate inside the corner: R >= v^2/(2a).
// a == 0 or R == 0 → no blending; any R > 0 with a == 0 is inconsistent.
static bool blendProfileOk(float velocity, float acceleration, float corner_radius)
{
    if (corner_radius <= 0.0f)
    {
        return true;
    }
    if (acceleration <= 0.0f)
    {
        return false;
    }
    const float min_radius = (velocity * velocity) / (2.0f * acceleration);
    return corner_radius + 1.0e-6f >= min_radius;
}

float MachineConfig::collisionThreshold(CollisionContext context) const
{
    return collisionLimit(context).z;
}

AxisForceLimit MachineConfig::collisionLimit(CollisionContext context) const
{
    switch (context)
    {
    case CollisionContext::Approach:
        return AxisForceLimit{collision.approach_x_N, collision.approach_y_N, collision.approach_N};
    case CollisionContext::Retract:
        return AxisForceLimit{collision.retract_x_N, collision.retract_y_N, collision.retract_N};
    case CollisionContext::Clamp:
        return AxisForceLimit{collision.clamp_x_N, collision.clamp_y_N, collision.clamp_N};
    case CollisionContext::Unclamp:
        return AxisForceLimit{collision.unclamp_x_N, collision.unclamp_y_N, collision.unclamp_N};
    case CollisionContext::Contact:
        return AxisForceLimit{collision.contact_x_N, collision.contact_y_N, collision.contact_N};
    case CollisionContext::Measure:
        return AxisForceLimit{collision.measure_x_N, collision.measure_y_N, collision.measure_N};
    default:
        return AxisForceLimit{collision.approach_x_N, collision.approach_y_N, collision.approach_N};
    }
}

ConfigValidationError validateMachineConfig(const MachineConfig& config)
{
    if (!velocityOk(config.motion.v_approach_testpoints) || !velocityOk(config.motion.v_fine_testpoints) ||
        !velocityOk(config.motion.v_bore_search) ||
        !velocityOk(config.motion.v_unclamp) || !velocityOk(config.motion.v_homing) ||
        !velocityOk(config.motion.v_homing_fast) ||
        !velocityOk(config.motion.phase1_v_extract) || !velocityOk(config.motion.phase1_v_insert) ||
        !velocityOk(config.motion.phase3_v_extract) || !velocityOk(config.motion.phase3_v_insert) ||
        !velocityOk(config.motion.phase3_v_insert_fine) ||
        !velocityOk(config.motion.extract_after_free_v) ||
        !velocityOk(config.probe.v_approach) || !velocityOk(config.probe.v_search) ||
        !velocityOk(config.fault_manual.v_max))
    {
        if (config.motion.v_approach_testpoints <= 0.0f || config.motion.v_fine_testpoints <= 0.0f ||
            config.motion.v_bore_search <= 0.0f ||
            config.motion.v_unclamp <= 0.0f || config.motion.v_homing <= 0.0f ||
            config.motion.v_homing_fast <= 0.0f ||
            config.motion.phase1_v_extract <= 0.0f || config.motion.phase1_v_insert <= 0.0f ||
            config.motion.phase3_v_extract <= 0.0f || config.motion.phase3_v_insert <= 0.0f ||
            config.motion.phase3_v_insert_fine <= 0.0f ||
            config.motion.extract_after_free_v <= 0.0f ||
            config.probe.v_approach <= 0.0f || config.probe.v_search <= 0.0f ||
            config.fault_manual.v_max <= 0.0f)
        {
            return ConfigValidationError::VelocityNotPositive;
        }
        return ConfigValidationError::VelocityExceedsSafety;
    }

    if (!accelerationOk(config.motion.a_testpoints) || !accelerationOk(config.motion.a_homing) ||
        !accelerationOk(config.motion.a_homing_fast) ||
        !accelerationOk(config.motion.phase1_a) || !accelerationOk(config.motion.phase3_a))
    {
        if (config.motion.a_testpoints < 0.0f || config.motion.a_homing < 0.0f ||
            config.motion.a_homing_fast < 0.0f ||
            config.motion.phase1_a < 0.0f || config.motion.phase3_a < 0.0f)
        {
            return ConfigValidationError::AccelerationNegative;
        }
        return ConfigValidationError::AccelerationExceedsSafety;
    }

    // Manual jog must be able to ramp. 0 would be an instant velocity step.
    if (!(config.motion.a_manual > 0.0f) || config.motion.a_manual > SafetyLimits::kMaxAcceleration ||
        !(config.fault_manual.a > 0.0f) || config.fault_manual.a > SafetyLimits::kMaxAcceleration)
    {
        if (!(config.motion.a_manual > 0.0f) || !(config.fault_manual.a > 0.0f))
        {
            return ConfigValidationError::AccelerationNegative;
        }
        return ConfigValidationError::AccelerationExceedsSafety;
    }

    if (!cornerOk(config.motion.corner_testpoints) || !cornerOk(config.motion.phase1_corner) ||
        !cornerOk(config.motion.phase3_corner))
    {
        if (config.motion.corner_testpoints < 0.0f || config.motion.phase1_corner < 0.0f ||
            config.motion.phase3_corner < 0.0f)
        {
            return ConfigValidationError::CornerNegative;
        }
        return ConfigValidationError::CornerExceedsSafety;
    }

    // Use the higher of insert/extract/(after-free) speed per phase for corner consistency.
    const float phase1_v =
        (config.motion.phase1_v_extract > config.motion.phase1_v_insert) ? config.motion.phase1_v_extract
                                                                         : config.motion.phase1_v_insert;
    float phase3_v =
        (config.motion.phase3_v_extract > config.motion.phase3_v_insert) ? config.motion.phase3_v_extract
                                                                         : config.motion.phase3_v_insert;
    if (config.motion.extract_after_free_v > phase3_v)
    {
        phase3_v = config.motion.extract_after_free_v;
    }
    if (config.motion.phase3_v_insert_fine > phase3_v)
    {
        phase3_v = config.motion.phase3_v_insert_fine;
    }

    if (!blendProfileOk(config.motion.v_approach_testpoints, config.motion.a_testpoints,
                        config.motion.corner_testpoints) ||
        !blendProfileOk(phase1_v, config.motion.phase1_a, config.motion.phase1_corner) ||
        !blendProfileOk(phase3_v, config.motion.phase3_a, config.motion.phase3_corner))
    {
        return ConfigValidationError::CornerInconsistent;
    }

    // Buffer must be strictly positive so every plate point stays below safe_z_plate.
    if (!std::isfinite(config.motion.safe_z_buffer) || !(config.motion.safe_z_buffer > 0.0f) ||
        config.motion.safe_z_buffer > SafetyLimits::kMaxAbsSafeZ)
    {
        return ConfigValidationError::SafeZInvalid;
    }
    if (!std::isfinite(config.motion.safe_z_calib_buffer) ||
        !(config.motion.safe_z_calib_buffer > 0.0f) ||
        config.motion.safe_z_calib_buffer > SafetyLimits::kMaxAbsSafeZ)
    {
        return ConfigValidationError::SafeZInvalid;
    }
    if (!std::isfinite(config.motion.point_z_clearance_mm) || config.motion.point_z_clearance_mm < 0.0f ||
        config.motion.point_z_clearance_mm > SafetyLimits::kMaxPointZClearanceMm)
    {
        return ConfigValidationError::SafeZInvalid;
    }

    if (config.force.force_filter < 0.0f || config.force.force_filter > SafetyLimits::kMaxForceFilter)
    {
        return ConfigValidationError::FilterInvalid;
    }

    // 0 = every tick; upper bound keeps a single stroke from collapsing to one sample.
    if (!std::isfinite(config.force.force_path_dz_mm) || config.force.force_path_dz_mm < 0.0f ||
        config.force.force_path_dz_mm > 5.0f)
    {
        return ConfigValidationError::FilterInvalid;
    }
    if (!std::isfinite(config.force.force_path_dz_fine_mm) ||
        config.force.force_path_dz_fine_mm < 0.0f || config.force.force_path_dz_fine_mm > 5.0f)
    {
        return ConfigValidationError::FilterInvalid;
    }
    if (!std::isfinite(config.force.force_near_target_frac) ||
        config.force.force_near_target_frac <= 0.0f || config.force.force_near_target_frac > 1.0f)
    {
        return ConfigValidationError::FilterInvalid;
    }

    if (!forceAbsOk(config.force.range_N, SafetyLimits::kMaxFzAbs_N) ||
        !forceAbsOk(config.force.hard_limit_x_N, config.force.range_N) ||
        !forceAbsOk(config.force.hard_limit_y_N, config.force.range_N) ||
        !forceAbsOk(config.force.hard_limit_N, config.force.range_N))
    {
        if (!(config.force.range_N > 0.0f) || !(config.force.hard_limit_x_N > 0.0f) ||
            !(config.force.hard_limit_y_N > 0.0f) || !(config.force.hard_limit_N > 0.0f))
        {
            return ConfigValidationError::ForceNotPositive;
        }
        return ConfigValidationError::ForceExceedsSafety;
    }

    if (!forceAbsOk(config.force.xy_tolerance_N, SafetyLimits::kMaxFxAbs_N) ||
        !forceAbsOk(config.force.force_tolerance_N, SafetyLimits::kMaxFzAbs_N) ||
        !forceAbsOk(config.force.test_lateral_force_N, SafetyLimits::kMaxFxAbs_N) ||
        !forceAbsOk(config.collision.approach_x_N, config.force.range_N) ||
        !forceAbsOk(config.collision.approach_y_N, config.force.range_N) ||
        !forceAbsOk(config.collision.approach_N, config.force.range_N) ||
        !forceAbsOk(config.collision.retract_x_N, config.force.range_N) ||
        !forceAbsOk(config.collision.retract_y_N, config.force.range_N) ||
        !forceAbsOk(config.collision.retract_N, config.force.range_N) ||
        !forceAbsOk(config.collision.clamp_x_N, config.force.range_N) ||
        !forceAbsOk(config.collision.clamp_y_N, config.force.range_N) ||
        !forceAbsOk(config.collision.clamp_N, config.force.range_N) ||
        !forceAbsOk(config.collision.unclamp_x_N, config.force.range_N) ||
        !forceAbsOk(config.collision.unclamp_y_N, config.force.range_N) ||
        !forceAbsOk(config.collision.unclamp_N, config.force.range_N) ||
        !forceAbsOk(config.collision.contact_x_N, config.force.range_N) ||
        !forceAbsOk(config.collision.contact_y_N, config.force.range_N) ||
        !forceAbsOk(config.collision.contact_N, config.force.range_N))
    {
        if (config.force.xy_tolerance_N <= 0.0f || config.force.force_tolerance_N <= 0.0f ||
            config.force.test_lateral_force_N <= 0.0f || config.collision.approach_x_N <= 0.0f ||
            config.collision.approach_y_N <= 0.0f || config.collision.approach_N <= 0.0f ||
            config.collision.retract_x_N <= 0.0f || config.collision.retract_y_N <= 0.0f ||
            config.collision.retract_N <= 0.0f || config.collision.clamp_x_N <= 0.0f ||
            config.collision.clamp_y_N <= 0.0f || config.collision.clamp_N <= 0.0f ||
            config.collision.unclamp_x_N <= 0.0f || config.collision.unclamp_y_N <= 0.0f ||
            config.collision.unclamp_N <= 0.0f || config.collision.contact_x_N <= 0.0f ||
            config.collision.contact_y_N <= 0.0f || config.collision.contact_N <= 0.0f)
        {
            return ConfigValidationError::ForceNotPositive;
        }
        return ConfigValidationError::ForceExceedsSafety;
    }

    // Measure collision is percent of Prüfkraft (exclusive > 100%, up to SafetyLimits).
    const auto measurePctOk = [](float pct) {
        return std::isfinite(pct) && pct > SafetyLimits::kMinMeasureCollisionPercent &&
               pct <= SafetyLimits::kMaxMeasureCollisionPercent;
    };
    if (!measurePctOk(config.collision.measure_x_N) || !measurePctOk(config.collision.measure_y_N) ||
        !measurePctOk(config.collision.measure_N))
    {
        return ConfigValidationError::ForceExceedsSafety;
    }

    // Fault-manual recovery hard limits may exceed the measuring range but not the typo brake.
    if (!forceAbsOk(config.fault_manual.hard_limit_x_N, SafetyLimits::kMaxFxAbs_N) ||
        !forceAbsOk(config.fault_manual.hard_limit_y_N, SafetyLimits::kMaxFyAbs_N) ||
        !forceAbsOk(config.fault_manual.hard_limit_N, SafetyLimits::kMaxFzAbs_N) ||
        config.fault_manual.hard_limit_x_N < config.force.hard_limit_x_N ||
        config.fault_manual.hard_limit_y_N < config.force.hard_limit_y_N ||
        config.fault_manual.hard_limit_N < config.force.hard_limit_N)
    {
        if (!(config.fault_manual.hard_limit_x_N > 0.0f) || !(config.fault_manual.hard_limit_y_N > 0.0f) ||
            !(config.fault_manual.hard_limit_N > 0.0f))
        {
            return ConfigValidationError::ForceNotPositive;
        }
        return ConfigValidationError::ForceExceedsSafety;
    }

    if (!(config.force.coord_tolerance_mm > 0.0f) || !(config.force.z_limit_pos_mm > 0.0f) ||
        !(config.force.z_limit_neg_mm > 0.0f) || !(config.force.tool_offset_change_tolerance_mm >= 0.0f))
    {
        return ConfigValidationError::BufferNegative;
    }
    if (config.force.coord_tolerance_mm > SafetyLimits::kMaxCoordTolerance ||
        config.force.z_limit_pos_mm > SafetyLimits::kMaxZLimitMm ||
        config.force.z_limit_neg_mm > SafetyLimits::kMaxZLimitMm ||
        config.force.tool_offset_change_tolerance_mm > SafetyLimits::kMaxToolOffsetChangeTolerance)
    {
        return ConfigValidationError::BufferExceedsSafety;
    }

    if (config.force.xy_correction_step <= 0.0f || config.force.xy_max_correction <= 0.0f ||
        config.force.insert_search_distance <= 0.0f)
    {
        return ConfigValidationError::BufferNegative;
    }
    if (config.force.xy_max_correction > SafetyLimits::kMaxXyCorrection)
    {
        return ConfigValidationError::BufferExceedsSafety;
    }
    if (!(config.force.xy_search_limit > 0.0f))
    {
        return ConfigValidationError::BufferNegative;
    }
    if (config.force.xy_search_limit > SafetyLimits::kMaxXyCorrection)
    {
        return ConfigValidationError::BufferExceedsSafety;
    }

    if (config.force.insertion_buffer < 0.0f)
    {
        return ConfigValidationError::BufferNegative;
    }
    if (config.force.insertion_buffer > SafetyLimits::kMaxInsertionBuffer)
    {
        return ConfigValidationError::BufferExceedsSafety;
    }
    // 0 = no AfterSeat retract (tare / second PreClamp at seat pose).
    if (config.force.after_seat_retract_mm < 0.0f)
    {
        return ConfigValidationError::BufferNegative;
    }
    if (config.force.after_seat_retract_mm > SafetyLimits::kMaxInsertionBuffer)
    {
        return ConfigValidationError::BufferExceedsSafety;
    }
    // 0 = stop extract at kraftfrei after tensile arming.
    if (config.force.extract_after_free_mm < 0.0f)
    {
        return ConfigValidationError::BufferNegative;
    }
    if (config.force.extract_after_free_mm > SafetyLimits::kMaxInsertionBuffer)
    {
        return ConfigValidationError::BufferExceedsSafety;
    }

    if (!(config.force.sample_timeout_s >= SafetyLimits::kMinForceSampleTimeout_s))
    {
        return ConfigValidationError::TimeNotPositive;
    }
    if (config.force.sample_timeout_s > SafetyLimits::kMaxForceSampleTimeout_s)
    {
        return ConfigValidationError::TimeExceedsSafety;
    }

    if (!(config.force.xy_correct_timeout_s >= SafetyLimits::kMinPreClampTimeout_s))
    {
        return ConfigValidationError::TimeNotPositive;
    }
    if (config.force.xy_correct_timeout_s > SafetyLimits::kMaxPreClampTimeout_s)
    {
        return ConfigValidationError::TimeExceedsSafety;
    }
    if (config.force.bore_refine_passes > SafetyLimits::kMaxBoreRefinePasses)
    {
        return ConfigValidationError::IterationsZero;
    }
    if (config.force.xy_max_attempts == 0u ||
        config.force.xy_max_attempts > SafetyLimits::kMaxPreClampAttempts)
    {
        return ConfigValidationError::IterationsZero;
    }

    if (!(config.communication.heartbeat_timeout_s >= SafetyLimits::kMinHeartbeatTimeout_s))
    {
        return ConfigValidationError::TimeNotPositive;
    }
    if (config.communication.heartbeat_timeout_s > SafetyLimits::kMaxHeartbeatTimeout_s)
    {
        return ConfigValidationError::TimeExceedsSafety;
    }

    if (config.spreader.extend_time_s < SafetyLimits::kMinSpreaderTime_s ||
        config.spreader.retract_time_s < SafetyLimits::kMinSpreaderTime_s)
    {
        return ConfigValidationError::TimeNotPositive;
    }
    if (config.spreader.extend_time_s > SafetyLimits::kMaxSpreaderTime_s ||
        config.spreader.retract_time_s > SafetyLimits::kMaxSpreaderTime_s)
    {
        return ConfigValidationError::TimeExceedsSafety;
    }

    if (config.force.insert_search_distance > SafetyLimits::kMaxApproachDistance)
    {
        return ConfigValidationError::BufferExceedsSafety;
    }

    // Einmess-Bohrung / plate bore teach (shared probe params).
    if (!(config.probe.wall_force_N > 0.0f) || !(config.probe.floor_force_N > 0.0f))
    {
        return ConfigValidationError::ForceNotPositive;
    }
    if (config.probe.wall_force_N > SafetyLimits::kMaxFxAbs_N ||
        config.probe.floor_force_N > SafetyLimits::kMaxFzAbs_N)
    {
        return ConfigValidationError::ForceExceedsSafety;
    }
    if (!(config.probe.xy_search_limit > 0.0f) || !(config.probe.z_search_limit > 0.0f) ||
        !(config.probe.z_lift_mm >= 0.0f) || !(config.probe.approach_distance > 0.0f) ||
        !(config.motion.homing_approach_distance > 0.0f))
    {
        return ConfigValidationError::BufferNegative;
    }
    if (config.probe.xy_search_limit > SafetyLimits::kMaxXyCorrection ||
        config.probe.z_search_limit > SafetyLimits::kMaxPlateZSearch ||
        config.probe.z_lift_mm > SafetyLimits::kMaxPlateZLift ||
        config.probe.approach_distance > SafetyLimits::kMaxApproachDistance ||
        config.motion.homing_approach_distance > SafetyLimits::kMaxApproachDistance)
    {
        return ConfigValidationError::BufferExceedsSafety;
    }
    if (!(config.probe.timeout_s >= SafetyLimits::kMinPreClampTimeout_s) ||
        config.probe.timeout_s > SafetyLimits::kMaxPreClampTimeout_s)
    {
        return config.probe.timeout_s < SafetyLimits::kMinPreClampTimeout_s
                   ? ConfigValidationError::TimeNotPositive
                   : ConfigValidationError::TimeExceedsSafety;
    }
    if (config.probe.refine_passes > SafetyLimits::kMaxBoreRefinePasses)
    {
        return ConfigValidationError::IterationsZero;
    }

    return ConfigValidationError::Ok;
}

bool tryApplyMachineConfig(MachineConfig& dest, const MachineConfig& incoming,
                           ConfigValidationError& error)
{
    error = validateMachineConfig(incoming);
    if (error != ConfigValidationError::Ok)
    {
        return false;
    }
    dest = incoming;
    return true;
}
