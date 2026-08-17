// Deterministic unit tests for TestExecutor (Milestone 7).
#include "Machine.hpp"
#include "TestExecutor.hpp"
#include "TestPoint.hpp"
#include "TestProgram.hpp"
#include "Types.hpp"

#include <cmath>

// Compare floats within a fixed tolerance. // helper
static bool nearEqual(float a, float b, float epsilon = 1e-3f)
{
    return std::fabs(a - b) <= epsilon; // abs compare
}

// Drive the executor until terminal or max steps. // helper
static void runUntilDone(TestExecutor& ex, int max_steps)
{
    for (int i = 0; i < max_steps && ex.processState() == ProcessState::Running; ++i)
    {
        ex.tick(1.0f); // fixed dt
    }
}

// Build a simple 3-point program along +X. // helper
static TestProgram makeThreePointProgram()
{
    TestProgram program; // empty
    program.add(TestPoint{10.0f, 0.0f, 0.0f}); // p0
    program.add(TestPoint{20.0f, 0.0f, 0.0f}); // p1
    program.add(TestPoint{30.0f, 0.0f, 0.0f}); // p2
    return program; // copy
}

static void seedIdentityPlate(Machine& machine)
{
    machine.setPlateOrigin(TestPoint{0.0f, 0.0f, 0.0f});
    TestConfiguration cfg = machine.testConfiguration();
    cfg.target_force_N = 0.1f;
    cfg.iterations = 1u;
    cfg.mode = TestMode::Aging;
    ConfigValidationError err = ConfigValidationError::Ok;
    machine.trySetTestConfiguration(cfg, err);
}

int main()
{
    // Test 1 – Initial Idle, no current point.
    {
        Machine machine; // host machine
        TestExecutor ex(machine); // idle executor

        if (ex.processState() != ProcessState::Idle) // must be Idle
        {
            return 1; // fail
        }

        TestPoint out{}; // scratch
        if (ex.currentPoint(out)) // no valid point
        {
            return 2; // fail
        }
    }

    // Test 2 – Start a 3-point program.
    {
        Machine machine; // machine
        seedIdentityPlate(machine);
        TestExecutor ex(machine); // executor
        ex.setProgram(makeThreePointProgram()); // load

        if (!ex.start()) // must start
        {
            return 3; // fail
        }
        if (ex.processState() != ProcessState::Running) // Running
        {
            return 4; // fail
        }
        if (ex.currentIndex() != 0) // first point
        {
            return 5; // fail
        }

        TestPoint cur{}; // scratch
        if (!ex.currentPoint(cur) || !nearEqual(cur.x, 10.0f)) // point 0
        {
            return 6; // fail
        }

        // One tick should begin motion toward point 0. // motion begins
        const float x0 = machine.x(); // before
        ex.tick(1.0f); // advance
        if (nearEqual(machine.x(), x0) && machine.machineState() == MachineState::Active)
        {
            // If already at target (unlikely from origin), Active would not hold;
            // from 0 toward 10 with v=10, x must change. // expect move
            return 7; // fail
        }
        if (!nearEqual(machine.x(), x0) && machine.x() <= x0)
        {
            return 7; // fail unexpected direction
        }
    }

    // Test 3 – Sequence order to Completed.
    {
        Machine machine; // machine
        seedIdentityPlate(machine);
        TestExecutor ex(machine); // executor
        ex.setProgram(makeThreePointProgram()); // 3 points

        if (!ex.start()) // start
        {
            return 8; // fail
        }

        // Track that we observe indices 0, then 1, then 2 before Completed. // order
        bool saw0 = (ex.currentIndex() == 0); // at start
        bool saw1 = false; // later
        bool saw2 = false; // later

        for (int i = 0; i < 400 && ex.processState() == ProcessState::Running; ++i)
        {
            const std::size_t idx = ex.currentIndex(); // sample
            if (idx == 1)
            {
                saw1 = true; // visited
            }
            if (idx == 2)
            {
                saw2 = true; // visited
            }
            ex.tick(1.0f); // step
        }

        if (ex.processState() != ProcessState::Completed) // must complete
        {
            return 9; // fail
        }
        if (!saw0 || !saw1 || !saw2) // order coverage
        {
            return 10; // fail
        }
        if (ex.currentIndex() != 2) // last index
        {
            return 11; // fail
        }

        TestPoint last{}; // scratch
        if (!ex.currentPoint(last) || !nearEqual(last.x, 30.0f)) // last point data
        {
            return 12; // fail
        }
        if (!nearEqual(machine.x(), 0.0f) || !nearEqual(machine.y(), 0.0f) || !nearEqual(machine.z(), 0.0f))
        {
            return 13; // fail end pose (safe pose)
        }
    }

    // Test 4 – Pause keeps Process Running and freezes progress.
    {
        Machine machine; // machine
        seedIdentityPlate(machine);
        TestExecutor ex(machine); // executor
        ex.setProgram(makeThreePointProgram()); // load
        ex.start(); // run

        const std::size_t idx_before = ex.currentIndex(); // snapshot
        const float x_before = machine.x(); // snapshot

        ex.pause(); // pause during PointApproach
        if (ex.processState() != ProcessState::Running) // process stays Running
        {
            return 14; // fail
        }
        if (machine.machineState() != MachineState::Paused) // machine paused
        {
            return 15; // fail
        }

        ex.tick(1.0f); // should not advance process
        ex.tick(1.0f); // again
        if (ex.currentIndex() != idx_before) // index frozen
        {
            return 16; // fail
        }
        if (!nearEqual(machine.x(), x_before)) // pose frozen
        {
            return 17; // fail
        }
    }

    // Test 5 – Resume continues to Completed.
    {
        Machine machine; // machine
        seedIdentityPlate(machine);
        TestExecutor ex(machine); // executor
        ex.setProgram(makeThreePointProgram()); // load
        ex.start(); // run
        ex.tick(1.0f); // partial
        ex.pause(); // pause
        ex.resume(); // resume

        if (ex.processState() != ProcessState::Running) // still Running
        {
            return 18; // fail
        }

        runUntilDone(ex, 400); // finish
        if (ex.processState() != ProcessState::Completed) // complete
        {
            return 19; // fail
        }
        if (!nearEqual(machine.x(), 0.0f)) // safe-end pose
        {
            return 20; // fail
        }
    }

    // Test 6 – Stop aborts and freezes.
    {
        Machine machine; // machine
        seedIdentityPlate(machine);
        TestExecutor ex(machine); // executor
        ex.setProgram(makeThreePointProgram()); // load
        ex.start(); // run
        ex.tick(1.0f); // partial
        const float x_before = machine.x(); // snapshot

        ex.stop(); // abort
        if (ex.processState() != ProcessState::Aborted) // aborted
        {
            return 21; // fail
        }
        if (machine.machineState() != MachineState::Stopped) // stopped
        {
            return 22; // fail
        }

        ex.tick(1.0f); // no further motion
        if (!nearEqual(machine.x(), x_before)) // frozen
        {
            return 23; // fail
        }
        if (ex.processState() != ProcessState::Aborted) // stays aborted
        {
            return 24; // fail
        }
    }

    // Test 7 – Empty program start fails and stays Idle.
    {
        Machine machine; // machine
        TestExecutor ex(machine); // idle
        TestProgram empty; // size 0
        ex.setProgram(empty); // empty

        if (ex.start()) // must fail
        {
            return 25; // fail
        }
        if (ex.processState() != ProcessState::Idle) // stay Idle
        {
            return 26; // fail
        }
    }

    // Test 8 – Invalid transitions are no-ops / rejected.
    {
        Machine machine; // machine
        seedIdentityPlate(machine);
        TestExecutor ex(machine); // idle

        ex.pause(); // Idle pause
        ex.resume(); // Idle resume
        if (ex.processState() != ProcessState::Idle) // unchanged
        {
            return 27; // fail
        }

        ex.setProgram(makeThreePointProgram()); // load
        ex.start(); // Running
        if (ex.start()) // second start must fail
        {
            return 28; // fail
        }
        if (ex.processState() != ProcessState::Running) // still Running
        {
            return 29; // fail
        }

        ex.stop(); // Aborted
        ex.resume(); // resume after Aborted must not revive process
        if (ex.processState() != ProcessState::Aborted) // stay Aborted
        {
            return 30; // fail
        }
        ex.pause(); // pause after Aborted
        if (ex.processState() != ProcessState::Aborted) // unchanged
        {
            return 31; // fail
        }
    }

    // Test 9 – External Machine::stop aborts on next tick.
    {
        Machine machine; // machine
        seedIdentityPlate(machine);
        TestExecutor ex(machine); // executor
        ex.setProgram(makeThreePointProgram()); // load
        ex.start(); // Running
        ex.tick(1.0f); // partial

        machine.stop(); // external stop
        if (ex.processState() != ProcessState::Running) // not yet observed
        {
            return 32; // fail
        }

        ex.tick(1.0f); // executor observes Stopped
        if (ex.processState() != ProcessState::Aborted) // Aborted
        {
            return 33; // fail
        }
    }

    // Test 10 – Restart after Completed and after Aborted.
    {
        Machine machine; // machine
        seedIdentityPlate(machine);
        TestExecutor ex(machine); // executor

        // Complete first run. // complete path
        ex.setProgram(makeThreePointProgram()); // prog A
        ex.start(); // run
        runUntilDone(ex, 400); // finish
        if (ex.processState() != ProcessState::Completed) // done
        {
            return 34; // fail
        }

        TestProgram prog_b; // new program
        prog_b.add(TestPoint{5.0f, 1.0f, 0.0f}); // single point
        ex.setProgram(prog_b); // replace
        if (!ex.start()) // restart
        {
            return 35; // fail
        }
        runUntilDone(ex, 400); // finish
        if (ex.processState() != ProcessState::Completed) // completed again
        {
            return 36; // fail
        }
        if (!nearEqual(machine.x(), 0.0f) || !nearEqual(machine.y(), 0.0f)) // safe-end pose
        {
            return 37; // fail
        }

        // Abort then restart. // abort path
        ex.setProgram(makeThreePointProgram()); // prog C
        ex.start(); // run
        ex.tick(1.0f); // partial
        ex.stop(); // abort
        if (ex.processState() != ProcessState::Aborted) // aborted
        {
            return 38; // fail
        }

        TestProgram prog_d; // another
        prog_d.add(TestPoint{0.0f, 0.0f, 0.0f}); // return home-ish
        ex.setProgram(prog_d); // replace
        if (!ex.start()) // must start again
        {
            return 39; // fail
        }
        runUntilDone(ex, 400); // finish
        if (ex.processState() != ProcessState::Completed) // completed
        {
            return 40; // fail
        }
    }

    // All TestExecutor tests passed.
    return 0;
}
