#include "ForceSample.hpp"
#include "Machine.hpp"
#include "TestExecutor.hpp"
#include "TestPoint.hpp"
#include "TestProgram.hpp"
#include "Types.hpp"

#include <cmath>

static bool nearEqual(float a, float b, float eps = 1.0e-3f)
{
    return std::fabs(a - b) <= eps;
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
}

int main()
{
    // Start rejected when a plate point sits at or above Safe-Z.
    {
        Machine machine;
        prepare(machine);
        MachineConfig mcfg = machine.machineConfig();
        mcfg.motion.safe_z = 2.0f;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!machine.trySetMachineConfig(mcfg, err))
        {
            return 1;
        }
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{1.0f, 0.0f, 2.0f});
        ex.setProgram(program);
        if (ex.start())
        {
            return 2;
        }
    }

    // First approach: Z to Safe-Z, then XY, then Z down. No XY while Z is low.
    {
        Machine machine;
        prepare(machine);
        MachineConfig mcfg = machine.machineConfig();
        mcfg.motion.safe_z = 8.0f;
        mcfg.motion.v_approach = 20.0f;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!machine.trySetMachineConfig(mcfg, err))
        {
            return 10;
        }
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{6.0f, 4.0f, -1.0f});
        ex.setProgram(program);
        if (!ex.start())
        {
            return 11;
        }

        bool saw_xy_below_safe = false;
        bool reached_xy = false;
        bool reached_z = false;
        float prev_x = machine.x();
        float prev_y = machine.y();
        for (int i = 0; i < 2500 && ex.processState() == ProcessState::Running; ++i)
        {
            machine.setHostInputs(withProcessForce(ex, machine));
            ex.tick(0.05f);
            const float x = machine.x();
            const float y = machine.y();
            const float z = machine.z();
            const bool xy_step = std::fabs(x - prev_x) > 1.0e-4f || std::fabs(y - prev_y) > 1.0e-4f;
            const bool process_xy = ex.procedurePhase() == ProcedurePhase::PointApproach ||
                                    ex.procedurePhase() == ProcedurePhase::MoveSafePose ||
                                    ex.procedurePhase() == ProcedurePhase::MoveSafeEnd;
            if (xy_step && process_xy && z < mcfg.motion.safe_z - 0.2f)
            {
                saw_xy_below_safe = true;
            }
            prev_x = x;
            prev_y = y;
            if (nearEqual(machine.x(), 6.0f, 0.15f) && nearEqual(machine.y(), 4.0f, 0.15f) &&
                nearEqual(machine.z(), mcfg.motion.safe_z, 0.15f))
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
            return 12;
        }
        if (!reached_xy || !reached_z)
        {
            return 13;
        }
        ex.stop();
    }

    // Point change: raise to Safe-Z before XY to the next point.
    {
        Machine machine;
        prepare(machine);
        MachineConfig mcfg = machine.machineConfig();
        mcfg.motion.safe_z = 7.0f;
        mcfg.motion.v_approach = 20.0f;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!machine.trySetMachineConfig(mcfg, err))
        {
            return 20;
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
        if (!ex.start())
        {
            return 21;
        }

        bool saw_second_xy_low = false;
        bool reached_second = false;
        float prev_x = machine.x();
        float prev_y = machine.y();
        for (int i = 0; i < 2500 && ex.processState() != ProcessState::Completed &&
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
            if (xy_step && process_xy && z < mcfg.motion.safe_z - 0.3f)
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
            return 22;
        }
        if (!reached_second)
        {
            return 23;
        }
        ex.stop();
    }

    // Safe-End parks at Safe-Z, not a 3D dive to home_offset.z.
    {
        Machine machine;
        prepare(machine);
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{1.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        if (!ex.start())
        {
            return 30;
        }
        for (int i = 0; i < 2000 && ex.processState() == ProcessState::Running; ++i)
        {
            machine.setHostInputs(withProcessForce(ex, machine));
            ex.tick(0.05f);
        }
        if (ex.processState() != ProcessState::Completed)
        {
            return 31;
        }
        if (!nearEqual(machine.z(), machine.machineConfig().motion.safe_z, 0.15f))
        {
            return 32;
        }
    }

    return 0;
}
