// Milestone 8: PC host end-to-end loop over Machine + TestExecutor.
#include "HostInputs.hpp"
#include "Machine.hpp"
#include "TestExecutor.hpp"
#include "TestPoint.hpp"
#include "TestProgram.hpp"
#include "Types.hpp"

#include <cmath>
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

int main()
{
    // Linkage sanity (kept from M1).
    std::cout << "core_version=" << core_version() << std::endl;

    // Host owns Machine; Executor references it (no ownership).
    Machine machine;
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
    if (!executor.start())
    {
        std::cout << "ERROR: executor.start() failed" << std::endl;
        return 2;
    }

    std::cout << "M8 host loop started (3 demo points)" << std::endl;

    // Fixed dt host loop (deterministic; no sleep).
    constexpr float kDt = 1.0f;
    constexpr int kMaxTicks = 100;
    constexpr int kPrintEvery = 1;

    int tick_count = 0;
    std::size_t last_logged_index = static_cast<std::size_t>(-1);

    while (executor.processState() == ProcessState::Running && tick_count < kMaxTicks)
    {
        // Host fills I/O boundary each cycle (no force/safety evaluation in M8).
        HostInputs inputs{};
        inputs.force = 0.0f;
        inputs.e_stop = false;
        machine.setHostInputs(inputs);

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
            std::cout << std::endl;
        }
    }

    // Timeout / unexpected terminal state.
    if (executor.processState() != ProcessState::Completed)
    {
        std::cout << "ERROR: expected Completed, got " << toString(executor.processState())
                  << " after " << tick_count << " ticks" << std::endl;
        return 3;
    }

    // End pose must match the last demo point.
    if (!nearEqual(machine.x(), p2.x) || !nearEqual(machine.y(), p2.y) || !nearEqual(machine.z(), p2.z))
    {
        std::cout << "ERROR: end pose mismatch. got=("
                  << machine.x() << "," << machine.y() << "," << machine.z()
                  << ") expected=(" << p2.x << "," << p2.y << "," << p2.z << ")" << std::endl;
        return 4;
    }

    std::cout << "M8 OK: Completed in " << tick_count << " ticks"
              << " end_pose=(" << machine.x() << "," << machine.y() << "," << machine.z() << ")"
              << std::endl;
    return 0;
}
