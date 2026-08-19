#include "ForceSample.hpp"
#include "HostInputs.hpp"
#include "Machine.hpp"
#include "TestConfiguration.hpp"
#include "TestExecutor.hpp"
#include "TestPoint.hpp"
#include "TestProgram.hpp"
#include "Types.hpp"

static void seed(Machine& machine)
{
    MachineConfig mcfg = machine.machineConfig();
    mcfg.calibration.home_offset = TestPoint{5.0f, 0.0f, 0.0f};
    ConfigValidationError err = ConfigValidationError::Ok;
    machine.trySetMachineConfig(mcfg, err);
    machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
    TestConfiguration cfg = machine.testConfiguration();
    cfg.target_force_N = 0.1f;
    cfg.mode = TestMode::Aging;
    cfg.iterations = 3u;
    machine.trySetTestConfiguration(cfg, err);
    homeMachine(machine);
    (void)calibrateProbeCenter(machine);
    machine.moveTo(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < 80 && machine.machineState() != MachineState::Idle; ++i)
    {
        tickWithForce(machine, 0.05f);
    }
}

static bool runUntilPhase(TestExecutor& ex, Machine& machine, ProcedurePhase phase, int max_steps)
{
    for (int i = 0; i < max_steps && ex.processState() == ProcessState::Running; ++i)
    {
        if (ex.procedurePhase() == phase)
        {
            return true;
        }
        machine.setHostInputs(withProcessForce(ex, machine));
        ex.tick(0.05f);
        if (ex.procedurePhase() == phase)
        {
            return true;
        }
    }
    return ex.procedurePhase() == phase;
}

static bool motionFrozen(Machine& machine)
{
    const float x = machine.x();
    const float y = machine.y();
    const float z = machine.z();
    const MachineState state = machine.machineState();
    tickWithForce(machine, 0.05f);
    if (machine.x() != x || machine.y() != y || machine.z() != z)
    {
        return false;
    }
    return machine.machineState() == state;
}

static int checkStopInPhase(ProcedurePhase phase, int fail_base)
{
    Machine machine;
    seed(machine);
    TestExecutor ex(machine);
    TestProgram program;
    program.add(TestPoint{12.0f, 3.0f, 0.0f});
    ex.setProgram(program);
    if (!ex.start())
    {
        return fail_base;
    }
    if (!runUntilPhase(ex, machine, phase, 2500))
    {
        return fail_base + 1;
    }
    ex.stop();
    if (ex.processState() != ProcessState::Aborted)
    {
        return fail_base + 2;
    }
    if (machine.machineState() != MachineState::Stopped)
    {
        return fail_base + 3;
    }
    if (machine.motionState() == MotionState::Moving)
    {
        return fail_base + 4;
    }
    if (!motionFrozen(machine))
    {
        return fail_base + 5;
    }
    return 0;
}

static int checkEStopInPhase(ProcedurePhase phase, int fail_base)
{
    Machine machine;
    seed(machine);
    TestExecutor ex(machine);
    TestProgram program;
    program.add(TestPoint{12.0f, 3.0f, 0.0f});
    ex.setProgram(program);
    if (!ex.start())
    {
        return fail_base;
    }
    if (!runUntilPhase(ex, machine, phase, 2500))
    {
        return fail_base + 1;
    }
    HostInputs in = withFreshForce(HostInputs{});
    in.e_stop = true;
    machine.setHostInputs(in);
    ex.tick(0.05f);
    if (machine.machineState() != MachineState::Fault)
    {
        return fail_base + 2;
    }
    if (machine.lastFaultReason() != FaultReason::EStop)
    {
        return fail_base + 3;
    }
    if (ex.processState() != ProcessState::Aborted)
    {
        return fail_base + 4;
    }
    if (machine.motionState() == MotionState::Moving)
    {
        return fail_base + 5;
    }
    machine.resume();
    if (machine.machineState() != MachineState::Fault)
    {
        return fail_base + 6;
    }
    return 0;
}

static int checkForceFaultInPhase(ProcedurePhase phase, int fail_base)
{
    Machine machine;
    seed(machine);
    TestExecutor ex(machine);
    TestProgram program;
    program.add(TestPoint{12.0f, 3.0f, 0.0f});
    ex.setProgram(program);
    if (!ex.start())
    {
        return fail_base;
    }
    if (!runUntilPhase(ex, machine, phase, 2500))
    {
        return fail_base + 1;
    }
    HostInputs in = withFreshForce(HostInputs{});
    in.force.fx = 50.0f;
    machine.setHostInputs(in);
    ex.tick(0.05f);
    if (machine.machineState() != MachineState::Fault)
    {
        return fail_base + 2;
    }
    if (ex.processState() != ProcessState::Aborted)
    {
        return fail_base + 3;
    }
    if (machine.motionState() == MotionState::Moving)
    {
        return fail_base + 4;
    }
    return 0;
}

int main()
{
    const ProcedurePhase phases[] = {
        ProcedurePhase::MoveSafePose, ProcedurePhase::PointApproach, ProcedurePhase::PreClampXyCorrect,
        ProcedurePhase::Clamp,        ProcedurePhase::InsertInitial, ProcedurePhase::OptimizeInsert,
        ProcedurePhase::TestOrAge,    ProcedurePhase::Unclamp,       ProcedurePhase::MoveSafeEnd,
    };

    int code = 0;
    int base = 10;
    for (ProcedurePhase phase : phases)
    {
        code = checkStopInPhase(phase, base);
        if (code != 0)
        {
            return code;
        }
        base += 10;
    }

    base = 200;
    for (ProcedurePhase phase : phases)
    {
        code = checkEStopInPhase(phase, base);
        if (code != 0)
        {
            return code;
        }
        base += 10;
    }

    base = 400;
    for (ProcedurePhase phase : phases)
    {
        code = checkForceFaultInPhase(phase, base);
        if (code != 0)
        {
            return code;
        }
        base += 10;
    }

    // Phase-2 iteration abort must stop motion (not leave Machine Active).
    {
        Machine machine;
        seed(machine);
        MachineConfig mcfg = machine.machineConfig();
        mcfg.phase2.max_optimization_iterations = 2u;
        mcfg.phase2.force_tolerance_N = 0.2f;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!machine.trySetMachineConfig(mcfg, err))
        {
            return 600;
        }
        TestConfiguration tcfg = machine.testConfiguration();
        tcfg.target_force_N = 10.0f;
        tcfg.mode = TestMode::Aging;
        if (!machine.trySetTestConfiguration(tcfg, err))
        {
            return 601;
        }
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{2.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        if (!ex.start())
        {
            return 602;
        }
        for (int i = 0; i < 2500 && ex.processState() == ProcessState::Running; ++i)
        {
            HostInputs in{};
            if (ex.procedurePhase() == ProcedurePhase::InsertInitial)
            {
                in.force.fz = 10.0f;
            }
            machine.setHostInputs(withPlantInputs(machine, in));
            ex.tick(0.05f);
        }
        if (ex.processState() != ProcessState::Aborted)
        {
            return 603;
        }
        if (machine.machineState() != MachineState::Stopped)
        {
            return 604;
        }
        if (machine.motionState() == MotionState::Moving)
        {
            return 605;
        }
    }

    return 0;
}
