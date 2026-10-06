#include "ForceWatchdog.hpp"
#include "HostInputs.hpp"
#include "Machine.hpp"
#include "SafetyLimits.hpp"
#include "Types.hpp"

int main()
{
    // Hard limit trips above the configured hard limit, not the 500 N typo brake.
    {
        MachineConfig cfg{};
        cfg.collision.approach_N = 20.0f;
        ForceVector force{0.0f, 0.0f, cfg.force.hard_limit_N + 0.1f};
        if (evaluateForceWatchdog(force, cfg, CollisionContext::Approach) != ForceWatchdogTrip::HardLimit)
        {
            return 1;
        }
        // Exactly the hard limit is allowed; the phase threshold still applies.
        force.fz = cfg.force.hard_limit_N;
        if (evaluateForceWatchdog(force, cfg, CollisionContext::Approach) != ForceWatchdogTrip::CollisionContext)
        {
            return 2;
        }
        force.fz = 19.9f;
        if (evaluateForceWatchdog(force, cfg, CollisionContext::Approach) != ForceWatchdogTrip::None)
        {
            return 3;
        }
    }

    // Phase context uses its own threshold; other axes still move after a prior... wait, Fault latches.
    {
        Machine m;
        m.setCollisionContext(CollisionContext::Approach);
        m.moveTo(5.0f, 0.0f, 0.0f);
        HostInputs in{};
        in.force.fx = 25.0f; // default approach threshold 20 N
        m.setHostInputs(in);
        m.tick(0.05f);
        if (m.machineState() != MachineState::Fault)
        {
            return 10;
        }
        if (m.lastForceWatchdogTrip() != ForceWatchdogTrip::CollisionContext)
        {
            return 11;
        }
    }

    // Measure context allows a higher Fz than Approach.
    {
        Machine m;
        m.setCollisionContext(CollisionContext::Measure);
        m.moveTo(0.0f, 0.0f, -2.0f);
        HostInputs in{};
        in.force.fz = 25.0f; // < default measure 30 N, > approach 20 N
        m.setHostInputs(in);
        m.tick(0.05f);
        if (m.machineState() == MachineState::Fault)
        {
            return 12;
        }
        m.setCollisionContext(CollisionContext::Approach);
        m.tick(0.05f);
        if (m.machineState() != MachineState::Fault)
        {
            return 13;
        }
    }

    // Calibration can drop the phase threshold. The hard limit still trips.
    {
        Machine m;
        m.setCollisionPhaseEnabled(false);
        HostInputs in{};
        in.force.fz = 25.0f;
        m.setHostInputs(in);
        m.tick(0.05f);
        if (m.machineState() == MachineState::Fault || m.collisionPhaseEnabled())
        {
            return 20;
        }
        in.force.fz = SafetyLimits::kMaxFzAbs_N + 0.1f;
        m.setHostInputs(in);
        m.tick(0.05f);
        if (m.machineState() != MachineState::Fault || m.lastForceWatchdogTrip() != ForceWatchdogTrip::HardLimit)
        {
            return 21;
        }
    }

    // Hard limit in Idle (not only Active).
    {
        Machine m;
        HostInputs in{};
        in.force.fx = 50.1f;
        m.setHostInputs(in);
        m.tick(0.05f);
        if (m.machineState() != MachineState::Fault)
        {
            return 14;
        }
        if (m.lastForceWatchdogTrip() != ForceWatchdogTrip::HardLimit)
        {
            return 15;
        }
    }

    return 0;
}
