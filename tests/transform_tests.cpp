#include "CalibrationSet.hpp"
#include "CoordinateTransform.hpp"
#include "ForceSample.hpp"
#include "Machine.hpp"
#include "TestExecutor.hpp"
#include "TestPoint.hpp"
#include "TestProgram.hpp"

#include <cmath>

static bool nearEqual(float a, float b, float eps = 1e-5f)
{
    return std::fabs(a - b) <= eps;
}

int main()
{
    // Reject transform without plate origin.
    {
        CalibrationSet set{};
        TestPoint out{};
        if (toMachinePoint(set, TestPoint{1.0f, 2.0f, 3.0f}, out))
        {
            return 1;
        }
    }

    // machine = origin + plate + toolOffset.
    {
        CalibrationSet set{};
        setPlateOrigin(set, TestPoint{10.0f, 20.0f, 5.0f});
        set.tool_offset = TestPoint{0.5f, -1.0f, 2.0f};
        TestPoint out{};
        if (!toMachinePoint(set, TestPoint{1.0f, 2.0f, 3.0f}, out))
        {
            return 2;
        }
        if (!nearEqual(out.x, 11.5f) || !nearEqual(out.y, 21.0f) || !nearEqual(out.z, 10.0f))
        {
            return 3;
        }
        setPlateOrigin(set, TestPoint{0.0f, 0.0f, 0.0f});
        if (!toMachinePoint(set, TestPoint{1.0f, 2.0f, 3.0f}, out) || !nearEqual(out.x, 1.5f))
        {
            return 4;
        }
    }

    // Executor start rejected without origin; with origin plate coords are transformed.
    {
        Machine machine;
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{4.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        if (ex.start())
        {
            return 10;
        }
        machine.setPlateOrigin(TestPoint{10.0f, 0.0f, 0.0f});
        if (ex.start())
        {
            return 13;
        }
        if (!homeMachine(machine))
        {
            return 14;
        }
        if (!ex.start())
        {
            return 11;
        }
        bool saw_machine_x = false;
        for (int i = 0; i < 2500 && ex.processState() == ProcessState::Running; ++i)
        {
            machine.setHostInputs(withProcessForce(ex, machine));
            ex.tick(0.05f);
            if (nearEqual(machine.x(), 14.0f, 0.2f))
            {
                saw_machine_x = true;
            }
        }
        if (!saw_machine_x)
        {
            return 12;
        }
    }

    // Homing snaps pose to homeOffset.
    {
        Machine machine;
        MachineConfig cfg = machine.machineConfig();
        cfg.calibration.home_offset = TestPoint{1.0f, 2.0f, 3.0f};
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!machine.trySetMachineConfig(cfg, err))
        {
            return 20;
        }
        HostInputs in{};
        machine.setHostInputs(withFreshForce(in));
        machine.home();
        in.limitZMax = true;
        machine.setHostInputs(withFreshForce(in));
        machine.tick(0.05f);
        in.limitZMax = false;
        in.limitXMax = true;
        machine.setHostInputs(withFreshForce(in));
        machine.tick(0.05f);
        in.limitXMax = false;
        in.limitYMax = true;
        machine.setHostInputs(withFreshForce(in));
        machine.tick(0.05f);
        if (!machine.isHomed())
        {
            return 21;
        }
        if (!nearEqual(machine.x(), 1.0f) || !nearEqual(machine.y(), 2.0f) || !nearEqual(machine.z(), 3.0f))
        {
            return 22;
        }
    }

    return 0;
}
