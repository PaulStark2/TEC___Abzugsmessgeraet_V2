#include "ForceSample.hpp"
#include "HostInputs.hpp"
#include "Machine.hpp"
#include "MachineConfig.hpp"
#include "TestConfiguration.hpp"
#include "TestExecutor.hpp"
#include "TestPoint.hpp"
#include "TestProgram.hpp"
#include "Types.hpp"

#include <cmath>

static bool nearEqual(float a, float b, float eps = 1e-2f)
{
    return std::fabs(a - b) <= eps;
}

static void seedPlate(Machine& machine)
{
    machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
    homeMachine(machine);
}

static HostInputs makeInputs(float fx = 0.0f, float fy = 0.0f, float fz = 0.0f)
{
    HostInputs in{};
    in.force.fx = fx;
    in.force.fy = fy;
    in.force.fz = fz;
    return in;
}

static bool runUntilPhase(TestExecutor& ex, Machine& machine, ProcedurePhase phase, int max_steps,
                          float fz = 0.0f)
{
    machine.setHostInputs(withPlantInputs(machine, makeInputs(0.0f, 0.0f, fz)));
    for (int i = 0; i < max_steps && ex.processState() == ProcessState::Running; ++i)
    {
        if (ex.procedurePhase() == phase)
        {
            return true;
        }
        machine.setHostInputs(withPlantInputs(machine, makeInputs(0.0f, 0.0f, fz)));
        ex.tick(0.05f);
    }
    return ex.procedurePhase() == phase;
}

static bool runInsertInitialAndGetInsertionZ(float probe_search_distance, float insert_search_distance,
                                              float target_force_N, float& insertion_z_out)
{
    Machine machine;
    seedPlate(machine);

    // Configure only the two parameters under test.
    {
        MachineConfig mcfg = machine.machineConfig();
        mcfg.probe.search_distance = probe_search_distance;
        mcfg.force.insert_search_distance = insert_search_distance;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!machine.trySetMachineConfig(mcfg, err))
        {
            return false;
        }
    }

    TestConfiguration cfg = machine.testConfiguration();
    cfg.target_force_N = target_force_N;
    cfg.mode = TestMode::Aging;
    cfg.iterations = 1u;
    ConfigValidationError err = ConfigValidationError::Ok;
    if (!machine.trySetTestConfiguration(cfg, err))
    {
        return false;
    }

    TestExecutor ex(machine);
    TestProgram program;
    program.add(TestPoint{2.0f, 0.0f, 0.0f});
    ex.setProgram(program);
    if (!ex.start())
    {
        return false;
    }

    if (!runUntilPhase(ex, machine, ProcedurePhase::InsertInitial, 2500))
    {
        return false;
    }

    // InsertInitial aborts if motionSettled() becomes true while fz is still
    // below target. ProcedureController runs `machine_.tick()` before
    // `handleInsertInitial()`, so we must approximate the moment where Z is
    // close enough using the current machine.z() before the tick.
    const float z_entry = machine.z();
    const float commanded_insertion_z = z_entry - insert_search_distance;
    const float fz_target = target_force_N;

    for (int i = 0; i < 2500 && ex.processState() == ProcessState::Running; ++i)
    {
        // Use a wider margin so fz is already >=target when
        // handleInsertInitial() observes motionSettled() in the same tick.
        const float fz = (machine.z() <= commanded_insertion_z + 0.8f) ? fz_target : 0.0f;
        machine.setHostInputs(withPlantInputs(machine, makeInputs(0.0f, 0.0f, fz)));
        ex.tick(0.05f);
        if (ex.procedurePhase() == ProcedurePhase::OptimizeInsert ||
            ex.procedurePhase() == ProcedurePhase::TestOrAge)
        {
            insertion_z_out = ex.procedure().insertionZ();
            return true;
        }
    }

    return false;
}

int main()
{
    // Phase 1: Fz reaching target stores InsertionPoint and ExtractionPoint = Z + buffer.
    {
        Machine machine;
        seedPlate(machine);
        TestConfiguration cfg = machine.testConfiguration();
        cfg.target_force_N = 5.0f;
        cfg.mode = TestMode::Aging;
        cfg.iterations = 1u;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!machine.trySetTestConfiguration(cfg, err))
        {
            return 1;
        }

        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{2.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        if (!ex.start())
        {
            return 2;
        }

        if (!runUntilPhase(ex, machine, ProcedurePhase::InsertInitial, 2500))
        {
            return 3;
        }

        float captured_z = 0.0f;
        bool captured = false;
        for (int i = 0; i < 400 && ex.processState() == ProcessState::Running; ++i)
        {
            const float fz = (machine.z() < -0.4f) ? 5.0f : 0.0f;
            machine.setHostInputs(withPlantInputs(machine, makeInputs(0.0f, 0.0f, fz)));
            if (!captured && fz >= 5.0f)
            {
                captured_z = machine.z();
                captured = true;
            }
            ex.tick(0.05f);
            if (ex.procedurePhase() == ProcedurePhase::OptimizeInsert ||
                ex.procedurePhase() == ProcedurePhase::TestOrAge)
            {
                break;
            }
        }

        if (!captured)
        {
            return 4;
        }
        if (!nearEqual(ex.procedure().insertionZ(), captured_z, 0.15f))
        {
            return 5;
        }
        const float buffer = machine.machineConfig().force.insertion_buffer;
        if (!nearEqual(ex.procedure().extractionZ(), ex.procedure().insertionZ() + buffer, 0.15f))
        {
            return 6;
        }
        if (ex.pauseAllowed())
        {
            return 7;
        }
    }

    // Regression: probe.search_distance must not affect InsertInitial depth
    // (insert depth is force.insert_search_distance).
    {
        const float target_force = 5.0f;
        float ins_a = 0.0f;
        float ins_b = 0.0f;
        if (!runInsertInitialAndGetInsertionZ(5.0f, 10.0f, target_force, ins_a))
        {
            return 500;
        }
        if (!runInsertInitialAndGetInsertionZ(25.0f, 10.0f, target_force, ins_b))
        {
            return 501;
        }
        if (!nearEqual(ins_a, ins_b, 0.05f))
        {
            return 502;
        }
    }

    // Regression: insert_search_distance must affect InsertInitial depth.
    {
        const float target_force = 5.0f;
        float ins_a = 0.0f;
        float ins_b = 0.0f;
        if (!runInsertInitialAndGetInsertionZ(20.0f, 10.0f, target_force, ins_a))
        {
            return 510;
        }
        if (!runInsertInitialAndGetInsertionZ(20.0f, 12.0f, target_force, ins_b))
        {
            return 511;
        }
        // Deeper insert_search_distance moves insertion_z by roughly the same amount.
        if (!nearEqual(ins_a - ins_b, 2.0f, 0.2f))
        {
            return 512;
        }
    }

    // Phase 1: search end without Sollkraft is a fault, not InsertionPoint.
    {
        Machine machine;
        seedPlate(machine);
        TestConfiguration cfg = machine.testConfiguration();
        cfg.target_force_N = 8.0f;
        cfg.mode = TestMode::Aging;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!machine.trySetTestConfiguration(cfg, err))
        {
            return 32;
        }
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{1.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        if (!ex.start())
        {
            return 33;
        }
        if (!runUntilPhase(ex, machine, ProcedurePhase::InsertInitial, 2500))
        {
            return 34;
        }
        for (int i = 0; i < 800 && ex.processState() == ProcessState::Running; ++i)
        {
            machine.setHostInputs(withPlantInputs(machine, makeInputs(0.0f, 0.0f, 0.0f)));
            ex.tick(0.05f);
        }
        if (ex.processState() != ProcessState::Aborted)
        {
            return 35;
        }
        if (machine.machineState() != MachineState::Stopped)
        {
            return 36;
        }
        if (machine.motionState() == MotionState::Moving)
        {
            return 37;
        }
    }

    // Phase 1: Z-min before Sollkraft aborts.
    {
        Machine machine;
        seedPlate(machine);
        TestConfiguration cfg = machine.testConfiguration();
        cfg.target_force_N = 8.0f;
        cfg.mode = TestMode::Aging;
        ConfigValidationError err = ConfigValidationError::Ok;
        machine.trySetTestConfiguration(cfg, err);
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{1.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        if (!ex.start())
        {
            return 38;
        }
        if (!runUntilPhase(ex, machine, ProcedurePhase::InsertInitial, 2500))
        {
            return 39;
        }
        HostInputs blocked = withPlantInputs(machine, makeInputs(0.0f, 0.0f, 0.0f));
        blocked.limitZMin = true;
        machine.setHostInputs(blocked);
        ex.tick(0.05f);
        if (ex.processState() != ProcessState::Aborted)
        {
            return 40;
        }
        if (machine.motionState() == MotionState::Moving)
        {
            return 41;
        }
    }

    // Phase 2: too-low force moves insertion deeper (Z-); max attempts abort.
    {
        Machine machine;
        seedPlate(machine);
        MachineConfig mcfg = machine.machineConfig();
        mcfg.phase2.max_optimization_iterations = 2u;
        mcfg.phase2.z_correction_gain = 0.2f;
        mcfg.phase2.z_correction_max = 1.0f;
        mcfg.phase2.force_tolerance_N = 0.2f;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!machine.trySetMachineConfig(mcfg, err))
        {
            return 8;
        }
        TestConfiguration tcfg = machine.testConfiguration();
        tcfg.target_force_N = 10.0f;
        tcfg.mode = TestMode::Aging;
        if (!machine.trySetTestConfiguration(tcfg, err))
        {
            return 9;
        }

        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{1.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        if (!ex.start())
        {
            return 10;
        }
        if (!runUntilPhase(ex, machine, ProcedurePhase::InsertInitial, 2500))
        {
            return 11;
        }
        for (int i = 0; i < 800 && ex.processState() == ProcessState::Running; ++i)
        {
            const float fz = (ex.procedurePhase() == ProcedurePhase::InsertInitial) ? 10.0f : 0.0f;
            machine.setHostInputs(withPlantInputs(machine, makeInputs(0.0f, 0.0f, fz)));
            ex.tick(0.05f);
        }
        if (ex.processState() != ProcessState::Aborted)
        {
            return 12;
        }
        if (ex.procedurePhase() != ProcedurePhase::Aborted)
        {
            return 13;
        }
    }

    // Phase 2 success: force within tolerance proceeds to TestOrAge.
    {
        Machine machine;
        seedPlate(machine);
        TestConfiguration tcfg = machine.testConfiguration();
        tcfg.target_force_N = 8.0f;
        tcfg.mode = TestMode::Aging;
        tcfg.iterations = 1u;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!machine.trySetTestConfiguration(tcfg, err))
        {
            return 14;
        }

        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{1.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        if (!ex.start())
        {
            return 15;
        }

        for (int i = 0; i < 2500 && ex.processState() == ProcessState::Running; ++i)
        {
            const float fz = (ex.procedurePhase() == ProcedurePhase::InsertInitial && machine.z() < -0.2f)
                                 ? 8.0f
                                 : (ex.procedurePhase() == ProcedurePhase::OptimizeInsert ? 8.0f : 0.0f);
            machine.setHostInputs(withPlantInputs(machine, makeInputs(0.0f, 0.0f, fz)));
            ex.tick(0.05f);
            if (ex.procedurePhase() == ProcedurePhase::TestOrAge)
            {
                break;
            }
        }
        if (ex.procedurePhase() != ProcedurePhase::TestOrAge &&
            ex.processState() != ProcessState::Completed)
        {
            return 16;
        }
    }

    // STOP during InsertInitial.
    {
        Machine machine;
        seedPlate(machine);
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{4.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        ex.start();
        if (!runUntilPhase(ex, machine, ProcedurePhase::InsertInitial, 2500))
        {
            return 17;
        }
        ex.stop();
        if (ex.processState() != ProcessState::Aborted)
        {
            return 18;
        }
        if (machine.machineState() != MachineState::Stopped)
        {
            return 19;
        }
    }

    // Safety fault during InsertInitial.
    {
        Machine machine;
        seedPlate(machine);
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{4.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        ex.start();
        if (!runUntilPhase(ex, machine, ProcedurePhase::InsertInitial, 2500))
        {
            return 20;
        }
        machine.setHostInputs(withPlantInputs(machine, makeInputs(0.0f, 0.0f, 50.0f)));
        ex.tick(0.05f);
        if (machine.machineState() != MachineState::Fault)
        {
            return 21;
        }
        if (ex.processState() != ProcessState::Aborted)
        {
            return 22;
        }
    }

    // Aging: no force-path samples. Test: recorder fills.
    {
        Machine machine;
        seedPlate(machine);
        TestConfiguration tcfg = machine.testConfiguration();
        tcfg.target_force_N = 0.1f;
        tcfg.mode = TestMode::Aging;
        tcfg.iterations = 2u;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!machine.trySetTestConfiguration(tcfg, err))
        {
            return 23;
        }
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{1.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        ex.start();
        for (int i = 0; i < 2500 && ex.processState() == ProcessState::Running; ++i)
        {
            machine.setHostInputs(withProcessForce(ex, machine));
            ex.tick(0.05f);
        }
        if (ex.processState() != ProcessState::Completed)
        {
            return 24;
        }
        if (ex.procedure().forcePath().size() != 0)
        {
            return 25;
        }
    }

    {
        Machine machine;
        seedPlate(machine);
        TestConfiguration tcfg = machine.testConfiguration();
        tcfg.target_force_N = 0.1f;
        tcfg.mode = TestMode::Test;
        tcfg.iterations = 2u;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!machine.trySetTestConfiguration(tcfg, err))
        {
            return 26;
        }
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{1.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        ex.start();
        bool saw_samples = false;
        for (int i = 0; i < 2500 && ex.processState() == ProcessState::Running; ++i)
        {
            machine.setHostInputs(withProcessForce(ex, machine));
            ex.tick(0.05f);
            if (ex.procedure().forcePath().size() > 0)
            {
                saw_samples = true;
            }
        }
        if (ex.processState() != ProcessState::Completed)
        {
            return 27;
        }
        if (!saw_samples)
        {
            return 28;
        }
    }

    // Pause rejected in TestOrAge.
    {
        Machine machine;
        seedPlate(machine);
        TestConfiguration tcfg = machine.testConfiguration();
        tcfg.target_force_N = 0.1f;
        tcfg.mode = TestMode::Aging;
        tcfg.iterations = 3u;
        ConfigValidationError err = ConfigValidationError::Ok;
        machine.trySetTestConfiguration(tcfg, err);
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{1.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        ex.start();
        if (!runUntilPhase(ex, machine, ProcedurePhase::TestOrAge, 2500, 0.1f))
        {
            return 29;
        }
        if (ex.pauseAllowed())
        {
            return 30;
        }
        ex.pause();
        if (machine.machineState() == MachineState::Paused)
        {
            return 31;
        }
    }

    return 0;
}
