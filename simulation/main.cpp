// Milestone 8: PC host end-to-end loop over Machine + TestExecutor.
#include "HostInputs.hpp"
#include "Machine.hpp"
#include "MachineConfig.hpp"
#include "SafeZ.hpp"
#include "TestExecutor.hpp"
#include "TestPoint.hpp"
#include "TestProgram.hpp"
#include "Types.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>

// Convert ProcessState to a short label for console output.
static const char* toString(ProcessState s)
{
    switch (s)
    {
    case ProcessState::Idle:
        return "Idle";
    case ProcessState::Running:
        return "Running";
    case ProcessState::Completed:
        return "Completed";
    case ProcessState::Aborted:
        return "Aborted";
    default:
        return "Unknown";
    }
}

// Convert MachineState to a short label for console output.
static const char* toString(MachineState s)
{
    switch (s)
    {
    case MachineState::Idle:
        return "Idle";
    case MachineState::Active:
        return "Active";
    case MachineState::Paused:
        return "Paused";
    case MachineState::Stopped:
        return "Stopped";
    case MachineState::Fault:
        return "Fault";
    case MachineState::Homing:
        return "Homing";
    default:
        return "Unknown";
    }
}

// Convert MotionState to a short label for console output.
static const char* toString(MotionState s)
{
    switch (s)
    {
    case MotionState::Idle:
        return "Idle";
    case MotionState::Moving:
        return "Moving";
    case MotionState::AtTarget:
        return "AtTarget";
    default:
        return "Unknown";
    }
}

// Absolute float compare for end-pose checks.
static bool nearEqual(float a, float b, float epsilon = 1e-3f)
{
    return std::fabs(a - b) <= epsilon;
}

// Host force for the M8 demo: zero except tensile-positive Fz during insert/measure
// so Phase 1 cannot succeed from AtTarget or from pose-proportional stubs.

static HostInputs withHostSamples(HostInputs in)
{
    static std::uint32_t seq = 1;
    in.forceSampleValid = true;
    in.forceSampleSequence = seq;
    in.hostHeartbeatValid = true;
    in.hostHeartbeatSequence = seq;
    ++seq;
    return in;
}

static bool homeMachine(Machine& machine)
{
    HostInputs in = withHostSamples(HostInputs{});
    machine.setHostInputs(in);
    if (!machine.home())
    {
        return false;
    }
    in.limitZMax = true;
    machine.setHostInputs(withHostSamples(in));
    machine.tick(0.05f);
    in.limitZMax = false;
    in.limitXMax = true;
    machine.setHostInputs(withHostSamples(in));
    machine.tick(0.05f);
    in.limitXMax = false;
    in.limitYMax = true;
    machine.setHostInputs(withHostSamples(in));
    machine.tick(0.05f);
    return machine.isHomed();
}

int main()
{
    // Linkage sanity (kept from M1).
    std::cout << "core_version=" << core_version() << std::endl;

    // Host owns Machine; Executor references it (no ownership).
    Machine machine;
    machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
    TestExecutor executor(machine);

    // Hardcoded 3-point demo program (M8 only; no JSON/GUI).
    const TestPoint p0{10.0f, 0.0f, 0.0f};
    const TestPoint p1{20.0f, 5.0f, 0.0f};
    const TestPoint p2{30.0f, 5.0f, -5.0f};

    TestProgram program;
    if (!program.add(p0) || !program.add(p1) || !program.add(p2))
    {
        std::cout << "ERROR: failed to build demo TestProgram" << std::endl;
        return 1;
    }

    executor.setProgram(program);
    if (!homeMachine(machine))
    {
        std::cout << "ERROR: demo homing failed" << std::endl;
        return 5;
    }
    if (!executor.start())
    {
        std::cout << "ERROR: executor.start() failed" << std::endl;
        return 2;
    }

    std::cout << "M8 host loop started (3 demo points, Safe-Z approach)" << std::endl;

    // Fixed dt host loop (deterministic; no sleep).
    constexpr float kDt = 1.0f;
    constexpr int kMaxTicks = 200;
    constexpr int kPrintEvery = 1;

    int tick_count = 0;
    std::size_t last_logged_index = static_cast<std::size_t>(-1);

    while (executor.processState() == ProcessState::Running && tick_count < kMaxTicks)
    {
        // M9: pose -> deterministic 3D force stub; Machine stores only (no evaluation).
        HostInputs inputs{};
        const ProcedurePhase phase = executor.procedurePhase();
        if (phase == ProcedurePhase::InsertInitial || phase == ProcedurePhase::OptimizeInsert ||
            phase == ProcedurePhase::TestOrAge)
        {
            inputs.force.fz = machine.testConfiguration().target_force_N;
        }
        inputs.e_stop = false;
        machine.setHostInputs(withHostSamples(inputs));

        // Only executor.tick drives Machine::tick internally.
        executor.tick(kDt);
        ++tick_count;

        const std::size_t idx = executor.currentIndex();
        const bool index_changed = (idx != last_logged_index);
        last_logged_index = idx;

        if (index_changed || (tick_count % kPrintEvery) == 0)
        {
            TestPoint cur{};
            const bool has_point = executor.currentPoint(cur);

            std::cout << "tick=" << tick_count
                      << " process=" << toString(executor.processState())
                      << " machine=" << toString(machine.machineState())
                      << " motion=" << toString(machine.motionState())
                      << " index=" << idx
                      << " pose=(" << machine.x() << "," << machine.y() << "," << machine.z() << ")";
            if (has_point)
            {
                std::cout << " target=(" << cur.x << "," << cur.y << "," << cur.z << ")";
            }
            std::cout << " force=(Fx=" << inputs.force.fx << ", Fy=" << inputs.force.fy
                      << ", Fz=" << inputs.force.fz << ")" << std::endl;
        }
    }

    // Timeout / unexpected terminal state.
    if (executor.processState() != ProcessState::Completed)
    {
        std::cout << "ERROR: expected Completed, got " << toString(executor.processState())
                  << " after " << tick_count << " ticks" << std::endl;
        return 3;
    }

    // Safe-End: XY at home_offset, Z remains at dynamic program Safe-Z.
    float safe_z_machine = 0.0f;
    if (!resolveProgramSafeZMachine(program, machine.machineConfig().motion.safe_z_buffer,
                                    machine.calibrationSet(), safe_z_machine))
    {
        std::cout << "ERROR: could not resolve dynamic Safe-Z" << std::endl;
        return 4;
    }
    if (!nearEqual(machine.x(), 0.0f) || !nearEqual(machine.y(), 0.0f) ||
        !nearEqual(machine.z(), safe_z_machine))
    {
        std::cout << "ERROR: end pose mismatch. got=("
                  << machine.x() << "," << machine.y() << "," << machine.z()
                  << ") expected=(0,0," << safe_z_machine << ")" << std::endl;
        return 4;
    }

    std::cout << "M8 OK: Completed in " << tick_count << " ticks"
              << " end_pose=(" << machine.x() << "," << machine.y() << "," << machine.z() << ")"
              << std::endl;
    return 0;
}
