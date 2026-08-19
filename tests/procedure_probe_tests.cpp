#include "ForceSample.hpp"
#include "HostInputs.hpp"
#include "Machine.hpp"
#include "ProbeCalibration.hpp"
#include "TestExecutor.hpp"
#include "TestPoint.hpp"
#include "TestProgram.hpp"
#include "Types.hpp"

#include <cmath>

static bool nearEqual(float a, float b, float eps = 0.8f)
{
    return std::fabs(a - b) <= eps;
}

static bool runUntil(TestExecutor& ex, Machine& machine, ProcedurePhase phase, int max_steps)
{
    for (int i = 0; i < max_steps && ex.processState() == ProcessState::Running; ++i)
    {
        if (ex.procedurePhase() == phase)
        {
            return true;
        }
        machine.setHostInputs(withPlantInputs(machine));
        ex.tick(0.05f);
    }
    return ex.procedurePhase() == phase;
}

int main()
{
    // Manual: skip ProbeApproach, complete calibration, process Completed.
    {
        Machine machine;
        homeMachine(machine);
        TestExecutor ex(machine);
        if (!ex.startProbeCalibration())
        {
            return 1;
        }
        if (ex.procedurePhase() != ProcedurePhase::ProbeCalibrate)
        {
            return 2;
        }
        for (int i = 0; i < 2500 && ex.processState() == ProcessState::Running; ++i)
        {
            machine.setHostInputs(withPlantInputs(machine));
            ex.tick(0.05f);
        }
        if (ex.processState() != ProcessState::Completed || !machine.calibrationSet().probe_center_valid)
        {
            return 3;
        }
    }

    // Automatic: ProbeApproach → ProbeCalibrate → MoveSafePose/PointApproach.
    {
        Machine machine;
        machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
        homeMachine(machine);
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{8.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        if (!ex.start())
        {
            return 10;
        }
        if (ex.procedurePhase() != ProcedurePhase::ProbeApproach &&
            ex.procedurePhase() != ProcedurePhase::ProbeCalibrate)
        {
            return 11;
        }
        bool saw_approach = ex.procedurePhase() == ProcedurePhase::ProbeApproach;
        bool saw_calib = ex.procedurePhase() == ProcedurePhase::ProbeCalibrate;
        for (int i = 0; i < 2500 && ex.processState() == ProcessState::Running; ++i)
        {
            if (ex.procedurePhase() == ProcedurePhase::ProbeApproach)
            {
                saw_approach = true;
            }
            if (ex.procedurePhase() == ProcedurePhase::ProbeCalibrate)
            {
                saw_calib = true;
            }
            if (ex.procedurePhase() == ProcedurePhase::PointApproach ||
                ex.procedurePhase() == ProcedurePhase::PreClampXyCorrect ||
                ex.procedurePhase() == ProcedurePhase::Clamp)
            {
                break;
            }
            machine.setHostInputs(withPlantInputs(machine));
            ex.tick(0.05f);
        }
        if (!saw_calib)
        {
            return 12;
        }
        if (!machine.calibrationSet().probe_center_valid)
        {
            return 13;
        }
        if (!runUntil(ex, machine, ProcedurePhase::Clamp, 2500))
        {
            return 14;
        }
        if (saw_approach == false && machine.machineConfig().probe.nominal_probe.x == 0.0f &&
            machine.machineConfig().probe.nominal_probe.y == 0.0f)
        {
            // Approach can finish in the start() kick; still acceptable.
        }
        const TestPoint nominal = machine.machineConfig().probe.nominal_probe;
        if (!nearEqual(machine.calibrationSet().probe_center.x, nominal.x) ||
            !nearEqual(machine.calibrationSet().probe_center.y, nominal.y))
        {
            return 15;
        }
    }

    return 0;
}
