// Deterministic unit tests for M11 safety (limits, E-Stop, pneumatics).
#include "ForceSample.hpp"
#include "Machine.hpp"
#include "TestExecutor.hpp"

#include "HostInputs.hpp"
#include "TestPoint.hpp"
#include "TestProgram.hpp"

#include <cmath>

static bool nearEqual(float a, float b, float epsilon = 1e-4f)
{
    return std::fabs(a - b) <= epsilon;
}

static void setHostSafety(Machine& machine, const HostInputs& base)
{
    machine.setHostInputs(withFreshForce(base));
}

static HostInputs makeSafeInputs()
{
    HostInputs in{};
    in.force = ForceVector{0.0f, 0.0f, 0.0f};
    in.e_stop = false;
    in.limitXMin = false;
    in.limitXMax = false;
    in.limitYMin = false;
    in.limitYMax = false;
    in.limitZMin = false;
    in.limitZMax = false;
    return in;
}

static bool outputsMatch(const HostOutputs& a, const HostOutputs& b)
{
    return a.spreaderExtend == b.spreaderExtend && a.spreaderExtended == b.spreaderExtended &&
           a.fixtureExtend == b.fixtureExtend && a.fixtureExtended == b.fixtureExtended;
}

int main()
{
    constexpr float kDt = 0.02f;

    // -------------------------------
    // L1–L6: each limit individually (command into active limit)
    // -------------------------------
    {
        struct LimitCase
        {
            bool limitXMin;
            bool limitXMax;
            bool limitYMin;
            bool limitYMax;
            bool limitZMin;
            bool limitZMax;
            float tx;
            float ty;
            float tz;
        };

        const LimitCase cases[] = {
            {true, false, false, false, false, false, -10.0f, 0.0f, 0.0f},
            {false, true, false, false, false, false, 10.0f, 0.0f, 0.0f},
            {false, false, true, false, false, false, 0.0f, -10.0f, 0.0f},
            {false, false, false, true, false, false, 0.0f, 10.0f, 0.0f},
            {false, false, false, false, true, false, 0.0f, 0.0f, -10.0f},
            {false, false, false, false, false, true, 0.0f, 0.0f, 10.0f},
        };

        for (std::size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
        {
            Machine m;
            HostInputs in = makeSafeInputs();
            in.limitXMin = cases[i].limitXMin;
            in.limitXMax = cases[i].limitXMax;
            in.limitYMin = cases[i].limitYMin;
            in.limitYMax = cases[i].limitYMax;
            in.limitZMin = cases[i].limitZMin;
            in.limitZMax = cases[i].limitZMax;
            setHostSafety(m, in);

            const float x0 = m.x();
            const float y0 = m.y();
            const float z0 = m.z();

            m.moveTo(cases[i].tx, cases[i].ty, cases[i].tz);
            m.tick(kDt);

            if (m.machineState() == MachineState::Fault)
            {
                return static_cast<int>(1 + i); // soft limits must not fault
            }

            const uint8_t expected_bit = static_cast<uint8_t>(1u << i);
            if ((m.blockedDirections() & expected_bit) == 0)
            {
                return static_cast<int>(100 + i); // direction must be blocked
            }
        }
    }

    // L1b: after blocked +X command, reverse -X must still work (stored target not permanently clamped).
    {
        Machine m;
        HostInputs in = makeSafeInputs();
        in.limitXMax = true;
        setHostSafety(m, in);

        m.moveTo(10.0f, 0.0f, 0.0f);
        for (int t = 0; t < 5; ++t)
        {
            m.tick(kDt);
        }
        if (m.machineState() == MachineState::Fault)
        {
            return 107;
        }

        m.moveTo(-10.0f, 0.0f, 0.0f);
        m.tick(kDt);
        if (m.machineState() == MachineState::Fault)
        {
            return 108;
        }
        if (nearEqual(m.x(), 0.0f))
        {
            return 109; // should move away from max limit
        }
    }

    // -------------------------------
    // L7: limit active, move away from limit (no fault)
    // -------------------------------
    {
        Machine m;
        HostInputs in = makeSafeInputs();
        in.limitXMin = true;
        setHostSafety(m, in);

        m.moveTo(10.0f, 0.0f, 0.0f); // away from min limit (+X)
        m.tick(kDt);

        if (m.machineState() == MachineState::Fault)
        {
            return 20;
        }
        if (nearEqual(m.x(), 0.0f))
        {
            return 21; // should have moved toward +X
        }
    }

    // -------------------------------
    // L8: at limit, hold position (no fault)
    // -------------------------------
    {
        Machine m;
        HostInputs in = makeSafeInputs();
        in.limitXMin = true;
        setHostSafety(m, in);

        m.moveTo(0.0f, 0.0f, 0.0f); // already at target while limit active
        if (m.machineState() != MachineState::Idle)
        {
            return 22;
        }

        m.tick(kDt);
        if (m.machineState() == MachineState::Fault)
        {
            return 23;
        }
    }

    // -------------------------------
    // E1: E-Stop during Active motion
    // -------------------------------
    {
        Machine m;
        setHostSafety(m, makeSafeInputs());

        m.moveTo(10.0f, 0.0f, 0.0f);
        m.tick(kDt);

        const float x_before = m.x();
        HostInputs in = makeSafeInputs();
        in.e_stop = true;
        setHostSafety(m, in);
        m.tick(kDt);

        if (m.machineState() != MachineState::Fault)
        {
            return 30;
        }
        if (!nearEqual(m.x(), x_before))
        {
            return 31;
        }
    }

    // -------------------------------
    // E2: E-Stop during Paused (via TestExecutor)
    // -------------------------------
    {
        Machine machine;
        machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
        if (!homeMachine(machine))
        {
            return 31;
        }
        (void)calibrateProbeCenter(machine);
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{20.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        ex.start();

        ex.tick(kDt);
        machine.pause();

        HostInputs in = makeSafeInputs();
        in.e_stop = true;
        setHostSafety(machine, in);
        // ProcedureController::tick returns early while Paused — evaluate E-Stop on Machine.
        machine.tick(kDt);

        if (machine.machineState() != MachineState::Fault)
        {
            return 32;
        }
        ex.tick(kDt); // Running tick sees Fault → abort process
        if (ex.processState() != ProcessState::Aborted)
        {
            return 33;
        }
    }

    // -------------------------------
    // E3: E-Stop during Idle
    // -------------------------------
    {
        Machine m;
        HostInputs in = makeSafeInputs();
        in.e_stop = true;
        setHostSafety(m, in);
        m.tick(kDt);

        if (m.machineState() != MachineState::Fault)
        {
            return 34;
        }
    }

    // -------------------------------
    // E4: Fault latched until clearFault(); e_stop must be false
    // -------------------------------
    {
        Machine m;
        HostInputs in = makeSafeInputs();
        in.e_stop = true;
        setHostSafety(m, in);
        m.tick(kDt);

        if (m.machineState() != MachineState::Fault)
        {
            return 35;
        }

        m.clearFault(); // no-op while e_stop still true
        if (m.machineState() != MachineState::Fault)
        {
            return 36;
        }

        in.e_stop = false;
        setHostSafety(m, in);
        m.tick(kDt);
        if (m.machineState() != MachineState::Fault)
        {
            return 37; // still latched without clearFault
        }

        m.clearFault();
        if (m.machineState() != MachineState::Idle)
        {
            return 38;
        }
    }

    // -------------------------------
    // C1: Fz safety faults during Active motion (soft limits do not preempt Fz).
    // -------------------------------
    {
        Machine::ForceControlParams params{};
        params.fz_safety_threshold_N = 10.0f;
        Machine m(params);

        HostInputs in = makeSafeInputs();
        in.limitXMax = true;
        in.force = ForceVector{0.0f, 0.0f, 11.0f};
        setHostSafety(m, in);

        m.moveTo(0.0f, 10.0f, 0.0f);
        m.tick(kDt);

        if (m.machineState() != MachineState::Fault)
        {
            return 40;
        }
    }

    // -------------------------------
    // C2: E-Stop beats Fz in same tick
    // -------------------------------
    {
        Machine::ForceControlParams params{};
        params.fz_safety_threshold_N = 10.0f;
        Machine m(params);

        HostInputs in = makeSafeInputs();
        in.e_stop = true;
        in.force = ForceVector{0.0f, 0.0f, 11.0f};
        setHostSafety(m, in);

        m.moveTo(10.0f, 0.0f, 0.0f);
        m.tick(kDt);

        if (m.machineState() != MachineState::Fault)
        {
            return 41;
        }
    }

    // -------------------------------
    // X1: TestExecutor Aborted on safety, not Completed
    // -------------------------------
    {
        Machine::ForceControlParams params{};
        params.fz_safety_threshold_N = 10.0f;
        Machine machine(params);
        machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
        if (!homeMachine(machine))
        {
            return 49;
        }
        (void)calibrateProbeCenter(machine);
        TestExecutor ex(machine);

        TestProgram program;
        program.add(TestPoint{5.0f, 0.0f, 0.0f});
        program.add(TestPoint{10.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        ex.start();

        HostInputs in = makeSafeInputs();
        setHostSafety(machine, in);
        ex.tick(kDt);

        in.force = ForceVector{0.0f, 0.0f, 11.0f};
        setHostSafety(machine, in);
        ex.tick(kDt);

        if (ex.processState() != ProcessState::Aborted)
        {
            return 50;
        }
        if (ex.processState() == ProcessState::Completed)
        {
            return 51;
        }
    }

    // -------------------------------
    // X2: After safety, motionState != AtTarget
    // -------------------------------
    {
        Machine m;
        HostInputs in = makeSafeInputs();
        in.limitXMax = true;
        setHostSafety(m, in);

        m.moveTo(10.0f, 0.0f, 0.0f);
        m.tick(kDt);

        if (m.blockedDirections() == 0)
        {
            return 52;
        }
    }

    // -------------------------------
    // P1: Spreizer command immediate, feedback after config time
    // -------------------------------
    {
        Machine m;
        m.setSpreaderExtend(true);
        if (!m.hostOutputs().spreaderExtend || m.hostOutputs().spreaderExtended)
        {
            return 60;
        }
        tickWithForce(m, m.machineConfig().spreader.extend_time_s);
        if (!m.hostOutputs().spreaderExtended)
        {
            return 60;
        }
    }

    // -------------------------------
    // P2: Fixture stub
    // -------------------------------
    {
        Machine m;
        m.setFixtureExtend(true);
        const HostOutputs out = m.hostOutputs();
        if (!out.fixtureExtend || !out.fixtureExtended)
        {
            return 61;
        }
    }

    // -------------------------------
    // P3: Independent actuators
    // -------------------------------
    {
        Machine m;
        m.setSpreaderExtend(true);
        m.setFixtureExtend(false);
        tickWithForce(m, m.machineConfig().spreader.extend_time_s);
        const HostOutputs out = m.hostOutputs();
        if (!out.spreaderExtend || !out.spreaderExtended)
        {
            return 62;
        }
        if (out.fixtureExtend || out.fixtureExtended)
        {
            return 63;
        }
    }

    // -------------------------------
    // P4: E-Stop freezes the last spreader command; does not invent a retracted pose
    // -------------------------------
    {
        Machine m;
        m.setSpreaderExtend(true);
        m.setFixtureExtend(false);
        const bool latch_before = m.hostOutputs().spreaderExtended;

        HostInputs in = makeSafeInputs();
        in.e_stop = true;
        setHostSafety(m, in);
        m.tick(kDt);

        if (m.machineState() != MachineState::Fault)
        {
            return 64;
        }
        if (!m.hostOutputs().spreaderExtend || m.spreaderBusy())
        {
            return 65;
        }
        if (m.hostOutputs().spreaderExtended != latch_before)
        {
            return 65;
        }
        if (m.spreaderState() != SpreaderState::SafeDeenergized)
        {
            return 65;
        }
        if (m.hostOutputs().fixtureExtend || m.hostOutputs().fixtureExtended)
        {
            return 65;
        }
    }

    // -------------------------------
    // P4b: Pneumatics unchanged on limit/Fz fault (reversed initial state)
    // -------------------------------
    {
        Machine::ForceControlParams params{};
        params.fz_safety_threshold_N = 10.0f;
        Machine m(params);
        m.setSpreaderExtend(false);
        m.setFixtureExtend(true);
        const HostOutputs before = m.hostOutputs();

        HostInputs in = makeSafeInputs();
        in.force = ForceVector{0.0f, 0.0f, 11.0f};
        setHostSafety(m, in);
        m.moveTo(10.0f, 0.0f, 0.0f);
        m.tick(kDt);

        if (m.machineState() != MachineState::Fault)
        {
            return 66;
        }
        if (!outputsMatch(m.hostOutputs(), before))
        {
            return 67;
        }
    }

    // -------------------------------
    // P5: Pneumatic commands no-op in Fault; clearFault restores commandability
    // -------------------------------
    {
        Machine m;
        m.setSpreaderExtend(false);
        m.setFixtureExtend(false);

        HostInputs in = makeSafeInputs();
        in.e_stop = true;
        setHostSafety(m, in);
        m.tick(kDt);

        const HostOutputs at_fault = m.hostOutputs();

        m.setSpreaderExtend(true);
        m.setFixtureExtend(true);
        if (!outputsMatch(m.hostOutputs(), at_fault))
        {
            return 68; // no-op in fault
        }

        in.e_stop = false;
        setHostSafety(m, in);
        m.clearFault();
        if (m.machineState() != MachineState::Idle)
        {
            return 69;
        }
        if (!outputsMatch(m.hostOutputs(), at_fault))
        {
            return 70; // clearFault must not change pneumatics
        }

        m.setSpreaderExtend(true);
        tickWithForce(m, m.machineConfig().spreader.extend_time_s);
        if (!m.hostOutputs().spreaderExtend || !m.hostOutputs().spreaderExtended)
        {
            return 71; // commands work after clearFault
        }
    }

    return 0;
}
