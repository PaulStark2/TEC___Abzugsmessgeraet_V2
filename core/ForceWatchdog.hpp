#pragma once

#include "HostInputs.hpp"
#include "MachineConfig.hpp"
#include "SafetyLimits.hpp"
#include "Types.hpp"

#include <cmath>

// Result of one non-blocking force watchdog evaluation (M17.3).
enum class ForceWatchdogTrip : uint8_t
{
    None = 0,
    HardLimit = 1, // SafetyLimits, not GUI-configurable
    CollisionContext = 2, // MachineConfig threshold for the active context
    NonFinite = 3 // NaN / Inf must never pass fabs(NaN) >= limit
};

// Evaluate raw Newton values. Never blocks. Order: finite, hard limits, then phase threshold.
inline ForceWatchdogTrip evaluateForceWatchdog(const ForceVector& force, const MachineConfig& config,
                                               CollisionContext context)
{
    if (!std::isfinite(force.fx) || !std::isfinite(force.fy) || !std::isfinite(force.fz))
    {
        return ForceWatchdogTrip::NonFinite;
    }

    if (std::fabs(force.fx) > SafetyLimits::kMaxFxAbs_N ||
        std::fabs(force.fy) > SafetyLimits::kMaxFyAbs_N ||
        std::fabs(force.fz) > SafetyLimits::kMaxFzAbs_N)
    {
        return ForceWatchdogTrip::HardLimit;
    }

    const float threshold_N = config.collisionThreshold(context);
    if (std::fabs(force.fx) > threshold_N || std::fabs(force.fy) > threshold_N ||
        std::fabs(force.fz) > threshold_N)
    {
        return ForceWatchdogTrip::CollisionContext;
    }

    return ForceWatchdogTrip::None;
}
