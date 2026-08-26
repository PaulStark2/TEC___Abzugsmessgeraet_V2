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
    if (!velocityOk(config.motion.v_approach_testpoints) || !velocityOk(config.motion.v_fine_testpoints) ||
        !velocityOk(config.motion.v_unclamp) || !velocityOk(config.motion.v_homing) ||
        !velocityOk(config.motion.phase1_v_extract) || !velocityOk(config.motion.phase1_v_insert) ||
        !velocityOk(config.motion.phase2_v_extract) || !velocityOk(config.motion.phase2_v_insert) ||
        !velocityOk(config.motion.phase3_v_extract) || !velocityOk(config.motion.phase3_v_insert) ||
        !velocityOk(config.probe.v_approach_probe) || !velocityOk(config.probe.v_transfer_probe) ||
        !velocityOk(config.probe.v_search) || !velocityOk(config.probe.v_search_fine))
    {
        if (config.motion.v_approach_testpoints <= 0.0f || config.motion.v_fine_testpoints <= 0.0f ||
            config.motion.v_unclamp <= 0.0f || config.motion.v_homing <= 0.0f ||
            config.motion.phase1_v_extract <= 0.0f || config.motion.phase1_v_insert <= 0.0f ||
            config.motion.phase2_v_extract <= 0.0f || config.motion.phase2_v_insert <= 0.0f ||
            config.motion.phase3_v_extract <= 0.0f || config.motion.phase3_v_insert <= 0.0f ||
            config.probe.v_approach_probe <= 0.0f || config.probe.v_transfer_probe <= 0.0f ||
            config.probe.v_search <= 0.0f || config.probe.v_search_fine <= 0.0f)
        {
            return ConfigValidationError::VelocityNotPositive;
        }
        return ConfigValidationError::VelocityExceedsSafety;
    }

    if (!accelerationOk(config.motion.a_testpoints) || !accelerationOk(config.motion.a_homing) ||
        !accelerationOk(config.motion.phase1_a) || !accelerationOk(config.motion.phase2_a) ||
        !accelerationOk(config.motion.phase3_a) || !accelerationOk(config.probe.a_probe_transfer) ||
        !accelerationOk(config.probe.a_probe_search) || !accelerationOk(config.probe.a_probe_brake))
    {
        if (config.motion.a_testpoints < 0.0f || config.motion.a_homing < 0.0f ||
            config.motion.phase1_a < 0.0f || config.motion.phase2_a < 0.0f ||
            config.motion.phase3_a < 0.0f || config.probe.a_probe_transfer < 0.0f ||
            config.probe.a_probe_search < 0.0f || config.probe.a_probe_brake < 0.0f)
        {
            return ConfigValidationError::AccelerationNegative;
        }
        return ConfigValidationError::AccelerationExceedsSafety;
    }

    if (!cornerOk(config.motion.corner_testpoints) || !cornerOk(config.motion.phase1_corner) ||
        !cornerOk(config.motion.phase2_corner) || !cornerOk(config.motion.phase3_corner) ||
        !cornerOk(config.probe.corner_probe_transfer))
    {
        if (config.motion.corner_testpoints < 0.0f || config.motion.phase1_corner < 0.0f ||
            config.motion.phase2_corner < 0.0f || config.motion.phase3_corner < 0.0f ||
            config.probe.corner_probe_transfer < 0.0f)
        {
            return ConfigValidationError::CornerNegative;
        }
        return ConfigValidationError::CornerExceedsSafety;
    }

    // Use the higher of insert/extract speed per phase for corner consistency.
    const float phase1_v =
        (config.motion.phase1_v_extract > config.motion.phase1_v_insert) ? config.motion.phase1_v_extract
                                                                         : config.motion.phase1_v_insert;
    const float phase2_v =
        (config.motion.phase2_v_extract > config.motion.phase2_v_insert) ? config.motion.phase2_v_extract
                                                                         : config.motion.phase2_v_insert;
    const float phase3_v =
        (config.motion.phase3_v_extract > config.motion.phase3_v_insert) ? config.motion.phase3_v_extract
                                                                         : config.motion.phase3_v_insert;
    const float probe_transfer_v = (config.probe.v_approach_probe > config.probe.v_transfer_probe)
                                       ? config.probe.v_approach_probe
                                       : config.probe.v_transfer_probe;

    if (!blendProfileOk(config.motion.v_approach_testpoints, config.motion.a_testpoints,
                        config.motion.corner_testpoints) ||
        !blendProfileOk(phase1_v, config.motion.phase1_a, config.motion.phase1_corner) ||
        !blendProfileOk(phase2_v, config.motion.phase2_a, config.motion.phase2_corner) ||
        !blendProfileOk(phase3_v, config.motion.phase3_a, config.motion.phase3_corner) ||
        !blendProfileOk(probe_transfer_v, config.probe.a_probe_transfer,
                        config.probe.corner_probe_transfer))
    {
        return ConfigValidationError::CornerInconsistent;
    }

    // Buffer must be strictly positive so every plate point stays below safe_z_plate.
    if (!std::isfinite(config.motion.safe_z_buffer) || !(config.motion.safe_z_buffer > 0.0f) ||
        config.motion.safe_z_buffer > SafetyLimits::kMaxAbsSafeZ)
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
