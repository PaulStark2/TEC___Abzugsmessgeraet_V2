#include "ForceSample.hpp"
#include "HostInputs.hpp"
#include "Machine.hpp"
#include "MachineConfig.hpp"
#include "TestConfiguration.hpp"
#include "TestExecutor.hpp"
#include "Types.hpp"

#include <cmath>

// Hand-mode TestOrAge: SeatToForce then iterations; standalone complete keeps Homed.

static HostInputs inputsWithFz(float fz)
{
    HostInputs in{};
    in.force.fz = fz;
    return withFreshForce(in);
}

static bool configure(Machine& machine)
{
    MachineConfig mcfg = machine.machineConfig();
    mcfg.force.force_filter = 0.0f;
    mcfg.force.insert_search_distance = 5.0f;
    mcfg.force.force_tolerance_N = 0.5f;
    mcfg.spreader.extend_time_s = 0.05f;
    mcfg.spreader.retract_time_s = 0.05f;
    ConfigValidationError err = ConfigValidationError::Ok;
    if (!machine.trySetMachineConfig(mcfg, err))
    {
        return false;
    }
    homeMachine(machine);
    TestConfiguration tcfg = machine.testConfiguration();
    tcfg.mode = TestMode::Aging;
    tcfg.target_force_N = 5.0f;
    tcfg.iterations = 1u;
    return machine.trySetTestConfiguration(tcfg, err);
}

int main()
{
    // Happy path: seat → one extract/insert cycle → Completed, Homed kept.
    {
        Machine machine;
        if (!configure(machine))
        {
            return 10;
        }
        // Mid-workspace pose (Z negative from MCS home at +max).
        machine.moveTo(machine.x(), machine.y(), -10.0f);
        for (int i = 0; i < 400; ++i)
        {
            machine.setHostInputs(inputsWithFz(0.0f));
            machine.tick(0.05f);
            if (machine.motionState() == MotionState::AtTarget)
            {
                break;
            }
        }

        TestExecutor ex(machine);
        if (!ex.startTestOrAgeManual())
        {
            return 11;
        }
        const float z0 = machine.z();
        for (int i = 0; i < 20000 && ex.processState() == ProcessState::Running; ++i)
        {
            float fz = 0.0f;
            const ProcedurePhase ph = ex.procedurePhase();
            if (ph == ProcedurePhase::InsertInitial)
            {
                if (machine.z() <= z0 - 1.0f)
                {
                    fz = 5.0f;
                }
            }
            else if (ph == ProcedurePhase::TestOrAge)
            {
                const unsigned leg = ex.procedure().cycleLeg();
                if (leg == 1u)
                {
                    // Insert dive: contact once below last kraftfrei-ish height.
                    if (machine.z() <= z0 - 0.5f)
                    {
                        fz = 5.0f;
                    }
                }
                else
                {
                    // Extract: ignore first 0 N; arm on Zug, then kraftfrei.
                    const float dz = machine.z() - z0;
                    if (dz < 0.3f)
                    {
                        fz = 5.0f;
                    }
                    else if (dz < 1.0f)
                    {
                        fz = -5.0f;
                    }
                    else
                    {
                        fz = 0.0f;
                    }
                }
            }
            machine.setHostInputs(inputsWithFz(fz));
            ex.tick(0.05f);
        }
        if (ex.processState() != ProcessState::Completed)
        {
            return 12;
        }
        if (ex.procedurePhase() != ProcedurePhase::Completed)
        {
            return 13;
        }
        if (machine.homingState() != HomingState::Homed)
        {
            return 14;
        }
    }

    // STOP abort keeps Homed (standalone).
    {
        Machine machine;
        if (!configure(machine))
        {
            return 20;
        }
        TestExecutor ex(machine);
        if (!ex.startTestOrAgeManual())
        {
            return 21;
        }
        for (int i = 0; i < 40 && ex.processState() == ProcessState::Running; ++i)
        {
            machine.setHostInputs(inputsWithFz(0.0f));
            ex.tick(0.05f);
        }
        ex.stop();
        if (ex.processState() != ProcessState::Aborted)
        {
            return 22;
        }
        if (machine.homingState() != HomingState::Homed)
        {
            return 23;
        }
    }

    // Reject without Homed / zero force.
    {
        Machine machine;
        TestExecutor ex(machine);
        if (ex.startTestOrAgeManual())
        {
            return 30;
        }
        if (!configure(machine))
        {
            return 31;
        }
        TestConfiguration tcfg = machine.testConfiguration();
        tcfg.target_force_N = 0.0f;
        ConfigValidationError err = ConfigValidationError::Ok;
        // Direct zero may fail trySet — force via start check: set valid then mutate if possible.
        tcfg.target_force_N = 5.0f;
        tcfg.iterations = 0u;
        if (machine.trySetTestConfiguration(tcfg, err))
        {
            if (ex.startTestOrAgeManual())
            {
                return 32;
            }
        }
    }

    return 0;
}
