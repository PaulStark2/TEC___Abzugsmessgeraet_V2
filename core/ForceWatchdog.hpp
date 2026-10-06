#pragma once

#include "HostInputs.hpp"
#include "MachineConfig.hpp"
#include "Types.hpp"

#include <cmath>

// Result of one non-blocking force watchdog evaluation (M17.3).
enum class ForceWatchdogTrip : uint8_t
{
    None = 0,
    HardLimit = 1, // MachineConfig force.hard_limit_N
    CollisionContext = 2, // MachineConfig threshold for the active context
    NonFinite = 3 // NaN / Inf must never pass fabs(NaN) >= limit
};

// Evaluate raw Newton values. Never blocks. Order: finite, hard limits, then phase threshold.
inline ForceWatchdogTrip evaluateForceWatchdog(const ForceVector& force, const MachineConfig& config,
                                               CollisionContext context, bool phase_enabled = true)
{
    if (!std::isfinite(force.fx) || !std::isfinite(force.fy) || !std::isfinite(force.fz))
    {
        return ForceWatchdogTrip::NonFinite;
    }

    const float hard_N = config.force.hard_limit_N;
    if (std::fabs(force.fx) > hard_N || std::fabs(force.fy) > hard_N || std::fabs(force.fz) > hard_N)
    {
        return ForceWatchdogTrip::HardLimit;
    }

    if (!phase_enabled)
    {
        return ForceWatchdogTrip::None;
    }

    const float threshold_N = config.collisionThreshold(context);
    if (std::fabs(force.fx) > threshold_N || std::fabs(force.fy) > threshold_N ||
        std::fabs(force.fz) > threshold_N)
    {
        return ForceWatchdogTrip::CollisionContext;
    }

    return ForceWatchdogTrip::None;
}
