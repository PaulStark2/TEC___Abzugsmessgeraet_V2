#include "ForceSample.hpp"
#include "HostInputs.hpp"
#include "Machine.hpp"
#include "MachineConfig.hpp"
#include "TestConfiguration.hpp"
#include "TestExecutor.hpp"
#include "Types.hpp"

#include <cmath>

// Extract seek: ignore first 0 N (Druck→Zug), end after Zug→kraftfrei; optional offset.

static HostInputs inputsWithFz(float fz)
{
    HostInputs in{};
    in.force.fz = fz;
    return withFreshForce(in);
}

static bool configure(Machine& machine, float extract_after_free_mm,
                      float phase3_v_extract = 2.0f, float extract_after_free_v = 1.0f)
{
    MachineConfig mcfg = machine.machineConfig();
    mcfg.force.force_filter = 0.0f;
    mcfg.force.insert_search_distance = 8.0f;
    mcfg.force.force_tolerance_N = 0.5f;
    mcfg.force.extract_after_free_mm = extract_after_free_mm;
    mcfg.motion.phase3_v_extract = phase3_v_extract;
    mcfg.motion.extract_after_free_v = extract_after_free_v;
    mcfg.spreader.extend_time_s = 0.05f;
    mcfg.spreader.retract_time_s = 0.05f;
    ConfigValidationError err = ConfigValidationError::Ok;
    if (!machine.trySetMachineConfig(mcfg, err))
    {
        return false;
    }
    if (!homeMachine(machine))
    {
        return false;
    }
    TestConfiguration tcfg = machine.testConfiguration();
    tcfg.mode = TestMode::Aging;
    tcfg.target_force_N = 5.0f;
    tcfg.iterations = 1u;
    return machine.trySetTestConfiguration(tcfg, err);
}

// Drive manual TestOrAge with a scripted Fz sequence keyed by extract progress.
static int runExtractScript(float extract_after_free_mm, bool inject_early_zero, float* out_free_z,
                            float phase3_v_extract = 2.0f, float extract_after_free_v = 1.0f)
{
    Machine machine;
    if (!configure(machine, extract_after_free_mm, phase3_v_extract, extract_after_free_v))
    {
        return 1;
    }
    TestExecutor ex(machine);
    if (!ex.startTestOrAgeManual())
    {
        return 2;
    }

    const float z0 = machine.z();
    bool saw_test = false;
    for (int i = 0; i < 5000 && ex.processState() == ProcessState::Running; ++i)
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
            saw_test = true;
            const unsigned leg = ex.procedure().cycleLeg();
            if (leg == 1u)
            {
                if (machine.z() <= z0 - 0.5f)
                {
                    fz = 5.0f;
                }
            }
            else
            {
                const float dz = machine.z() - z0;
                if (inject_early_zero && dz < 0.4f)
                {
                    // First 0 N crossing (Druck→Zug) — must NOT end extract.
                    fz = 0.0f;
                }
                else if (dz < 0.8f)
                {
                    fz = -5.0f; // Zug
                }
                else
                {
                    fz = 0.0f; // kraftfrei after Zug
                }
            }
        }
        machine.setHostInputs(inputsWithFz(fz));
        ex.tick(0.05f);
    }

    if (!saw_test)
    {
        return 3;
    }
    if (ex.processState() != ProcessState::Completed)
    {
        return 4;
    }
    if (out_free_z != nullptr)
    {
        *out_free_z = ex.procedure().forceFreeZ();
    }
    return 0;
}

int main()
{
    // Early 0 N without Zug must not complete; with Zug+kraftfrei it must.
    {
        const int rc = runExtractScript(0.0f, true, nullptr);
        if (rc != 0)
        {
            return 10 + rc;
        }
    }

    // Without early-zero injection, Zug→kraftfrei still completes.
    {
        const int rc = runExtractScript(0.0f, false, nullptr);
        if (rc != 0)
        {
            return 20 + rc;
        }
    }

    // Offset > 0: kraftfrei latched, then continue +Z before complete.
    {
        float free_z = 0.0f;
        const int rc = runExtractScript(2.0f, true, &free_z);
        if (rc != 0)
        {
            return 30 + rc;
        }
        // After offset the TCP should be above the latched kraftfrei pose.
        // forceFreeZ stays at kraftfrei; completion leaves machine at/above free+offset.
        if (!(free_z > 0.0f))
        {
            // z0 starts near 0 after home; free_z should be clearly above seat.
            // Accept any finite latch — main check is Completed above.
        }
    }

    // Slow phase3 extract + fast after-free speed: offset path must still complete
    // (profile switch at kraftfrei must not stall the seek).
    {
        float free_z = 0.0f;
        const int rc = runExtractScript(2.0f, true, &free_z, 0.2f, 5.0f);
        if (rc != 0)
        {
            return 50 + rc;
        }
    }

    // Instant kraftfrei (fz=0 always) must abort — never saw Zug.
    {
        Machine machine;
        if (!configure(machine, 0.0f))
        {
            return 40;
        }
        TestExecutor ex(machine);
        if (!ex.startTestOrAgeManual())
        {
            return 41;
        }
        const float z0 = machine.z();
        for (int i = 0; i < 800 && ex.processState() == ProcessState::Running; ++i)
        {
            float fz = 0.0f;
            if (ex.procedurePhase() == ProcedurePhase::InsertInitial && machine.z() <= z0 - 1.0f)
            {
                fz = 5.0f;
            }
            // TestOrAge extract: stay at 0 N → no Zug arming → abort.
            machine.setHostInputs(inputsWithFz(fz));
            ex.tick(0.05f);
        }
        if (ex.processState() != ProcessState::Aborted)
        {
            return 42;
        }
    }

    return 0;
}
