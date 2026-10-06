#include "ForceSample.hpp"
#include "Machine.hpp"
#include "MachineConfig.hpp"
#include "SpreaderTimer.hpp"
#include "Types.hpp"

int main()
{
    // Unit timer: command immediate, feedback after time.
    {
        SpreaderTimer timer;
        timer.commandExtend(0.4f);
        if (!timer.command() || timer.extended())
        {
            return 1;
        }
        timer.tick(0.2f);
        if (timer.extended() || !timer.isBusy())
        {
            return 2;
        }
        timer.tick(0.2f);
        if (!timer.extended() || timer.isBusy())
        {
            return 3;
        }
        timer.commandRetract(0.3f);
        if (timer.command() || !timer.extended())
        {
            return 4;
        }
        timer.tick(0.3f);
        if (timer.extended() || timer.command())
        {
            return 5;
        }
    }

    // Machine uses MachineConfig times; no blocking delay.
    {
        Machine m;
        MachineConfig cfg = m.machineConfig();
        cfg.spreader.extend_time_s = 0.2f;
        cfg.spreader.retract_time_s = 0.1f;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!m.trySetMachineConfig(cfg, err))
        {
            return 10;
        }
        m.setSpreaderExtend(true);
        if (!m.hostOutputs().spreaderExtend || m.hostOutputs().spreaderExtended)
        {
            return 11;
        }
        tickWithForce(m, 0.1f);
        if (m.hostOutputs().spreaderExtended)
        {
            return 12;
        }
        tickWithForce(m, 0.1f);
        if (!m.hostOutputs().spreaderExtended)
        {
            return 13;
        }
        m.setSpreaderExtend(false);
        if (m.hostOutputs().spreaderExtend || !m.hostOutputs().spreaderExtended)
        {
            return 14;
        }
        tickWithForce(m, 0.1f);
        if (m.hostOutputs().spreaderExtended)
        {
            return 15;
        }
    }

    // STOP freezes the last command; latch is not rewritten.
    {
        SpreaderTimer timer;
        timer.commandExtend(0.4f);
        timer.tick(0.1f);
        timer.enterSafeState();
        if (!timer.command() || timer.extended() || timer.isBusy())
        {
            return 20;
        }
        if (timer.state() != SpreaderState::SafeDeenergized)
        {
            return 21;
        }
    }

    // Fixture remains an immediate stub (no timer).
    {
        Machine m;
        m.setFixtureExtend(true);
        if (!m.hostOutputs().fixtureExtend || !m.hostOutputs().fixtureExtended)
        {
            return 16;
        }
    }

    return 0;
}
