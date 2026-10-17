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
    (void)calibrateProbeCenter(machine);
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

    if (!runUntilPhase(ex, machine, ProcedurePhase::InsertInitial, 12000))
    {
        return false;
    }

    // InsertInitial aborts if motionSettled() becomes true while fz is still
    // below target. ProcedureController runs `machine_.tick()` before
    // `handleInsertInitial()`, so we must approximate the moment where Z is
    // close enough using the current machine.z() before the tick.
    const float z_entry = machine.z();
    const float fz_target = target_force_N;

    for (int i = 0; i < 4000 && ex.processState() == ProcessState::Running; ++i)
    {
        const float insert_end = ex.procedure().forceFreeZ() - insert_search_distance;
        const float fz =
            (ex.procedure().cycleLeg() >= 2 && machine.z() <= insert_end + 0.8f) ? fz_target : 0.0f;
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

        if (!runUntilPhase(ex, machine, ProcedurePhase::InsertInitial, 12000))
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
        // Phase 1: ExtractionPoint = force-free Z + insertion_buffer (set when leaving
        // force-free / starting insert). When already free at approach Z, that is
        // approach_z + buffer — not necessarily insertion_z + buffer.
        if (!(ex.procedure().extractionZ() > ex.procedure().insertionZ() - 1.0e-3f))
        {
            return 6;
        }
        if (ex.procedure().extractionZ() + 1.0e-3f < captured_z + buffer - 0.5f &&
            !nearEqual(ex.procedure().extractionZ(), ex.procedure().insertionZ() + buffer, 0.25f))
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
        if (!runUntilPhase(ex, machine, ProcedurePhase::InsertInitial, 12000))
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
        if (!runUntilPhase(ex, machine, ProcedurePhase::InsertInitial, 12000))
        {
            return 39;
        }
        for (int i = 0; i < 400 && ex.processState() == ProcessState::Running; ++i)
        {
            HostInputs in = withPlantInputs(machine, makeInputs(0.0f, 0.0f, 0.0f));
            if (ex.procedure().cycleLeg() >= 2)
            {
                in.limitZMin = true;
            }
            machine.setHostInputs(in);
            ex.tick(0.05f);
            if (ex.processState() == ProcessState::Aborted)
            {
                break;
            }
        }
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
        if (!runUntilPhase(ex, machine, ProcedurePhase::InsertInitial, 12000))
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

        for (int i = 0; i < 4000 && ex.processState() == ProcessState::Running; ++i)
        {
            float fz = 0.0f;
            if (ex.procedurePhase() == ProcedurePhase::OptimizeInsert)
            {
                fz = 8.0f;
            }
            else if (ex.procedurePhase() == ProcedurePhase::InsertInitial && machine.z() < -0.05f)
            {
                fz = 8.0f;
            }
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
        if (!runUntilPhase(ex, machine, ProcedurePhase::InsertInitial, 12000))
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
        if (!runUntilPhase(ex, machine, ProcedurePhase::InsertInitial, 12000))
        {
            return 20;
        }
        machine.setHostInputs(withPlantInputs(machine, makeInputs(0.0f, 0.0f, 50.1f)));
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
        for (int i = 0; i < 12000 && ex.processState() == ProcessState::Running; ++i)
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
        for (int i = 0; i < 12000 && ex.processState() == ProcessState::Running; ++i)
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
        bool reached = false;
        for (int i = 0; i < 12000 && ex.processState() == ProcessState::Running; ++i)
        {
            if (ex.procedurePhase() == ProcedurePhase::TestOrAge)
            {
                reached = true;
                break;
            }
            const ProcedurePhase ph = ex.procedurePhase();
            const float fz = (ph == ProcedurePhase::InsertInitial || ph == ProcedurePhase::OptimizeInsert ||
                              ph == ProcedurePhase::TestOrAge)
                                 ? 0.1f
                                 : 0.0f;
            machine.setHostInputs(withPlantInputs(machine, makeInputs(0.0f, 0.0f, fz)));
            ex.tick(0.05f);
        }
        if (!reached && ex.procedurePhase() != ProcedurePhase::TestOrAge)
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

    // Hybrid Phase 1: preload force at Clamp→InsertInitial so unload leg runs first.
    {
        Machine machine;
        seedPlate(machine);
        MachineConfig mcfg = machine.machineConfig();
        mcfg.force.insert_search_distance = 8.0f;
        mcfg.phase2.force_tolerance_N = 0.5f;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!machine.trySetMachineConfig(mcfg, err))
        {
            return 40;
        }
        TestConfiguration cfg = machine.testConfiguration();
        cfg.target_force_N = 5.0f;
        cfg.mode = TestMode::Aging;
        cfg.iterations = 1u;
        if (!machine.trySetTestConfiguration(cfg, err))
        {
            return 41;
        }

        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{2.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        if (!ex.start())
        {
            return 42;
        }
        if (!runUntilPhase(ex, machine, ProcedurePhase::Clamp, 3000))
        {
            return 43;
        }

        // Keep preload while Clamp finishes so enterPhase(InsertInitial) sees force.
        // The rest tare between Clamp and InsertInitial must sample 0 N.
        float z_at_insert = 0.0f;
        bool entered_insert = false;
        bool saw_extract = false;
        bool reached_optimize = false;
        for (int i = 0; i < 1200 && ex.processState() == ProcessState::Running; ++i)
        {
            float fz = 0.0f;
            if (ex.procedurePhase() == ProcedurePhase::InsertInitial)
            {
                fz = 3.0f;
                if (!entered_insert)
                {
                    entered_insert = true;
                    z_at_insert = machine.z();
                }
                if (machine.z() > z_at_insert + 0.25f)
                {
                    saw_extract = true;
                    fz = 0.0f;
                }
                else if (saw_extract && machine.z() < z_at_insert - 0.25f)
                {
                    fz = 5.0f;
                }
                else if (saw_extract)
                {
                    fz = 0.0f;
                }
            }
            machine.setHostInputs(withPlantInputs(machine, makeInputs(0.0f, 0.0f, fz)));
            ex.tick(0.05f);
            if (ex.procedurePhase() == ProcedurePhase::OptimizeInsert ||
                ex.procedurePhase() == ProcedurePhase::TestOrAge)
            {
                reached_optimize = true;
                break;
            }
        }
        if (!entered_insert || !saw_extract || !reached_optimize)
        {
            return 44;
        }
    }

    // PointApproach: coarse Z stops at program.z + approach_distance before PreClamp.
    {
        Machine machine;
        seedPlate(machine);
        MachineConfig mcfg = machine.machineConfig();
        mcfg.probe.approach_distance = 15.0f;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!machine.trySetMachineConfig(mcfg, err))
        {
            return 50;
        }
        TestConfiguration cfg = machine.testConfiguration();
        cfg.target_force_N = 0.1f;
        cfg.mode = TestMode::Aging;
        if (!machine.trySetTestConfiguration(cfg, err))
        {
            return 51;
        }
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{3.0f, 0.0f, 2.0f});
        ex.setProgram(program);
        if (!ex.start())
        {
            return 52;
        }
        bool saw_standoff = false;
        for (int i = 0; i < 4000 && ex.processState() == ProcessState::Running; ++i)
        {
            machine.setHostInputs(withPlantInputs(machine, makeInputs()));
            ex.tick(0.05f);
            if (ex.procedurePhase() == ProcedurePhase::PointApproach)
            {
                // During coarse leg TCP should not yet be at program Z=2.
                if (machine.z() > 2.0f + 10.0f && machine.z() < 2.0f + 16.0f)
                {
                    saw_standoff = true;
                }
            }
            if (ex.procedurePhase() == ProcedurePhase::PreClampXyCorrect ||
                ex.procedurePhase() == ProcedurePhase::Clamp ||
                ex.procedurePhase() == ProcedurePhase::InsertInitial)
            {
                break;
            }
        }
        if (!saw_standoff)
        {
            return 53;
        }
    }

    // Phase 2: F too high at insertion → insertion_z moves +Z (less insert); then accept.
    {
        Machine machine;
        seedPlate(machine);
        MachineConfig mcfg = machine.machineConfig();
        mcfg.phase2.force_tolerance_N = 0.5f;
        mcfg.phase2.z_correction_gain = 0.5f;
        mcfg.phase2.z_correction_max = 2.0f;
        mcfg.phase2.minimum_correction = 0.01f;
        mcfg.phase2.max_optimization_iterations = 8u;
        mcfg.phase2.max_sign_flips = 8u;
        mcfg.motion.phase2_v_insert = 5.0f;
        mcfg.motion.phase2_v_extract = 3.0f;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!machine.trySetMachineConfig(mcfg, err))
        {
            return 60;
        }
        TestConfiguration cfg = machine.testConfiguration();
        cfg.target_force_N = 10.0f;
        cfg.mode = TestMode::Aging;
        cfg.iterations = 1u;
        if (!machine.trySetTestConfiguration(cfg, err))
        {
            return 61;
        }

        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{2.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        if (!ex.start())
        {
            return 62;
        }
        if (!runUntilPhase(ex, machine, ProcedurePhase::InsertInitial, 8000))
        {
            return 63;
        }

        float z_at_insert = machine.z();
        bool saw_extract = false;
        for (int i = 0; i < 4000 && ex.processState() == ProcessState::Running; ++i)
        {
            float fz = ex.procedurePhase() == ProcedurePhase::ForceTare ? 0.0f : 5.0f;
            if (ex.procedurePhase() == ProcedurePhase::InsertInitial)
            {
                if (machine.z() > z_at_insert + 0.2f)
                {
                    saw_extract = true;
                    fz = 0.0f;
                }
                else if (saw_extract && machine.z() <= z_at_insert + 0.05f)
                {
                    fz = 10.0f;
                }
                else if (saw_extract)
                {
                    fz = 0.0f;
                }
            }
            machine.setHostInputs(withPlantInputs(machine, makeInputs(0.0f, 0.0f, fz)));
            ex.tick(0.05f);
            if (ex.procedurePhase() == ProcedurePhase::OptimizeInsert)
            {
                break;
            }
        }
        if (ex.procedurePhase() != ProcedurePhase::OptimizeInsert)
        {
            return 64;
        }

        const float insertion_before = ex.procedure().insertionZ();
        bool saw_insert_leg = false;
        bool saw_extract_leg = false;
        int motion_ticks = 0;
        float z_prev = machine.z();
        unsigned high_force_iters = 1u; // first settle reports too-high force once

        for (int i = 0; i < 6000 && ex.processState() == ProcessState::Running; ++i)
        {
            if (ex.procedurePhase() == ProcedurePhase::ForceTare)
            {
                machine.setHostInputs(withPlantInputs(machine, makeInputs()));
                ex.tick(0.05f);
                continue;
            }
            if (ex.procedurePhase() != ProcedurePhase::OptimizeInsert)
            {
                break;
            }
            const uint8_t leg = static_cast<uint8_t>(ex.procedure().cycleLeg());
            float fz = 0.0f;
            if (leg == 0)
            {
                saw_insert_leg = true;
                const float iz = ex.procedure().insertionZ();
                const bool near_insert = std::fabs(machine.z() - iz) < 0.25f;
                if (near_insert && high_force_iters > 0u)
                {
                    fz = 20.0f; // too high → expect +Z correction
                }
                else if (near_insert)
                {
                    fz = 10.0f; // after correction: accept
                }
                else
                {
                    fz = 4.0f;
                }
                if (std::fabs(machine.z() - z_prev) > 1e-4f)
                {
                    ++motion_ticks;
                }
            }
            else
            {
                saw_extract_leg = true;
                fz = 0.0f;
                if (high_force_iters > 0u && ex.procedure().insertionZ() > insertion_before + 0.05f)
                {
                    high_force_iters = 0u;
                }
            }
            z_prev = machine.z();
            machine.setHostInputs(withPlantInputs(machine, makeInputs(0.0f, 0.0f, fz)));
            ex.tick(0.05f);
        }

        if (!saw_insert_leg || !saw_extract_leg || motion_ticks < 3)
        {
            return 65;
        }
        if (!(ex.procedure().insertionZ() > insertion_before + 0.05f))
        {
            return 66; // F too high must raise insertion_z
        }
        if (ex.procedurePhase() != ProcedurePhase::TestOrAge &&
            ex.procedurePhase() != ProcedurePhase::Unclamp &&
            ex.procedurePhase() != ProcedurePhase::MoveSafeEnd &&
            ex.procedurePhase() != ProcedurePhase::Completed)
        {
            return 68;
        }
    }

    return 0;
}
