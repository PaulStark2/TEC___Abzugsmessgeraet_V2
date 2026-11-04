#pragma once

#include "HostInputs.hpp"
#include "MachineConfig.hpp"
#include "Types.hpp"

#include <cmath>

// Result of one non-blocking force watchdog evaluation (M17.3).
enum class ForceWatchdogTrip : uint8_t
{
    None = 0,
    HardLimit = 1, // MachineConfig force.hard_limit_N (or recovery hard in fault-manual)
    CollisionContext = 2, // MachineConfig threshold for the active context
    NonFinite = 3 // NaN / Inf must never pass fabs(NaN) >= limit
};

// Evaluate raw Newton values. Never blocks. Order: finite, hard limits, then phase threshold.
// Each axis is compared with its own limit. hard_limit is absolute N; phase_limit is absolute N
// (caller scales Measure percent → N before calling).
inline ForceWatchdogTrip evaluateForceWatchdog(const ForceVector& force, const AxisForceLimit& hard_limit,
                                               const AxisForceLimit& phase_limit, bool phase_enabled = true)
{
    if (!std::isfinite(force.fx) || !std::isfinite(force.fy) || !std::isfinite(force.fz))
    {
        return ForceWatchdogTrip::NonFinite;
    }

    if (std::fabs(force.fx) > hard_limit.x || std::fabs(force.fy) > hard_limit.y ||
        std::fabs(force.fz) > hard_limit.z)
    {
        return ForceWatchdogTrip::HardLimit;
    }

    if (!phase_enabled)
    {
        return ForceWatchdogTrip::None;
    }

    if (std::fabs(force.fx) > phase_limit.x || std::fabs(force.fy) > phase_limit.y ||
        std::fabs(force.fz) > phase_limit.z)
    {
        return ForceWatchdogTrip::CollisionContext;
    }

    return ForceWatchdogTrip::None;
}

// Convenience: hard limits from MachineConfig.force; phase from collisionLimit(context).
// For Measure, collisionLimit returns percent — tests that need Newton must pass AxisForceLimit.
inline ForceWatchdogTrip evaluateForceWatchdog(const ForceVector& force, const MachineConfig& config,
                                               CollisionContext context, bool phase_enabled = true)
{
    const AxisForceLimit hard{config.force.hard_limit_x_N, config.force.hard_limit_y_N,
                              config.force.hard_limit_N};
    return evaluateForceWatchdog(force, hard, config.collisionLimit(context), phase_enabled);
}
