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
// Each axis is compared with its own limit.
inline ForceWatchdogTrip evaluateForceWatchdog(const ForceVector& force, const MachineConfig& config,
                                               CollisionContext context, bool phase_enabled = true)
{
    if (!std::isfinite(force.fx) || !std::isfinite(force.fy) || !std::isfinite(force.fz))
    {
        return ForceWatchdogTrip::NonFinite;
    }

    if (std::fabs(force.fx) > config.force.hard_limit_x_N ||
        std::fabs(force.fy) > config.force.hard_limit_y_N ||
        std::fabs(force.fz) > config.force.hard_limit_N)
    {
        return ForceWatchdogTrip::HardLimit;
    }

    if (!phase_enabled)
    {
        return ForceWatchdogTrip::None;
    }

    const AxisForceLimit limit = config.collisionLimit(context);
    if (std::fabs(force.fx) > limit.x || std::fabs(force.fy) > limit.y || std::fabs(force.fz) > limit.z)
    {
        return ForceWatchdogTrip::CollisionContext;
    }

    return ForceWatchdogTrip::None;
}
