#include "ForceSample.hpp"
#include "Machine.hpp"
#include "SafeZ.hpp"
#include "TestExecutor.hpp"
#include "TestPoint.hpp"
#include "TestProgram.hpp"
#include "Types.hpp"

#include <cmath>

static bool nearEqual(float a, float b, float eps = 1.0e-3f)
{
    return std::fabs(a - b) <= eps;
}

static bool expectedSafeZMachine(const Machine& machine, const TestProgram& program, float& out)
{
    return resolveProgramSafeZMachine(program, machine.machineConfig().motion.safe_z_buffer,
                                      machine.calibrationSet(), out);
}

static void prepare(Machine& machine)
{
    machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
    TestConfiguration cfg = machine.testConfiguration();
    cfg.target_force_N = 0.1f;
    cfg.mode = TestMode::Aging;
    cfg.iterations = 1u;
    ConfigValidationError err = ConfigValidationError::Ok;
    machine.trySetTestConfiguration(cfg, err);
    homeMachine(machine);
    (void)calibrateProbeCenter(machine);
}

int main()
{
    // Test A — simple: max(0,5,10) + 30 = 40
    {
        TestProgram program;
        program.add(TestPoint{0.0f, 0.0f, 0.0f});
        program.add(TestPoint{0.0f, 0.0f, 5.0f});
        program.add(TestPoint{0.0f, 0.0f, 10.0f});
        float max_z = 0.0f;
        float plate = 0.0f;
        if (!computeSafeZPlate(program, 30.0f, max_z, plate) || !nearEqual(max_z, 10.0f) ||
            !nearEqual(plate, 40.0f))
        {
            return 1;
        }
    }

    // Test B — negative Z: max(-10,-5,-2) + 30 = 28
    {
        TestProgram program;
        program.add(TestPoint{0.0f, 0.0f, -10.0f});
        program.add(TestPoint{0.0f, 0.0f, -5.0f});
        program.add(TestPoint{0.0f, 0.0f, -2.0f});
        float max_z = 0.0f;
        float plate = 0.0f;
        if (!computeSafeZPlate(program, 30.0f, max_z, plate) || !nearEqual(max_z, -2.0f) ||
            !nearEqual(plate, 28.0f))
        {
            return 2;
        }
    }

    // Test C — program change 10→40, 25→55
    {
        TestProgram a;
        a.add(TestPoint{0.0f, 0.0f, 10.0f});
        TestProgram b;
        b.add(TestPoint{0.0f, 0.0f, 25.0f});
        float max_z = 0.0f;
        float plate_a = 0.0f;
        float plate_b = 0.0f;
        if (!computeSafeZPlate(a, 30.0f, max_z, plate_a) || !nearEqual(plate_a, 40.0f))
        {
            return 3;
        }
        if (!computeSafeZPlate(b, 30.0f, max_z, plate_b) || !nearEqual(plate_b, 55.0f))
        {
            return 4;
        }
    }

    // Test D — buffer change 30→40, 50→60
    {
        TestProgram program;
        program.add(TestPoint{0.0f, 0.0f, 10.0f});
        float max_z = 0.0f;
        float plate30 = 0.0f;
        float plate50 = 0.0f;
        if (!computeSafeZPlate(program, 30.0f, max_z, plate30) || !nearEqual(plate30, 40.0f))
        {
            return 5;
        }
        if (!computeSafeZPlate(program, 50.0f, max_z, plate50) || !nearEqual(plate50, 60.0f))
        {
            return 6;
        }
    }

    // Test E — empty program → no Safe-Z → start rejected
    {
        Machine machine;
        prepare(machine);
        TestExecutor ex(machine);
        TestProgram empty;
        ex.setProgram(empty);
        if (ex.start())
        {
            return 7;
        }
        float unused = 0.0f;
        if (resolveProgramSafeZMachine(empty, 30.0f, machine.calibrationSet(), unused))
        {
            return 8;
        }
    }

    // Test F — plate_origin.z does not change safe_z_plate
    {
        TestProgram program;
        program.add(TestPoint{1.0f, 2.0f, 10.0f});
        float max_z = 0.0f;
        float plate = 0.0f;
        if (!computeSafeZPlate(program, 30.0f, max_z, plate) || !nearEqual(plate, 40.0f))
        {
            return 9;
        }
        CalibrationSet c0{};
        c0.plate_origin_valid = true;
        c0.plate_origin = TestPoint{0.0f, 0.0f, 0.0f};
        CalibrationSet c17{};
        c17.plate_origin_valid = true;
        c17.plate_origin = TestPoint{0.0f, 0.0f, 17.0f};
        float m0 = 0.0f;
        float m17 = 0.0f;
        if (!computeSafeZMachine(c0, plate, m0) || !nearEqual(m0, 40.0f))
        {
            return 10;
        }
        if (!computeSafeZMachine(c17, plate, m17) || !nearEqual(m17, 57.0f))
        {
            return 11;
        }
    }

    // Test G — tool_offset flows once into machine Z
    {
        TestProgram program;
        program.add(TestPoint{0.0f, 0.0f, 10.0f});
        CalibrationSet calib{};
        calib.plate_origin_valid = true;
        calib.plate_origin = TestPoint{0.0f, 0.0f, 5.0f};
        calib.tool_offset = TestPoint{0.0f, 0.0f, 2.0f};
        float machine_z = 0.0f;
        if (!resolveProgramSafeZMachine(program, 30.0f, calib, machine_z) ||
            !nearEqual(machine_z, 47.0f))
        {
            return 12;
        }
    }

    // Test H — Raise → XY → Lower with dynamic Safe-Z
    {
        Machine machine;
        prepare(machine);
        MachineConfig mcfg = machine.machineConfig();
        mcfg.motion.safe_z_buffer = 5.0f;
        mcfg.motion.v_approach = 20.0f;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!machine.trySetMachineConfig(mcfg, err))
        {
            return 20;
        }
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{6.0f, 4.0f, -1.0f});
        ex.setProgram(program);
        float safe_z_machine = 0.0f;
        if (!expectedSafeZMachine(machine, program, safe_z_machine) ||
            !nearEqual(safe_z_machine, 4.0f)) // -1 + 5
        {
            return 21;
        }
        if (!ex.start())
        {
            return 22;
        }

        bool saw_xy_below_safe = false;
        bool reached_xy = false;
        bool reached_z = false;
        float prev_x = machine.x();
        float prev_y = machine.y();
        for (int i = 0; i < 8000 && ex.processState() == ProcessState::Running; ++i)
        {
            machine.setHostInputs(withProcessForce(ex, machine));
            ex.tick(0.05f);
            const float x = machine.x();
            const float y = machine.y();
            const float z = machine.z();
            const bool xy_step = std::fabs(x - prev_x) > 1.0e-4f || std::fabs(y - prev_y) > 1.0e-4f;
            const bool process_xy = ex.procedurePhase() == ProcedurePhase::PointApproach ||
                                    ex.procedurePhase() == ProcedurePhase::MoveSafePose;
            if (xy_step && process_xy && z < safe_z_machine - 0.2f)
            {
                saw_xy_below_safe = true;
            }
            prev_x = x;
            prev_y = y;
            if (nearEqual(machine.x(), 6.0f, 0.15f) && nearEqual(machine.y(), 4.0f, 0.15f) &&
                nearEqual(machine.z(), safe_z_machine, 0.15f))
            {
                reached_xy = true;
            }
            if (nearEqual(machine.x(), 6.0f, 0.15f) && nearEqual(machine.y(), 4.0f, 0.15f) &&
                nearEqual(machine.z(), -1.0f, 0.2f))
            {
                reached_z = true;
                break;
            }
        }
        if (saw_xy_below_safe)
        {
            return 23;
        }
        if (!reached_xy || !reached_z)
        {
            return 24;
        }
        ex.stop();
    }

    // Point change: raise to Safe-Z before XY to the next point.
    {
        Machine machine;
        prepare(machine);
        MachineConfig mcfg = machine.machineConfig();
        mcfg.motion.safe_z_buffer = 5.0f;
        mcfg.motion.v_approach = 20.0f;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!machine.trySetMachineConfig(mcfg, err))
        {
            return 30;
        }
        TestConfiguration tcfg = machine.testConfiguration();
        tcfg.target_force_N = 0.1f;
        tcfg.mode = TestMode::Aging;
        tcfg.iterations = 1u;
        machine.trySetTestConfiguration(tcfg, err);

        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{3.0f, 0.0f, -0.5f});
        program.add(TestPoint{9.0f, 0.0f, -0.5f});
        ex.setProgram(program);
        float safe_z_machine = 0.0f;
        if (!expectedSafeZMachine(machine, program, safe_z_machine))
        {
            return 31;
        }
        if (!ex.start())
        {
            return 32;
        }

        bool saw_second_xy_low = false;
        bool reached_second = false;
        float prev_x = machine.x();
        float prev_y = machine.y();
        for (int i = 0; i < 12000 && ex.processState() != ProcessState::Completed &&
                        ex.processState() != ProcessState::Aborted;
             ++i)
        {
            machine.setHostInputs(withProcessForce(ex, machine));
            ex.tick(0.05f);
            const float x = machine.x();
            const float y = machine.y();
            const float z = machine.z();
            const bool xy_step = std::fabs(x - prev_x) > 1.0e-4f || std::fabs(y - prev_y) > 1.0e-4f;
            const bool process_xy = ex.procedurePhase() == ProcedurePhase::PointApproach ||
                                    ex.procedurePhase() == ProcedurePhase::MoveSafePose ||
                                    ex.procedurePhase() == ProcedurePhase::MoveSafeEnd ||
                                    ex.procedurePhase() == ProcedurePhase::NextPoint;
            if (xy_step && process_xy && z < safe_z_machine - 0.3f)
            {
                saw_second_xy_low = true;
            }
            prev_x = x;
            prev_y = y;
            if (ex.currentIndex() == 1 && nearEqual(machine.x(), 9.0f, 0.2f) &&
                nearEqual(machine.z(), -0.5f, 0.25f))
            {
                reached_second = true;
                break;
            }
        }
        if (saw_second_xy_low)
        {
            return 33;
        }
        if (!reached_second)
        {
            return 34;
        }
        ex.stop();
    }

    // Safe-End: raise Safe-Z, then run Machine::home() (not park at Safe-Z).
    {
        Machine machine;
        prepare(machine);
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{1.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        if (!ex.start())
        {
            return 41;
        }
        bool saw_homing = false;
        bool saw_z_seek_above_safe = false;
        float safe_z_machine = 0.0f;
        if (!expectedSafeZMachine(machine, program, safe_z_machine))
        {
            return 40;
        }
        for (int i = 0; i < 8000 && ex.processState() == ProcessState::Running; ++i)
        {
            if (ex.procedurePhase() == ProcedurePhase::MoveSafeEnd &&
                machine.machineState() == MachineState::Homing)
            {
                saw_homing = true;
                if (machine.z() > safe_z_machine + 0.5f)
                {
                    saw_z_seek_above_safe = true;
                }
            }
            machine.setHostInputs(withProcessForce(ex, machine));
            ex.tick(0.05f);
        }
        if (ex.processState() != ProcessState::Completed)
        {
            return 42;
        }
        if (!saw_homing || !saw_z_seek_above_safe || !machine.isHomed())
        {
            return 43;
        }
        const TestPoint home = machine.calibrationSet().home_offset;
        if (!nearEqual(machine.x(), home.x, 0.3f) || !nearEqual(machine.y(), home.y, 0.3f) ||
            !nearEqual(machine.z(), home.z, 0.3f))
        {
            return 44;
        }
    }

    return 0;
}
