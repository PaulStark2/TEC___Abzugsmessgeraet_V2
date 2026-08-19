#include "ForceSample.hpp"
#include "ForceWatchdog.hpp"
#include "HostInputs.hpp"
#include "Machine.hpp"
#include "MachineConfig.hpp"
#include "SafetyLimits.hpp"
#include "Types.hpp"

#include <cmath>
#include <limits>

int main()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float pos_inf = std::numeric_limits<float>::infinity();
    const float neg_inf = -std::numeric_limits<float>::infinity();

    // NaN must trip NonFinite, never slip through fabs(NaN) >= limit.
    {
        MachineConfig cfg{};
        ForceVector force{nan, 0.0f, 0.0f};
        if (evaluateForceWatchdog(force, cfg, CollisionContext::Approach) != ForceWatchdogTrip::NonFinite)
        {
            return 1;
        }
        force = ForceVector{0.0f, nan, 0.0f};
        if (evaluateForceWatchdog(force, cfg, CollisionContext::Approach) != ForceWatchdogTrip::NonFinite)
        {
            return 2;
        }
        force = ForceVector{0.0f, 0.0f, nan};
        if (evaluateForceWatchdog(force, cfg, CollisionContext::Approach) != ForceWatchdogTrip::NonFinite)
        {
            return 3;
        }
    }

    // +Inf / -Inf trip NonFinite (not a numeric comparison loophole).
    {
        MachineConfig cfg{};
        if (evaluateForceWatchdog(ForceVector{pos_inf, 0.0f, 0.0f}, cfg, CollisionContext::Approach) !=
            ForceWatchdogTrip::NonFinite)
        {
            return 4;
        }
        if (evaluateForceWatchdog(ForceVector{0.0f, neg_inf, 0.0f}, cfg, CollisionContext::Approach) !=
            ForceWatchdogTrip::NonFinite)
        {
            return 5;
        }
        if (evaluateForceWatchdog(ForceVector{0.0f, 0.0f, pos_inf}, cfg, CollisionContext::Measure) !=
            ForceWatchdogTrip::NonFinite)
        {
            return 6;
        }
    }

    // Machine latches Fault on NaN and stops motion.
    {
        Machine m;
        m.moveTo(20.0f, 0.0f, 0.0f);
        HostInputs in = withFreshForce(HostInputs{});
        in.force.fx = nan;
        m.setHostInputs(in);
        m.tick(0.05f);
        if (m.machineState() != MachineState::Fault)
        {
            return 10;
        }
        if (m.lastFaultReason() != FaultReason::ForceNonFinite)
        {
            return 11;
        }
        const float x = m.x();
        m.tick(0.05f);
        if (m.x() != x)
        {
            return 12;
        }
        m.resume();
        if (m.machineState() != MachineState::Fault)
        {
            return 13;
        }
    }

    // +Inf on Fz during Active.
    {
        Machine m;
        m.moveTo(5.0f, 0.0f, 0.0f);
        HostInputs in = withFreshForce(HostInputs{});
        in.force.fz = pos_inf;
        m.setHostInputs(in);
        m.tick(0.05f);
        if (m.machineState() != MachineState::Fault || m.lastFaultReason() != FaultReason::ForceNonFinite)
        {
            return 14;
        }
    }

    // Invalid sample (host reports no conversion) is a Fault, not 0 N.
    {
        Machine m;
        m.moveTo(8.0f, 0.0f, 0.0f);
        HostInputs in{};
        in.forceSampleValid = false;
        in.forceSampleSequence = 1;
        m.setHostInputs(in);
        m.tick(0.05f);
        if (m.machineState() != MachineState::Fault)
        {
            return 20;
        }
        if (m.lastFaultReason() != FaultReason::ForceInvalidSample)
        {
            return 21;
        }
    }

    // Unchanged sequence ages out → ForceStale. Motion must stop.
    {
        Machine m;
        MachineConfig cfg = m.machineConfig();
        cfg.force.sample_timeout_s = 0.1f;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!m.trySetMachineConfig(cfg, err))
        {
            return 30;
        }
        HostInputs in{};
        in.forceSampleValid = true;
        in.forceSampleSequence = 7;
        m.setHostInputs(in);
        m.moveTo(30.0f, 0.0f, 0.0f);
        m.tick(0.05f);
        if (m.machineState() == MachineState::Fault)
        {
            return 31;
        }
        m.tick(0.05f);
        m.tick(0.05f);
        if (m.machineState() != MachineState::Fault)
        {
            return 32;
        }
        if (m.lastFaultReason() != FaultReason::ForceStale)
        {
            return 33;
        }
        const float x = m.x();
        m.tick(0.05f);
        if (m.x() != x)
        {
            return 34;
        }
        m.resume();
        if (m.machineState() != MachineState::Fault)
        {
            return 35;
        }
    }

    // New sequence keeps the watchdog quiet.
    {
        Machine m;
        MachineConfig cfg = m.machineConfig();
        cfg.force.sample_timeout_s = 0.1f;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!m.trySetMachineConfig(cfg, err))
        {
            return 40;
        }
        m.moveTo(4.0f, 0.0f, 0.0f);
        for (int i = 0; i < 20; ++i)
        {
            tickWithForce(m, 0.05f);
            if (m.machineState() == MachineState::Fault)
            {
                return 41;
            }
        }
    }

    // Hard limit still trips after finite check.
    {
        Machine m;
        HostInputs in = withFreshForce(HostInputs{});
        in.force.fz = SafetyLimits::kMaxFzAbs_N;
        m.setHostInputs(in);
        m.tick(0.05f);
        if (m.machineState() != MachineState::Fault || m.lastFaultReason() != FaultReason::ForceHardLimit)
        {
            return 50;
        }
    }

    return 0;
}
