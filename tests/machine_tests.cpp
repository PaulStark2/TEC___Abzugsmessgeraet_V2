// Deterministic unit tests for the Core Machine model (Milestone 5).
#include "Machine.hpp"
#include "HostInputs.hpp"

#include <cmath>

// Compare two floats within a deterministic tolerance. // helper
static bool nearEqual(float a, float b, float epsilon = 1e-4f)
{
    // absolute difference compare // math
    return std::fabs(a - b) <= epsilon; // compare
}

// Return zero when all tests pass and non-zero on failure.
int main()
{
    // Test 1 – Initial Idle.
    {
        Machine m; // create machine
        if (m.machineState() != MachineState::Idle) // state check
        {
            return 1; // fail
        }

        const float x0 = m.x(); // initial pose x
        const float y0 = m.y(); // initial pose y
        const float z0 = m.z(); // initial pose z

        m.tick(1.0f); // tick while idle
        if (!nearEqual(m.x(), x0) || !nearEqual(m.y(), y0) || !nearEqual(m.z(), z0)) // position unchanged
        {
            return 2; // fail
        }
    }

    // Test 2 – Start Idle -> Active and movement begins on tick.
    {
        Machine m; // create
        m.moveTo(10.0f, 20.0f, -10.0f); // command new move

        if (m.machineState() != MachineState::Active) // expect active
        {
            return 3; // fail
        }

        const float x0 = m.x(); // current x before tick
        m.tick(1.0f); // advance

        // With default MotionPlanner params (maxVelocity=10, accel=0), in dt=1.0:
        // X should reach 10, Y should advance by 10 to 10, Z should advance by -10 to -10.
        if (nearEqual(m.x(), x0)) // ensure some change happened
        {
            return 4; // fail
        }
    }

    // Test 3 – Target reached: Active -> Idle and exact pose.
    {
        Machine m; // create
        m.moveTo(10.0f, 20.0f, -10.0f); // command pose

        // Advance in fixed dt steps until Idle.
        for (int i = 0; i < 10 && m.machineState() != MachineState::Idle; ++i)
        {
            m.tick(1.0f); // tick
        }

        if (m.machineState() != MachineState::Idle) // must end idle
        {
            return 5; // fail
        }

        if (!nearEqual(m.x(), 10.0f) || !nearEqual(m.y(), 20.0f) || !nearEqual(m.z(), -10.0f)) // final pose exact
        {
            return 6; // fail
        }
    }

    // Test 4 – Pause: Active -> Paused and no further movement.
    {
        Machine m; // create
        m.moveTo(30.0f, 0.0f, 0.0f); // target far enough

        m.tick(1.0f); // advance some
        const float x_before = m.x(); // remember pose

        m.pause(); // pause
        if (m.machineState() != MachineState::Paused) // state check
        {
            return 7; // fail
        }

        m.tick(1.0f); // tick while paused
        if (!nearEqual(m.x(), x_before)) // pose frozen
        {
            return 8; // fail
        }
    }

    // Test 5 – Resume: Paused -> Active, continues to stored target.
    {
        Machine m; // create
        m.moveTo(30.0f, 0.0f, 0.0f); // target

        m.tick(1.0f); // move partially
        m.pause(); // pause

        const float x_pause = m.x(); // remember position during pause
        m.resume(); // resume to stored target

        if (m.machineState() != MachineState::Active) // state active
        {
            return 9; // fail
        }

        // Progress until Idle again.
        for (int i = 0; i < 20 && m.machineState() != MachineState::Idle; ++i)
        {
            m.tick(1.0f); // tick
        }

        if (m.machineState() != MachineState::Idle) // end idle
        {
            return 10; // fail
        }

        if (!nearEqual(m.x(), 30.0f)) // reach target
        {
            return 11; // fail
        }

        // Ensure it actually moved beyond pause position.
        if (nearEqual(m.x(), x_pause)) // no movement occurred
        {
            return 12; // fail
        }
    }

    // Test 6 – Stop: Active -> Stopped and no further movement; resume no effect.
    {
        Machine m; // create
        m.moveTo(30.0f, 0.0f, 0.0f); // target

        m.tick(1.0f); // partially move
        const float x_before = m.x(); // remember

        m.stop(); // stop
        if (m.machineState() != MachineState::Stopped) // state check
        {
            return 13; // fail
        }

        m.tick(1.0f); // should not move
        if (!nearEqual(m.x(), x_before)) // frozen
        {
            return 14; // fail
        }

        m.resume(); // illegal resume after stop
        m.tick(1.0f); // still must not move
        if (!nearEqual(m.x(), x_before)) // still frozen
        {
            return 15; // fail
        }
    }

    // Test 7 – Stop cannot be resumed (resume no-op in Stopped).
    {
        Machine m; // create
        m.moveTo(10.0f, 0.0f, 0.0f); // command
        m.tick(1.0f); // maybe moved
        m.stop(); // stop

        const float x_before = m.x(); // store
        m.resume(); // no-op
        m.tick(1.0f); // still no movement

        if (m.machineState() != MachineState::Stopped) // unchanged
        {
            return 16; // fail
        }
        if (!nearEqual(m.x(), x_before)) // unchanged position
        {
            return 17; // fail
        }
    }

    // Test 8 – New move after Stop starts Active again.
    {
        Machine m; // create
        m.moveTo(10.0f, 0.0f, 0.0f); // command
        m.tick(0.5f); // partial
        m.stop(); // stop

        if (m.machineState() != MachineState::Stopped) // check
        {
            return 18; // fail
        }

        m.moveTo(20.0f, 0.0f, 0.0f); // new command
        if (m.machineState() != MachineState::Active) // active again
        {
            return 19; // fail
        }

        for (int i = 0; i < 10 && m.machineState() != MachineState::Idle; ++i)
        {
            m.tick(1.0f); // tick
        }
        if (m.machineState() != MachineState::Idle) // finished
        {
            return 20; // fail
        }
        if (!nearEqual(m.x(), 20.0f)) // reached
        {
            return 21; // fail
        }
    }

    // Test 9 – Invalid commands: pause/resume no-ops in wrong states.
    {
        Machine m; // create
        m.pause(); // Idle -> no-op
        if (m.machineState() != MachineState::Idle) // unchanged
        {
            return 22; // fail
        }

        m.resume(); // Idle -> no-op
        if (m.machineState() != MachineState::Idle) // unchanged
        {
            return 23; // fail
        }

        m.moveTo(10.0f, 0.0f, 0.0f); // to Active
        m.stop(); // to Stopped

        m.pause(); // should not affect Stopped
        if (m.machineState() != MachineState::Stopped) // unchanged
        {
            return 24; // fail
        }
    }

    // Test 10 – dt <= 0 does not change pose.
    {
        Machine m; // create
        m.moveTo(10.0f, 20.0f, -10.0f); // command

        const float x0 = m.x(); // record
        const float y0 = m.y(); // record
        const float z0 = m.z(); // record

        m.tick(0.0f); // dt=0
        if (!nearEqual(m.x(), x0) || !nearEqual(m.y(), y0) || !nearEqual(m.z(), z0)) // unchanged
        {
            return 25; // fail
        }

        m.tick(-1.0f); // dt negative
        if (!nearEqual(m.x(), x0) || !nearEqual(m.y(), y0) || !nearEqual(m.z(), z0)) // unchanged
        {
            return 26; // fail
        }
    }

    // Test 11 – Host I/O: setHostInputs accepts data; hostOutputs available.
    {
        Machine m; // create
        HostInputs in{}; // default POD
        in.force = 12.34f; // example input
        in.limitXMin = true; // example limit placeholder
        in.e_stop = true; // example e-stop placeholder
        m.setHostInputs(in); // store only

        // No evaluation in M5, just ensure outputs getter exists.
        HostOutputs out = m.hostOutputs(); // query outputs
        (void)out; // suppress unused warning
    }

    // All Machine tests passed.
    return 0;
}

