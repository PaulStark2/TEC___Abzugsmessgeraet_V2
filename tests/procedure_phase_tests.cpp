#include "HostInputs.hpp"
#include "Machine.hpp"
#include "TestExecutor.hpp"
#include "TestPoint.hpp"
#include "TestProgram.hpp"
#include "Types.hpp"

int main()
{
    Machine machine;
    machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
    TestExecutor ex(machine);
    TestProgram program;
    program.add(TestPoint{8.0f, 0.0f, 0.0f});
    ex.setProgram(program);

    if (!ex.start())
    {
        return 1;
    }
    if (ex.procedurePhase() != ProcedurePhase::PointApproach &&
        ex.procedurePhase() != ProcedurePhase::MoveSafePose)
    {
        return 2;
    }

    HostInputs in{};
    machine.setHostInputs(in);

    // Drive into Clamp, then pause must be rejected.
    for (int i = 0; i < 200 && ex.procedurePhase() != ProcedurePhase::Clamp; ++i)
    {
        ex.tick(0.05f);
    }
    if (ex.procedurePhase() != ProcedurePhase::Clamp)
    {
        return 3;
    }
    if (ex.pauseAllowed())
    {
        return 4;
    }
    ex.pause();
    if (machine.machineState() == MachineState::Paused)
    {
        return 5;
    }

    ex.stop();
    if (ex.processState() != ProcessState::Aborted)
    {
        return 6;
    }
    if (machine.machineState() != MachineState::Stopped)
    {
        return 7;
    }

    // Pause allowed during approach.
    Machine m2;
    m2.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
    TestExecutor ex2(m2);
    TestProgram p2;
    p2.add(TestPoint{20.0f, 0.0f, 0.0f});
    ex2.setProgram(p2);
    m2.setHostInputs(in);
    ex2.start();
    if (!ex2.pauseAllowed())
    {
        return 8;
    }
    ex2.pause();
    if (m2.machineState() != MachineState::Paused)
    {
        return 9;
    }

    return 0;
}
