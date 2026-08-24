// Deterministic unit tests for the Core MotionPlanner model.
#include "MotionPlanner.hpp"

#include <cmath>
#include <vector>

// Compare two floats within a deterministic tolerance.
static bool nearEqual(float a, float b, float epsilon = 1e-4f)
{
    return std::fabs(a - b) <= epsilon;
}

// Return zero when all tests pass and non-zero on failure.
int main()
{
    // Test 1 – Initial Idle and no movement on tick.
    {
        MotionPlanner mp;

        if (mp.motionState() != MotionState::Idle)
        {
            return 1;
        }

        const float x0 = mp.x();
        const float y0 = mp.y();
        const float z0 = mp.z();

        mp.tick(1.0f);

        if (!nearEqual(mp.x(), x0) || !nearEqual(mp.y(), y0) || !nearEqual(mp.z(), z0))
        {
            return 2;
        }
    }

    // Test 2 – Move: Idle → Moving → AtTarget.
    {
        MotionPlanner mp;
        mp.setMotionParams(10.0f, 0.0f);
        mp.moveTo(5.0f, 15.0f, 25.0f);

        if (mp.motionState() != MotionState::Moving)
        {
            return 3;
        }

        // With constant velocity and dt=1.0:
        // X reaches 5 after 1 tick, Y reaches 15 after 2 ticks, Z reaches 25 after 3 ticks.
        mp.tick(1.0f);
        if (mp.motionState() == MotionState::AtTarget)
        {
            return 4;
        }

        mp.tick(1.0f);
        if (mp.motionState() == MotionState::AtTarget)
        {
            // Z is still not at target after 2 ticks in this setup.
            return 5;
        }

        mp.tick(1.0f);

        if (mp.motionState() != MotionState::AtTarget)
        {
            return 6;
        }

        if (!nearEqual(mp.x(), 5.0f) || !nearEqual(mp.y(), 15.0f) || !nearEqual(mp.z(), 25.0f))
        {
            return 7;
        }
    }

    // Test 3 – Independent axis movement.
    {
        MotionPlanner mp;
        mp.setMotionParams(10.0f, 0.0f);
        mp.moveTo(5.0f, 0.0f, -5.0f);

        if (mp.motionState() != MotionState::Moving)
        {
            return 8;
        }

        mp.tick(1.0f);

        if (mp.motionState() != MotionState::AtTarget)
        {
            return 9;
        }

        if (!nearEqual(mp.x(), 5.0f) || !nearEqual(mp.y(), 0.0f) || !nearEqual(mp.z(), -5.0f))
        {
            return 10;
        }
    }

    // Test 4 – StopMotion / Freeze.
    {
        MotionPlanner mp;
        mp.setMotionParams(10.0f, 0.0f);
        mp.moveTo(100.0f, 100.0f, 100.0f);

        mp.tick(0.5f); // Move some distance before stopping.

        const float x_before = mp.x();
        const float y_before = mp.y();
        const float z_before = mp.z();

        mp.stopMotion();

        if (mp.motionState() == MotionState::Moving)
        {
            return 11;
        }

        // Further ticks must not change the pose.
        for (int i = 0; i < 20; ++i)
        {
            mp.tick(1.0f);
        }

        if (!nearEqual(mp.x(), x_before) || !nearEqual(mp.y(), y_before) || !nearEqual(mp.z(), z_before))
        {
            return 12;
        }
    }

    // Test 5 – No overshoot in positive and negative directions.
    {
        MotionPlanner mp;
        mp.setMotionParams(10.0f, 0.0f);
        mp.moveTo(10.0f, 10.0f, 10.0f);

        float x_max = mp.x();
        float y_max = mp.y();
        float z_max = mp.z();

        for (int i = 0; i < 5 && mp.motionState() != MotionState::AtTarget; ++i)
        {
            mp.tick(2.0f); // Step is larger than remaining distance for safety clamping.
            x_max = (mp.x() > x_max) ? mp.x() : x_max;
            y_max = (mp.y() > y_max) ? mp.y() : y_max;
            z_max = (mp.z() > z_max) ? mp.z() : z_max;
        }

        if (!nearEqual(mp.x(), 10.0f) || !nearEqual(mp.y(), 10.0f) || !nearEqual(mp.z(), 10.0f))
        {
            return 13;
        }

        if (x_max > 10.0f + 1e-3f || y_max > 10.0f + 1e-3f || z_max > 10.0f + 1e-3f)
        {
            return 14;
        }

        MotionPlanner mp2;
        mp2.setMotionParams(10.0f, 0.0f);
        mp2.moveTo(-10.0f, -10.0f, -10.0f);

        float x_min = mp2.x();
        float y_min = mp2.y();
        float z_min = mp2.z();

        for (int i = 0; i < 5 && mp2.motionState() != MotionState::AtTarget; ++i)
        {
            mp2.tick(2.0f);
            x_min = (mp2.x() < x_min) ? mp2.x() : x_min;
            y_min = (mp2.y() < y_min) ? mp2.y() : y_min;
            z_min = (mp2.z() < z_min) ? mp2.z() : z_min;
        }

        if (!nearEqual(mp2.x(), -10.0f) || !nearEqual(mp2.y(), -10.0f) || !nearEqual(mp2.z(), -10.0f))
        {
            return 15;
        }

        if (x_min < -10.0f - 1e-3f || y_min < -10.0f - 1e-3f || z_min < -10.0f - 1e-3f)
        {
            return 16;
        }
    }

    // Test 6 – dt <= 0 does not change pose.
    {
        MotionPlanner mp;
        mp.setMotionParams(10.0f, 0.0f);
        mp.moveTo(10.0f, 20.0f, 30.0f);

        const float x0 = mp.x();
        const float y0 = mp.y();
        const float z0 = mp.z();

        mp.tick(0.0f);
        if (!nearEqual(mp.x(), x0) || !nearEqual(mp.y(), y0) || !nearEqual(mp.z(), z0))
        {
            return 17;
        }

        mp.tick(-1.0f);
        if (!nearEqual(mp.x(), x0) || !nearEqual(mp.y(), y0) || !nearEqual(mp.z(), z0))
        {
            return 18;
        }
    }

    // Test 7 – moveTo(currentPose) results in AtTarget.
    {
        MotionPlanner mp;
        mp.setMotionParams(10.0f, 0.0f);
        mp.moveTo(0.0f, 0.0f, 0.0f);

        if (mp.motionState() != MotionState::AtTarget)
        {
            return 19;
        }

        mp.tick(1.0f);

        if (!nearEqual(mp.x(), 0.0f) || !nearEqual(mp.y(), 0.0f) || !nearEqual(mp.z(), 0.0f))
        {
            return 20;
        }
    }

    // Test 8 – setMotionParams propagates to all axes (indirectly).
    {
        MotionPlanner mp_fast;
        mp_fast.setMotionParams(5.0f, 0.0f);
        mp_fast.moveTo(10.0f, 10.0f, 10.0f);
        mp_fast.tick(1.0f);

        if (!nearEqual(mp_fast.x(), 5.0f) || !nearEqual(mp_fast.y(), 5.0f) || !nearEqual(mp_fast.z(), 5.0f))
        {
            return 21;
        }

        if (mp_fast.motionState() != MotionState::Moving)
        {
            // After 1 second, we are not at the targets yet (targets are 10).
            return 22;
        }

        MotionPlanner mp_slow;
        mp_slow.setMotionParams(10.0f, 0.0f);
        mp_slow.moveTo(10.0f, 10.0f, 10.0f);
        mp_slow.tick(1.0f);

        if (!nearEqual(mp_slow.x(), 10.0f) || !nearEqual(mp_slow.y(), 10.0f) || !nearEqual(mp_slow.z(), 10.0f))
        {
            return 23;
        }

        if (mp_slow.motionState() != MotionState::AtTarget)
        {
            return 24;
        }
    }

    // Dry-run sampleWaypointPath matches live moveTo+tick TCP samples.
    {
        MotionPlanner live;
        live.setMotionParams(10.0f, 0.0f);
        live.setCurrentPose(0.0f, 0.0f, 40.0f);

        MotionPlanner::TcpPose waypoints[] = {
            {0.0f, 0.0f, 40.0f},
            {20.0f, 10.0f, 40.0f},
            {20.0f, 10.0f, 5.0f},
        };

        const auto planned = live.sampleWaypointPath(waypoints, 3, 10.0f, 0.0f, 0.05f, 30.0f);

        MotionPlanner check;
        check.setMotionParams(10.0f, 0.0f);
        check.setCurrentPose(0.0f, 0.0f, 40.0f);
        std::vector<MotionPlanner::TcpPose> actual;
        actual.push_back({check.x(), check.y(), check.z()});
        for (const auto& wp : waypoints)
        {
            check.moveTo(wp.x, wp.y, wp.z);
            while (check.motionState() == MotionState::Moving)
            {
                check.tick(0.05f);
                actual.push_back({check.x(), check.y(), check.z()});
            }
        }

        if (planned.size() != actual.size() || planned.empty())
        {
            return 30;
        }
        for (std::size_t i = 0; i < planned.size(); ++i)
        {
            if (!nearEqual(planned[i].x, actual[i].x) || !nearEqual(planned[i].y, actual[i].y) ||
                !nearEqual(planned[i].z, actual[i].z))
            {
                return 31;
            }
        }
        // Live planner pose unchanged by sample.
        if (!nearEqual(live.x(), 0.0f) || !nearEqual(live.y(), 0.0f) || !nearEqual(live.z(), 40.0f))
        {
            return 32;
        }
    }

    // All MotionPlanner tests passed.
    return 0;
}

