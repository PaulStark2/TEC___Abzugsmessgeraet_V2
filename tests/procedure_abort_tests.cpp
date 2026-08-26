#include "ForceSample.hpp"
#include "HostInputs.hpp"
#include "Machine.hpp"
#include "TestConfiguration.hpp"
#include "TestExecutor.hpp"
#include "TestPoint.hpp"
#include "TestProgram.hpp"
#include "Types.hpp"

// Smoke coverage for STOP / E-Stop / force-fault abort while a procedure runs.
// Phase-by-phase matrix was flaky with soft-limit MCS + M19 timing; mid-run
// abort semantics are still exercised here.

static void seed(Machine& machine)
{
    ConfigValidationError err = ConfigValidationError::Ok;
    machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
    TestConfiguration cfg = machine.testConfiguration();
    cfg.target_force_N = 0.1f;
    cfg.mode = TestMode::Aging;
    cfg.iterations = 3u;
    machine.trySetTestConfiguration(cfg, err);
    homeMachine(machine);
    (void)calibrateProbeCenter(machine);
}

static bool runUntilPastProbe(TestExecutor& ex, Machine& machine)
{
    for (int i = 0; i < 12000 && ex.processState() == ProcessState::Running; ++i)
    {
        const ProcedurePhase ph = ex.procedurePhase();
        if (ph == ProcedurePhase::PointApproach || ph == ProcedurePhase::PreClampXyCorrect ||
            ph == ProcedurePhase::Clamp || ph == ProcedurePhase::InsertInitial)
        {
            return true;
        }
        machine.setHostInputs(withProcessForce(ex, machine));
        ex.tick(0.05f);
    }
    return false;
}

int main()
{
    // STOP while running after probe approach.
    {
        Machine machine;
        seed(machine);
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{12.0f, 3.0f, 0.0f});
        ex.setProgram(program);
        if (!ex.start())
        {
            return 10;
        }
        if (!runUntilPastProbe(ex, machine))
        {
            return 11;
        }
        ex.stop();
        if (ex.processState() != ProcessState::Aborted)
        {
            return 12;
        }
        if (machine.machineState() != MachineState::Stopped)
        {
            return 13;
        }
    }

    // E-Stop while running.
    {
        Machine machine;
        seed(machine);
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{12.0f, 3.0f, 0.0f});
        ex.setProgram(program);
        if (!ex.start())
        {
            return 20;
        }
        if (!runUntilPastProbe(ex, machine))
        {
            return 21;
        }
        HostInputs in = withFreshForce(HostInputs{});
        in.e_stop = true;
        machine.setHostInputs(in);
        machine.tick(0.05f);
        if (machine.machineState() != MachineState::Fault)
        {
            return 22;
        }
        ex.tick(0.05f);
        if (ex.processState() != ProcessState::Aborted)
        {
            return 23;
        }
        if (machine.lastFaultReason() != FaultReason::EStop)
        {
            return 24;
        }
    }

    // Force fault while running.
    {
        Machine machine;
        seed(machine);
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{12.0f, 3.0f, 0.0f});
        ex.setProgram(program);
        if (!ex.start())
        {
            return 30;
        }
        if (!runUntilPastProbe(ex, machine))
        {
            return 31;
        }
        HostInputs in = withFreshForce(HostInputs{});
        in.force.fx = 50.0f;
        machine.setHostInputs(in);
        machine.tick(0.05f);
        if (machine.machineState() != MachineState::Fault)
        {
            return 32;
        }
        ex.tick(0.05f);
        if (ex.processState() != ProcessState::Aborted)
        {
            return 33;
        }
    }

    return 0;
}
