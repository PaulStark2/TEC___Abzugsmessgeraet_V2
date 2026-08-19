// M16 Core homing sequence tests (Z+ → X+ → Y+).
#include "ForceSample.hpp"
#include "Machine.hpp"

#include "HostInputs.hpp"

#include <cmath>

static HostInputs makeSafeInputs()
{
    HostInputs in{};
    in.force = ForceVector{0.0f, 0.0f, 0.0f};
    in.e_stop = false;
    return withFreshForce(in);
}

int main()
{
    constexpr float kDt = 0.02f;

    // Successful homing through Z, X, Y limits.
    {
        Machine m;
        HostInputs in = makeSafeInputs();
        in.forceSampleSequence++;
        m.setHostInputs(in);

        if (!m.home())
        {
            return 1;
        }
        if (m.machineState() != MachineState::Homing || m.homingState() != HomingState::HomingZ)
        {
            return 2;
        }

        for (int step = 0; step < 500 && m.homingState() != HomingState::Homed; ++step)
        {
            if (m.z() >= 5.0f)
            {
                in.limitZMax = true;
            }
            else
            {
                in.limitZMax = false;
            }
            if (m.x() >= 5.0f)
            {
                in.limitXMax = true;
            }
            else
            {
                in.limitXMax = false;
            }
            if (m.y() >= 5.0f)
            {
                in.limitYMax = true;
            }
            else
            {
                in.limitYMax = false;
            }
            in.forceSampleSequence++;
            m.setHostInputs(in);
            m.tick(kDt);
        }

        if (m.homingState() != HomingState::Homed || !m.isHomed())
        {
            return 3;
        }
        if (m.machineState() != MachineState::Idle)
        {
            return 4;
        }
    }

    // E-Stop during homing => Fault.
    {
        Machine m;
        HostInputs in = makeSafeInputs();
        in.forceSampleSequence++;
        m.setHostInputs(in);
        m.home();
        m.tick(kDt);

        in.e_stop = true;
        in.forceSampleSequence++;
        m.setHostInputs(in);
        m.tick(kDt);

        if (m.machineState() != MachineState::Fault)
        {
            return 10;
        }
    }

    // Stop during homing => HomingError + Stopped.
    {
        Machine m;
        m.setHostInputs(makeSafeInputs());
        m.home();
        m.tick(kDt);
        m.stop();

        if (m.machineState() != MachineState::Stopped)
        {
            return 20;
        }
        if (m.homingState() != HomingState::HomingError)
        {
            return 21;
        }
    }

    // Re-home after success.
    {
        Machine m;
        HostInputs in = makeSafeInputs();
        in.forceSampleSequence++;
        m.setHostInputs(in);
        m.home();

        in.limitZMax = true;
        in.forceSampleSequence++;
        m.setHostInputs(in);
        m.tick(kDt);
        in.limitZMax = false;
        in.limitXMax = true;
        in.forceSampleSequence++;
        m.setHostInputs(in);
        m.tick(kDt);
        in.limitXMax = false;
        in.limitYMax = true;
        in.forceSampleSequence++;
        m.setHostInputs(in);
        m.tick(kDt);

        if (!m.isHomed())
        {
            return 30;
        }

        in.forceSampleSequence++;
        m.setHostInputs(in);
        if (!m.home())
        {
            return 31;
        }
        if (m.homingState() != HomingState::HomingZ)
        {
            return 32;
        }
    }

    // clearFault blocked while Fz above threshold.
    {
        Machine::ForceControlParams params{};
        params.fz_safety_threshold_N = 10.0f;
        Machine m(params);

        HostInputs in = makeSafeInputs();
        in.force = ForceVector{0.0f, 0.0f, 11.0f};
        in.forceSampleSequence++;
        m.setHostInputs(in);
        m.moveTo(10.0f, 0.0f, 0.0f);
        m.tick(kDt);

        if (m.machineState() != MachineState::Fault)
        {
            return 40;
        }

        m.clearFault();
        if (m.machineState() != MachineState::Fault)
        {
            return 41;
        }

        in.force = ForceVector{0.0f, 0.0f, 0.0f};
        in.forceSampleSequence++;
        m.setHostInputs(in);
        m.clearFault();
        if (m.machineState() != MachineState::Idle)
        {
            return 42;
        }
    }

    return 0;
}
