#include "MachineConfig.hpp"
#include "ProbeGeometry.hpp"

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

float MachineConfig::collisionThreshold(CollisionContext context) const
{
    switch (context)
    {
    case CollisionContext::Approach:
        return collision.approach_N;
    case CollisionContext::Retract:
        return collision.retract_N;
    case CollisionContext::Clamp:
        return collision.clamp_N;
    case CollisionContext::Unclamp:
        return collision.unclamp_N;
    case CollisionContext::Contact:
        return collision.contact_N;
    case CollisionContext::Measure:
        return collision.measure_N;
    default:
        return collision.approach_N;
    }
}

ConfigValidationError validateMachineConfig(const MachineConfig& config)
{
    if (!velocityOk(config.motion.v_approach) || !velocityOk(config.motion.v_slow) ||
        !velocityOk(config.motion.v_measure) || !velocityOk(config.motion.v_homing) ||
        !velocityOk(config.probe.search_speed) || !velocityOk(config.probe.fine_search_speed))
    {
        if (config.motion.v_approach <= 0.0f || config.motion.v_slow <= 0.0f ||
            config.motion.v_measure <= 0.0f || config.motion.v_homing <= 0.0f ||
            config.probe.search_speed <= 0.0f || config.probe.fine_search_speed <= 0.0f)
        {
            return ConfigValidationError::VelocityNotPositive;
        }
        return ConfigValidationError::VelocityExceedsSafety;
    }

    if (config.motion.acceleration < 0.0f)
    {
        return ConfigValidationError::AccelerationNegative;
    }
    if (config.motion.acceleration > SafetyLimits::kMaxAcceleration)
    {
        return ConfigValidationError::AccelerationExceedsSafety;
    }

    if (!std::isfinite(config.motion.safe_z) ||
        std::fabs(config.motion.safe_z) > SafetyLimits::kMaxAbsSafeZ)
    {
        return ConfigValidationError::SafeZInvalid;
    }

    if (config.force.force_filter < 0.0f || config.force.force_filter > SafetyLimits::kMaxForceFilter)
    {
        return ConfigValidationError::FilterInvalid;
    }

    if (!forceAbsOk(config.force.xy_tolerance_N, SafetyLimits::kMaxFxAbs_N) ||
        !forceAbsOk(config.phase2.force_tolerance_N, SafetyLimits::kMaxFzAbs_N) ||
        !forceAbsOk(config.collision.approach_N, SafetyLimits::kMaxFzAbs_N) ||
        !forceAbsOk(config.collision.retract_N, SafetyLimits::kMaxFzAbs_N) ||
        !forceAbsOk(config.collision.clamp_N, SafetyLimits::kMaxFzAbs_N) ||
        !forceAbsOk(config.collision.unclamp_N, SafetyLimits::kMaxFzAbs_N) ||
        !forceAbsOk(config.collision.contact_N, SafetyLimits::kMaxFzAbs_N) ||
        !forceAbsOk(config.collision.measure_N, SafetyLimits::kMaxFzAbs_N))
    {
        if (config.force.xy_tolerance_N <= 0.0f || config.phase2.force_tolerance_N <= 0.0f ||
            config.collision.approach_N <= 0.0f || config.collision.retract_N <= 0.0f ||
            config.collision.clamp_N <= 0.0f || config.collision.unclamp_N <= 0.0f ||
            config.collision.contact_N <= 0.0f || config.collision.measure_N <= 0.0f)
        {
            return ConfigValidationError::ForceNotPositive;
        }
        return ConfigValidationError::ForceExceedsSafety;
    }

    if (config.force.xy_correction_step <= 0.0f || config.force.xy_max_correction <= 0.0f ||
        config.force.insert_search_distance <= 0.0f || config.probe.search_distance <= 0.0f ||
        config.probe.backoff < 0.0f)
    {
        return ConfigValidationError::BufferNegative;
    }
    if (config.force.xy_max_correction > SafetyLimits::kMaxXyCorrection)
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

    if (!(config.phase2.z_correction_gain > 0.0f) || !(config.phase2.minimum_correction > 0.0f))
    {
        return ConfigValidationError::GainNotPositive;
    }
    if (config.phase2.z_correction_max <= 0.0f)
    {
        return ConfigValidationError::BufferNegative;
    }
    if (config.phase2.z_correction_max > SafetyLimits::kMaxZCorrection)
    {
        return ConfigValidationError::BufferExceedsSafety;
    }

    if (config.phase2.max_optimization_iterations == 0u ||
        config.phase2.max_optimization_iterations > SafetyLimits::kMaxIterations)
    {
        return ConfigValidationError::IterationsZero;
    }
    if (config.phase2.max_sign_flips == 0u ||
        config.phase2.max_sign_flips > SafetyLimits::kMaxIterations)
    {
        return ConfigValidationError::IterationsZero;
    }

    if (config.probe.search_depth <= 0.0f || config.probe.approach_distance <= 0.0f ||
        config.probe.head_diameter < 0.0f || config.probe.measuring_head_diameter <= 0.0f ||
        config.probe.measuring_head_thickness <= 0.0f || config.probe.tool_diameter <= 0.0f ||
        config.probe.tool_cylindrical_height <= 0.0f || config.probe.xy_search_margin < 0.0f)
    {
        return ConfigValidationError::BufferNegative;
    }
    if (config.probe.search_depth > SafetyLimits::kMaxSearchDepth ||
        config.probe.approach_distance > SafetyLimits::kMaxApproachDistance ||
        config.probe.search_distance > SafetyLimits::kMaxApproachDistance ||
        config.force.insert_search_distance > SafetyLimits::kMaxApproachDistance ||
        config.probe.backoff > SafetyLimits::kMaxApproachDistance ||
        config.probe.head_diameter > SafetyLimits::kMaxProbeDiameter ||
        config.probe.measuring_head_diameter > SafetyLimits::kMaxProbeDiameter ||
        config.probe.measuring_head_thickness > SafetyLimits::kMaxProbeThickness ||
        config.probe.tool_diameter > SafetyLimits::kMaxProbeDiameter ||
        config.probe.tool_cylindrical_height > SafetyLimits::kMaxToolCylindricalHeight ||
        config.probe.xy_search_margin > SafetyLimits::kMaxXySearchMargin)
    {
        return ConfigValidationError::BufferExceedsSafety;
    }
    if (!(xySearchInnerRadius(config.probe.measuring_head_diameter, config.probe.xy_search_margin) > 0.0f))
    {
        return ConfigValidationError::BufferNegative;
    }
    if (config.probe.num_fine_searches > SafetyLimits::kMaxIterations)
    {
        return ConfigValidationError::IterationsZero;
    }
    if (!std::isfinite(config.probe.probe_offset_reference.x) ||
        !std::isfinite(config.probe.probe_offset_reference.y) ||
        !std::isfinite(config.probe.probe_offset_reference.z) ||
        std::fabs(config.probe.probe_offset_reference.x) > SafetyLimits::kMaxNominalAbs ||
        std::fabs(config.probe.probe_offset_reference.y) > SafetyLimits::kMaxNominalAbs ||
        std::fabs(config.probe.probe_offset_reference.z) > SafetyLimits::kMaxNominalAbs)
    {
        return ConfigValidationError::SafeZInvalid;
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
