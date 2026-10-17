#include "ForceSample.hpp"
#include "ForceTare.hpp"
#include "HostInputs.hpp"
#include "Machine.hpp"
#include "MachineConfig.hpp"
#include "TestConfiguration.hpp"
#include "TestExecutor.hpp"
#include "TestProgram.hpp"
#include "Types.hpp"

#include <cmath>
#include <cstdio>

static bool nearEqual(float a, float b, float eps = 1e-3f)
{
    return std::fabs(a - b) <= eps;
}

static void feed(Machine& machine, float fx, float fy, float fz)
{
    HostInputs in{};
    in.force = ForceVector{fx, fy, fz};
    machine.setHostInputs(withFreshForce(in));
}

static bool finishConstant(ForceTare& tare, float dt, const ForceVector& sample, int ticks)
{
    for (int i = 0; i < ticks; ++i)
    {
        tare.tick(dt, sample);
    }
    return !tare.active() && tare.engaged();
}

int main()
{
    const ForceVector sample{1.0f, -2.0f, 4.0f};

    // Twenty samples over 1.9 s: first at begin, then every 100 ms.
    {
        ForceTare tare;
        if (!tare.begin(sample))
        {
            return 1;
        }
        for (int i = 0; i < 18; ++i)
        {
            if (!tare.tick(0.1f, sample) && !tare.active())
            {
                return 2;
            }
        }
        if (!tare.tick(0.1f, sample) || tare.active() || !tare.engaged())
        {
            return 3;
        }
        const ForceVector offset = tare.offset();
        if (!nearEqual(offset.fx, 1.0f) || !nearEqual(offset.fy, -2.0f) || !nearEqual(offset.fz, 4.0f))
        {
            return 4;
        }
        const ForceVector shown = tare.apply(ForceVector{1.5f, -2.0f, 9.0f});
        if (!nearEqual(shown.fx, 0.5f) || !nearEqual(shown.fy, 0.0f) || !nearEqual(shown.fz, 5.0f))
        {
            return 5;
        }
    }

    // Mean of a ramp, not the last sample.
    {
        ForceTare tare;
        ForceVector v{0.0f, 0.0f, 0.0f};
        tare.begin(v);
        for (int i = 1; i <= 19; ++i)
        {
            v.fz = static_cast<float>(i);
            tare.tick(0.1f, v);
        }
        if (tare.active() || !nearEqual(tare.offset().fz, 9.5f))
        {
            return 6;
        }
    }

    // 1 ms and 50 ms take the same 20 samples of a constant.
    {
        ForceTare fine;
        fine.begin(sample);
        for (int i = 0; i < 99; ++i)
        {
            fine.tick(0.001f, sample);
        }
        if (!fine.active())
        {
            return 7;
        }
        if (!finishConstant(fine, 0.001f, sample, 1801))
        {
            return 8;
        }
        ForceTare coarse;
        coarse.begin(sample);
        if (!coarse.tick(0.05f, sample) && !coarse.active())
        {
            return 9;
        }
        if (!finishConstant(coarse, 0.05f, sample, 37))
        {
            return 10;
        }
        if (!nearEqual(fine.offset().fz, coarse.offset().fz) || !nearEqual(fine.offset().fz, 4.0f))
        {
            return 11;
        }
    }

    // Stop drops an unfinished capture and keeps the previous offset.
    {
        Machine machine;
        feed(machine, 0.0f, 0.0f, 2.0f);
        if (!machine.beginForceTare())
        {
            return 12;
        }
        for (int i = 0; i < 40; ++i)
        {
            tickWithForce(machine, 0.05f, HostInputs{ForceVector{0.0f, 0.0f, 2.0f}});
        }
        if (machine.forceTareActive() || !nearEqual(machine.hostInputs().force.fz, 0.0f))
        {
            return 13;
        }
        feed(machine, 0.0f, 0.0f, 3.0f);
        if (!machine.beginForceTare())
        {
            return 14;
        }
        tickWithForce(machine, 0.05f, HostInputs{ForceVector{0.0f, 0.0f, 3.0f}});
        machine.stop();
        if (machine.forceTareActive())
        {
            return 15;
        }
        feed(machine, 0.0f, 0.0f, 3.0f);
        if (!nearEqual(machine.hostInputs().force.fz, 1.0f))
        {
            return 16;
        }
    }

    // Context threshold is off only while capturing. The hard limit stays.
    {
        Machine machine;
        machine.setCollisionContext(CollisionContext::Approach);
        feed(machine, 0.0f, 0.0f, 25.0f);
        if (!machine.beginForceTare())
        {
            return 17;
        }
        for (int i = 0; i < 40 && machine.forceTareActive(); ++i)
        {
            tickWithForce(machine, 0.05f, HostInputs{ForceVector{0.0f, 0.0f, 25.0f}});
        }
        if (machine.machineState() == MachineState::Fault || machine.forceTareActive())
        {
            return 18;
        }

        Machine open;
        open.setCollisionContext(CollisionContext::Approach);
        tickWithForce(open, 0.05f, HostInputs{ForceVector{0.0f, 0.0f, 25.0f}});
        if (open.machineState() != MachineState::Fault || open.lastFaultReason() != FaultReason::ForceCollision)
        {
            return 19;
        }

        Machine hard;
        feed(hard, 0.0f, 0.0f, 40.0f);
        hard.beginForceTare();
        tickWithForce(hard, 0.05f, HostInputs{ForceVector{0.0f, 0.0f, 40.0f}});
        if (hard.machineState() != MachineState::Fault || hard.lastFaultReason() != FaultReason::ForceHardLimit)
        {
            return 20;
        }
    }

    // One tare before the fine approach, then at each later standstill.
    // PreClamp follows the fine descent directly so the XY correction still sees contact force.
    {
        Machine machine;
        machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
        if (!homeMachine(machine) || !calibrateProbeCenter(machine))
        {
            return 21;
        }
        TestConfiguration cfg = machine.testConfiguration();
        cfg.target_force_N = 8.0f;
        cfg.mode = TestMode::Aging;
        cfg.iterations = 1u;
        ConfigValidationError err = ConfigValidationError::Ok;
        if (!machine.trySetTestConfiguration(cfg, err))
        {
            return 22;
        }
        TestExecutor ex(machine);
        TestProgram program;
        program.add(TestPoint{1.0f, 0.0f, 0.0f});
        ex.setProgram(program);
        if (!ex.start())
        {
            return 23;
        }

        bool saw_fine_after_tare = false;
        bool tare_into_preclamp = false;
        bool saw_preclamp = false;
        bool saw_insert = false;
        bool saw_optimize = false;
        bool saw_test = false;
        bool saw_unclamp = false;
        bool in_tare = false;
        for (int i = 0; i < 16000 && ex.processState() == ProcessState::Running; ++i)
        {
            const ProcedurePhase ph = ex.procedurePhase();
            if (ph == ProcedurePhase::ForceTare)
            {
                in_tare = true;
            }
            float fz = 0.0f;
            if (ph == ProcedurePhase::InsertInitial && machine.z() < -0.05f)
            {
                fz = 8.0f;
            }
            else if (ph == ProcedurePhase::OptimizeInsert || ph == ProcedurePhase::TestOrAge)
            {
                fz = 8.0f;
            }
            machine.setHostInputs(withPlantInputs(machine, HostInputs{ForceVector{0.0f, 0.0f, fz}}));
            ex.tick(0.05f);
            const ProcedurePhase next = ex.procedurePhase();
            if (next == ProcedurePhase::PreClampXyCorrect)
            {
                saw_preclamp = true;
            }
            if (in_tare && next != ProcedurePhase::ForceTare)
            {
                in_tare = false;
                saw_fine_after_tare = saw_fine_after_tare || next == ProcedurePhase::PointApproach;
                tare_into_preclamp = tare_into_preclamp || next == ProcedurePhase::PreClampXyCorrect;
                saw_insert = saw_insert || next == ProcedurePhase::InsertInitial;
                saw_optimize = saw_optimize || next == ProcedurePhase::OptimizeInsert;
                saw_test = saw_test || next == ProcedurePhase::TestOrAge;
                saw_unclamp = saw_unclamp || next == ProcedurePhase::Unclamp;
            }
            if (saw_unclamp)
            {
                break;
            }
        }
        if (!saw_fine_after_tare || tare_into_preclamp || !saw_preclamp || !saw_insert || !saw_optimize ||
            !saw_test || !saw_unclamp)
        {
            std::fprintf(stderr,
                         "tare boundaries fine=%d into_pre=%d pre=%d insert=%d opt=%d test=%d unclamp=%d "
                         "phase=%u state=%u\n",
                         saw_fine_after_tare ? 1 : 0, tare_into_preclamp ? 1 : 0, saw_preclamp ? 1 : 0,
                         saw_insert ? 1 : 0, saw_optimize ? 1 : 0, saw_test ? 1 : 0, saw_unclamp ? 1 : 0,
                         static_cast<unsigned>(ex.procedurePhase()),
                         static_cast<unsigned>(ex.processState()));
            return 24;
        }
    }

    return 0;
}
