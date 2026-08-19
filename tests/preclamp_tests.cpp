#include "ForceSample.hpp"
#include "Machine.hpp"
#include "TestExecutor.hpp"
#include "TestPoint.hpp"
#include "TestProgram.hpp"
#include "Types.hpp"

#include <cmath>

static bool nearEqual(float a, float b, float eps = 1e-3f)
{
    return std::fabs(a - b) <= eps;
}

static void seed(Machine& machine)
{
    machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
    TestConfiguration cfg = machine.testConfiguration();
    cfg.target_force_N = 0.1f;
    cfg.mode = TestMode::Aging;
    ConfigValidationError err = ConfigValidationError::Ok;
    machine.trySetTestConfiguration(cfg, err);
    homeMachine(machine);
    (void)calibrateProbeCenter(machine);
}

int main()
{
    // XY correction only while PreClamp is enabled.
    {
        Machine m;
        m.moveTo(0.0f, 0.0f, 0.0f);
        HostInputs in{};
        in.force.fx = 12.0f;
        tickWithForce(m, 0.05f, in);
        if (!nearEqual(m.x(), 0.0f))
        {
            return 1;
        }
        m.loadPreClampForceParams();
        m.setPreClampXyEnabled(true);
        tickWithForce(m, 0.05f, in);
        if (!(m.x() < 0.0f) || !m.preClampXyEnabled())
        {
            return 2;
        }
        m.setPreClampXyEnabled(false);
        const float x_off = m.x();
        tickWithForce(m, 0.05f, in);
        if (!nearEqual(m.x(), x_off) || m.preClampXyEnabled())
        {
            return 3;
        }
    }

    // Timeout aborts if Fx/Fy never settle.
    {
        Machine machine;
        seed(machine);
        MachineConfig mcfg = machine.machineConfig();
        mcfg.force.xy_correct_timeout_s = 0.2f;
        mcfg.force.xy_tolerance_N = 0.2f;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!machine.trySetMachineConfig(mcfg, err))
        {
            return 10;
        }
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{1.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        if (!ex.start())
        {
            return 11;
        }
        for (int i = 0; i < 2500 && ex.procedurePhase() != ProcedurePhase::PreClampXyCorrect &&
                        ex.processState() == ProcessState::Running;
             ++i)
        {
            machine.setHostInputs(withPlantInputs(machine));
            ex.tick(0.05f);
        }
        if (ex.procedurePhase() != ProcedurePhase::PreClampXyCorrect)
        {
            return 12;
        }
        for (int i = 0; i < 20 && ex.processState() == ProcessState::Running; ++i)
        {
            HostInputs in{};
            in.force.fx = 8.0f;
            machine.setHostInputs(withPlantInputs(machine, in));
            ex.tick(0.05f);
        }
        if (ex.processState() != ProcessState::Aborted)
        {
            return 13;
        }
        if (machine.preClampXyEnabled())
        {
            return 14;
        }
    }

    // After Clamp, PreClamp XY stays off through InsertInitial.
    {
        Machine machine;
        seed(machine);
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{1.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        if (!ex.start())
        {
            return 20;
        }
        for (int i = 0; i < 2500 && ex.procedurePhase() != ProcedurePhase::InsertInitial &&
                        ex.processState() == ProcessState::Running;
             ++i)
        {
            machine.setHostInputs(withPlantInputs(machine));
            ex.tick(0.05f);
        }
        if (ex.procedurePhase() != ProcedurePhase::InsertInitial)
        {
            return 21;
        }
        if (machine.preClampXyEnabled())
        {
            return 22;
        }
        const float x0 = machine.x();
        HostInputs in{};
        in.force.fx = 12.0f;
        machine.setHostInputs(withFreshForce(in));
        ex.tick(0.05f);
        if (!nearEqual(machine.x(), x0, 0.2f))
        {
            return 23;
        }
    }

    return 0;
}
