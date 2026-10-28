#include "ForceSample.hpp"
#include "Machine.hpp"
#include "MachineConfig.hpp"
#include "TestExecutor.hpp"
#include "TestPoint.hpp"
#include "TestProgram.hpp"
#include "Types.hpp"

#include <cmath>
#include <cstdio>

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

static bool configure(Machine& machine, bool confirm, uint8_t refine_passes = 0)
{
    MachineConfig mcfg = machine.machineConfig();
    mcfg.force.force_filter = 0.0f;
    mcfg.force.preclamp_confirm = confirm;
    mcfg.force.xy_search_limit = 5.0f;
    mcfg.force.xy_correct_timeout_s = 60.0f;
    mcfg.force.bore_refine_passes = refine_passes;
    ConfigValidationError err = ConfigValidationError::Ok;
    return machine.trySetMachineConfig(mcfg, err);
}

static void contactForce(const Machine& machine, HostInputs& in, float cx, float cy, bool oval)
{
    constexpr float kStiffness = 100.0f;
    const float dx = machine.x() - cx;
    const float dy = machine.y() - cy;
    in.force.fx = 0.0f;
    in.force.fy = 0.0f;
    if (!oval)
    {
        constexpr float kRadius = 0.5f;
        const float radius = std::hypot(dx, dy);
        if (radius > kRadius)
        {
            const float penetration = radius - kRadius;
            in.force.fx = kStiffness * penetration * dx / radius;
            in.force.fy = kStiffness * penetration * dy / radius;
        }
        return;
    }
    constexpr float kPosX = 0.4f;
    constexpr float kNegX = 2.0f;
    constexpr float kY = 4.0f;
    if (dx > kPosX)
    {
        in.force.fx = kStiffness * (dx - kPosX);
    }
    else if (dx < -kNegX)
    {
        in.force.fx = kStiffness * (dx + kNegX);
    }
    if (dy > kY)
    {
        in.force.fy = kStiffness * (dy - kY);
    }
    else if (dy < -kY)
    {
        in.force.fy = kStiffness * (dy + kY);
    }
}

static int runSearch(bool confirm, bool oval, float hole_dx, float hole_dy, bool expect_pause,
                     bool check_center, uint8_t refine_passes = 0)
{
    Machine machine;
    seed(machine);
    if (!configure(machine, confirm, refine_passes))
    {
        return 1;
    }
    TestExecutor ex(machine);
    TestProgram program;
    program.add(TestPoint{1.0f, 0.0f, 0.0f});
    ex.setProgram(program);
    if (!ex.start())
    {
        return 2;
    }

    const float cx = 1.0f + hole_dx;
    const float cy = hole_dy;
    // Oval walls +0.4 / -2 from cx → second-chord mid near this x when Y is free.
    const float oval_mid_x = cx + 0.5f * (0.4f - 2.0f);
    bool paused = false;
    bool track = false;
    float min_x = 0.0f;
    float max_x = 0.0f;
    float min_y = 0.0f;
    float max_y = 0.0f;
    for (int i = 0; i < 16000 && ex.processState() == ProcessState::Running; ++i)
    {
        if (ex.procedurePhase() == ProcedurePhase::Clamp)
        {
            break;
        }
        HostInputs in{};
        if (ex.procedurePhase() == ProcedurePhase::PreClampXyCorrect)
        {
            contactForce(machine, in, cx, cy, oval);
            if (!track)
            {
                track = true;
                min_x = max_x = machine.x();
                min_y = max_y = machine.y();
            }
            else
            {
                if (machine.x() < min_x)
                {
                    min_x = machine.x();
                }
                if (machine.x() > max_x)
                {
                    max_x = machine.x();
                }
                if (machine.y() < min_y)
                {
                    min_y = machine.y();
                }
                if (machine.y() > max_y)
                {
                    max_y = machine.y();
                }
            }
            // After both axes have spanned a chord, load the second-chord mid so
            // confirm can trigger on residual force (no circle residual anymore).
            if (expect_pause && oval && (max_x - min_x) > 1.5f && (max_y - min_y) > 1.5f &&
                std::hypot(machine.x() - oval_mid_x, machine.y() - cy) < 0.25f)
            {
                in.force.fx = 2.0f;
                in.force.fy = 0.0f;
            }
        }
        machine.setHostInputs(withPlantInputs(machine, in));
        ex.tick(0.05f);
        if (machine.machineState() == MachineState::Paused &&
            ex.procedurePhase() == ProcedurePhase::PreClampXyCorrect)
        {
            paused = true;
            if (!expect_pause)
            {
                return 3;
            }
            ex.resume();
        }
    }
    if (expect_pause && !paused)
    {
        return 4;
    }
    if (ex.procedurePhase() != ProcedurePhase::Clamp)
    {
        std::fprintf(stderr, "phase=%d state=%d x=%.3f y=%.3f\n", static_cast<int>(ex.procedurePhase()),
                     static_cast<int>(ex.processState()), machine.x(), machine.y());
        return 5;
    }
    if (check_center && std::hypot(machine.x() - cx, machine.y() - cy) > 0.3f)
    {
        std::fprintf(stderr, "center miss x=%.3f y=%.3f want %.3f %.3f\n", machine.x(), machine.y(), cx, cy);
        return 6;
    }
    return 0;
}

// High +Fx unloads slowly. Noise pushes the same sign back over the tolerance.
// The first wall point must still be the opposite wall, where Fx has changed sign.
static int runSameSignNoise()
{
    Machine machine;
    seed(machine);
    if (!configure(machine, false))
    {
        return 1;
    }
    TestExecutor ex(machine);
    TestProgram program;
    program.add(TestPoint{1.0f, 0.0f, 0.0f});
    ex.setProgram(program);
    if (!ex.start())
    {
        return 2;
    }

    bool in_preclamp = false;
    float min_x = 0.0f;
    int sample = 0;
    for (int i = 0; i < 8000 && ex.processState() == ProcessState::Running; ++i)
    {
        if (ex.procedurePhase() == ProcedurePhase::Clamp)
        {
            break;
        }
        HostInputs in{};
        if (ex.procedurePhase() == ProcedurePhase::PreClampXyCorrect)
        {
            if (!in_preclamp)
            {
                in_preclamp = true;
                min_x = machine.x();
            }
            const float x = machine.x();
            const float y = machine.y();
            if (x < min_x)
            {
                min_x = x;
            }
            float fx = 0.0f;
            float fy = 0.0f;
            if (x > -1.0f)
            {
                fx = 4.0f * (x + 1.0f);
            }
            else if (x < -2.0f)
            {
                fx = 4.0f * (x + 2.0f);
            }
            if (y > 0.4f)
            {
                fy = 4.0f * (y - 0.4f);
            }
            else if (y < -0.4f)
            {
                fy = 4.0f * (y + 0.4f);
            }
            if (fx > 0.15f && fx < 0.85f && (sample % 2) == 0)
            {
                fx += 0.6f;
            }
            ++sample;
            in.force.fx = fx;
            in.force.fy = fy;
        }
        machine.setHostInputs(withPlantInputs(machine, in));
        ex.tick(0.05f);
    }
    if (!in_preclamp || !(min_x < -2.0f))
    {
        std::fprintf(stderr, "noise did not reach the opposite wall, min_x=%.3f\n", min_x);
        return 3;
    }
    if (ex.procedurePhase() != ProcedurePhase::Clamp)
    {
        std::fprintf(stderr, "noise phase=%d x=%.3f y=%.3f\n", static_cast<int>(ex.procedurePhase()),
                     machine.x(), machine.y());
        return 4;
    }
    if (std::hypot(machine.x() - (-1.5f), machine.y()) > 0.4f)
    {
        std::fprintf(stderr, "noise center x=%.3f y=%.3f\n", machine.x(), machine.y());
        return 5;
    }
    return 0;
}

int main()
{
    const int centered = runSearch(false, false, 0.25f, -0.15f, false, true);
    if (centered != 0)
    {
        std::fprintf(stderr, "centered -> %d\n", centered);
        return 10 + centered;
    }

    const int anomaly = runSearch(true, true, 0.0f, 0.0f, true, false);
    if (anomaly != 0)
    {
        std::fprintf(stderr, "anomaly -> %d\n", anomaly);
        return 20 + anomaly;
    }

    const int noise = runSameSignNoise();
    if (noise != 0)
    {
        std::fprintf(stderr, "noise -> %d\n", noise);
        return 50 + noise;
    }

    const int quiet = runSearch(false, true, 0.0f, 0.0f, false, false);
    if (quiet != 0)
    {
        std::fprintf(stderr, "quiet -> %d\n", quiet);
        return 30 + quiet;
    }

    const int refine = runSearch(false, false, 0.25f, -0.15f, false, true, 1);
    if (refine != 0)
    {
        std::fprintf(stderr, "refine -> %d\n", refine);
        return 60 + refine;
    }

    Machine machine;
    seed(machine);
    if (!configure(machine, false))
    {
        return 40;
    }
    MachineConfig mcfg = machine.machineConfig();
    mcfg.force.xy_search_limit = 1.0f;
    ConfigValidationError err = ConfigValidationError::Ok;
    if (!machine.trySetMachineConfig(mcfg, err))
    {
        return 41;
    }
    TestExecutor ex(machine);
    TestProgram program;
    program.add(TestPoint{1.0f, 0.0f, 0.0f});
    ex.setProgram(program);
    if (!ex.start())
    {
        return 42;
    }
    for (int i = 0; i < 4000 && ex.processState() == ProcessState::Running; ++i)
    {
        machine.setHostInputs(withPlantInputs(machine));
        ex.tick(0.05f);
    }
    if (ex.processState() != ProcessState::Aborted)
    {
        return 43;
    }

    MachineConfig too_long = machine.machineConfig();
    too_long.force.xy_correct_timeout_s = 601.0f;
    if (machine.trySetMachineConfig(too_long, err))
    {
        return 44;
    }
    MachineConfig ok_long = machine.machineConfig();
    ok_long.force.xy_correct_timeout_s = 600.0f;
    if (!machine.trySetMachineConfig(ok_long, err))
    {
        return 45;
    }
    return 0;
}
