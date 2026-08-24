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
        // Manual calibration requires user-jogged TCP near the probe:
        // ProbeCalibration starts from current TCP pose as search_reference.
        {
            const TestPoint disc = machine.machineConfig().probe.probe_offset_reference;
            machine.moveTo(disc.x, disc.y, disc.z);
            for (int i = 0; i < 800 && machine.motionState() != MotionState::AtTarget; ++i)
            {
                machine.setHostInputs(withFreshForce(HostInputs{}));
                machine.tick(0.05f);
            }
        }
        TestExecutor ex(machine);
        if (!ex.startProbeCalibration())
        {
            return 1;
        }
        if (ex.procedurePhase() != ProcedurePhase::ProbeCalibrate)
        {
            return 2;
        }
        for (int i = 0; i < 12000 && ex.processState() == ProcessState::Running; ++i)
        {
            machine.setHostInputs(withPlantInputs(machine));
            ex.tick(0.05f);
        }
        if (ex.processState() != ProcessState::Completed || !machine.calibrationSet().probe_center_valid)
        {
            return 3;
        }
    }

    // Automatic: must reject start while probe_center_valid == false.
    {
        Machine machine;
        machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
        homeMachine(machine);
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{8.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        if (ex.start())
        {
            return 10; // must not start without a calibrated probe_center
        }
    }

    // Automatic after manual calibration: ProbeApproach → ProbeCalibrate → MoveSafePose/PointApproach.
    {
        Machine machine;
        machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
        homeMachine(machine);

        // Manual calibration first.
        {
            TestExecutor ex(machine);
            const TestPoint disc = machine.machineConfig().probe.probe_offset_reference;
            machine.moveTo(disc.x, disc.y, disc.z);
            for (int i = 0; i < 800 && machine.motionState() != MotionState::AtTarget; ++i)
            {
                machine.setHostInputs(withFreshForce(HostInputs{}));
                machine.tick(0.05f);
            }
            if (!ex.startProbeCalibration())
            {
                return 11;
            }
            for (int i = 0; i < 12000 && ex.processState() == ProcessState::Running; ++i)
            {
                machine.setHostInputs(withPlantInputs(machine));
                ex.tick(0.05f);
            }
            machine.setRuntimeConfigLocked(false);
            machine.setCommunicationWatchdogEnabled(false);
            if (!machine.calibrationSet().probe_center_valid)
            {
                return 12;
            }
        }

        // Now start the automatic procedure (raises to dynamic Safe-Z on approach).
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{8.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        if (!ex.start())
        {
            return 13;
        }
        if (ex.procedurePhase() != ProcedurePhase::ProbeApproach &&
            ex.procedurePhase() != ProcedurePhase::ProbeCalibrate)
        {
            return 14;
        }
        bool saw_approach = ex.procedurePhase() == ProcedurePhase::ProbeApproach;
        bool saw_calib = ex.procedurePhase() == ProcedurePhase::ProbeCalibrate;
        for (int i = 0; i < 12000 && ex.processState() == ProcessState::Running; ++i)
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
            return 15;
        }
        if (!runUntil(ex, machine, ProcedurePhase::Clamp, 8000))
        {
            return 16;
        }

        // After calibration, probe_center.xy should match the configured virtual disc center
        // used by withPlantInputs() (disc == probe_offset_reference by default).
        const TestPoint nominal = machine.machineConfig().probe.probe_offset_reference;
        if (!nearEqual(machine.calibrationSet().probe_center.x, nominal.x) ||
            !nearEqual(machine.calibrationSet().probe_center.y, nominal.y))
        {
            return 17;
        }
    }

    return 0;
}
