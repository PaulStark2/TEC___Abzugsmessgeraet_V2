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
        // Manual calibration: operator jogs TCP to the hub above the disc.
        {
            const TestPoint disc = machine.machineConfig().probe.probe_offset_reference;
            const float standoff = machine.machineConfig().probe.approach_distance;
            machine.moveTo(disc.x, disc.y, disc.z + standoff);
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
            const float standoff = machine.machineConfig().probe.approach_distance;
            machine.moveTo(disc.x, disc.y, disc.z + standoff);
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

        // M19 leaves the home pose — re-home before automatic start.
        if (!homeMachine(machine))
        {
            return 13;
        }

        const TestPoint stored_center = machine.calibrationSet().probe_center;
        const TestPoint tool_before = machine.calibrationSet().tool_offset;

        // Now start the automatic procedure (raises to dynamic Safe-Z on approach).
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{8.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        if (!ex.start())
        {
            return 14;
        }
        if (ex.procedurePhase() != ProcedurePhase::ProbeApproach &&
            ex.procedurePhase() != ProcedurePhase::ProbeCalibrate)
        {
            return 15;
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
            return 16;
        }
        if (!runUntil(ex, machine, ProcedurePhase::Clamp, 8000))
        {
            return 17;
        }

        // OffsetOnly: probeCenter must stay the Install reference; toolOffset ≈ 0 when
        // plant disc matches that reference (not reconstructed from probe_offset_reference).
        if (!nearEqual(machine.calibrationSet().probe_center.x, stored_center.x) ||
            !nearEqual(machine.calibrationSet().probe_center.y, stored_center.y) ||
            !nearEqual(machine.calibrationSet().probe_center.z, stored_center.z))
        {
            return 18;
        }
        const TestPoint tool_after = machine.calibrationSet().tool_offset;
        if (!nearEqual(tool_after.x, 0.0f) || !nearEqual(tool_after.y, 0.0f) ||
            !nearEqual(tool_after.z, 0.0f))
        {
            return 19;
        }
        (void)tool_before;
        (void)saw_approach;
    }

    return 0;
}
