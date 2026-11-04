#include "ForceWatchdog.hpp"
#include "HostInputs.hpp"
#include "Machine.hpp"
#include "SafetyLimits.hpp"
#include "TestConfiguration.hpp"
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

    // Measure uses percent of Prüfkraft; with target set, 150% keeps mid-range Fz OK.
    {
        Machine m;
        TestConfiguration run{};
        run.target_force_N = 20.0f; // effective measure limit = 30 N at 150%
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!m.trySetTestConfiguration(run, err))
        {
            return 12;
        }
        m.setCollisionContext(CollisionContext::Measure);
        m.moveTo(0.0f, 0.0f, -2.0f);
        HostInputs in{};
        in.force.fz = 25.0f; // < 30 N measure effective, > approach 20 N
        in.forceSampleValid = true;
        m.setHostInputs(in);
        m.tick(0.05f);
        if (m.machineState() == MachineState::Fault)
        {
            return 13;
        }
        m.setCollisionContext(CollisionContext::Approach);
        m.tick(0.05f);
        if (m.machineState() != MachineState::Fault)
        {
            return 16;
        }
    }

    // Calibration (phase off): configured hard limits are ignored; typo-brake remains.
    {
        Machine m;
        m.setCollisionPhaseEnabled(false);
        HostInputs in{};
        in.forceSampleValid = true;
        in.force.fz = 25.0f; // above default hard 30? no — below; and above approach
        m.setHostInputs(in);
        m.tick(0.05f);
        if (m.machineState() == MachineState::Fault || m.collisionPhaseEnabled())
        {
            return 20;
        }
        // Configured hard is 30 N; with phase off this must NOT Fault.
        in.force.fz = 40.0f;
        m.setHostInputs(in);
        m.tick(0.05f);
        if (m.machineState() == MachineState::Fault)
        {
            return 21;
        }
        in.force.fz = SafetyLimits::kMaxFzAbs_N + 0.1f;
        m.setHostInputs(in);
        m.tick(0.05f);
        if (m.machineState() != MachineState::Fault || m.lastForceWatchdogTrip() != ForceWatchdogTrip::HardLimit)
        {
            return 22;
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

    // X, Y and Z fault on their own limits.
    {
        MachineConfig cfg{};
        cfg.collision.approach_x_N = 10.0f;
        cfg.collision.approach_y_N = 18.0f;
        cfg.collision.approach_N = 16.0f;
        cfg.force.hard_limit_x_N = 25.0f;
        cfg.force.hard_limit_y_N = 25.0f;
        cfg.force.hard_limit_N = 25.0f;
        if (evaluateForceWatchdog(ForceVector{12.0f, 0.0f, 0.0f}, cfg, CollisionContext::Approach) !=
            ForceWatchdogTrip::CollisionContext)
        {
            return 30;
        }
        if (evaluateForceWatchdog(ForceVector{0.0f, 12.0f, 0.0f}, cfg, CollisionContext::Approach) !=
            ForceWatchdogTrip::None)
        {
            return 31;
        }
        if (evaluateForceWatchdog(ForceVector{0.0f, 0.0f, 12.0f}, cfg, CollisionContext::Approach) !=
            ForceWatchdogTrip::None)
        {
            return 32;
        }
        if (evaluateForceWatchdog(ForceVector{26.0f, 0.0f, 0.0f}, cfg, CollisionContext::Approach, false) !=
            ForceWatchdogTrip::HardLimit)
        {
            return 33;
        }
        if (evaluateForceWatchdog(ForceVector{0.0f, 0.0f, 20.0f}, cfg, CollisionContext::Approach, false) !=
            ForceWatchdogTrip::None)
        {
            return 34;
        }
    }

    return 0;
}
