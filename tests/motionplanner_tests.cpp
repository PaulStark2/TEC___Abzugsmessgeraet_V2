// Deterministic unit tests for the Core MotionPlanner model.
#include "MotionPlanner.hpp"
#include "Types.hpp"

#include <cmath>
#include <vector>

// Compare two floats within a deterministic tolerance.
static bool nearEqual(float a, float b, float epsilon = 1e-4f)
{
    return std::fabs(a - b) <= epsilon;
}

static int countMovingAxes(const MotionPlanner& mp, float prev_x, float prev_y, float prev_z)
{
    int n = 0;
    if (std::fabs(mp.x() - prev_x) > 1.0e-6f)
    {
        ++n;
    }
    if (std::fabs(mp.y() - prev_y) > 1.0e-6f)
    {
        ++n;
    }
    if (std::fabs(mp.z() - prev_z) > 1.0e-6f)
    {
        ++n;
    }
    return n;
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

    // Test 2 – Move: Idle → Moving → AtTarget (sequential Z-up → X → Y).
    {
        MotionPlanner mp;
        mp.setMotionParams(10.0f, 0.0f);
        mp.moveTo(5.0f, 15.0f, 25.0f);

        if (mp.motionState() != MotionState::Moving)
        {
            return 3;
        }

        // Z-up 25 @ 10/s → 3s; X 5 → 0.5s; Y 15 → 1.5s. Use fine ticks.
        for (int i = 0; i < 200 && mp.motionState() != MotionState::AtTarget; ++i)
        {
            mp.tick(0.1f);
        }

        if (mp.motionState() != MotionState::AtTarget)
        {
            return 6;
        }

        if (!nearEqual(mp.x(), 5.0f) || !nearEqual(mp.y(), 15.0f) || !nearEqual(mp.z(), 25.0f))
        {
            return 7;
        }
    }

    // Test 3 – Z-down then X (from origin to (5,0,-5)): sequential, not simultaneous.
    {
        MotionPlanner mp;
        mp.setMotionParams(10.0f, 0.0f);
        mp.moveTo(5.0f, 0.0f, -5.0f);

        if (mp.motionState() != MotionState::Moving)
        {
            return 8;
        }

        mp.tick(1.0f); // completes X first (no Z-up; X then Z-down)
        // Order: X to 5, then Z to -5. After 1s at v=10: X done, Z not started.
        if (!nearEqual(mp.x(), 5.0f) || !nearEqual(mp.y(), 0.0f) || !nearEqual(mp.z(), 0.0f))
        {
            return 9;
        }
        if (mp.motionState() != MotionState::Moving)
        {
            return 10;
        }

        mp.tick(1.0f); // Z-down completes
        if (mp.motionState() != MotionState::AtTarget)
        {
            return 11;
        }
        if (!nearEqual(mp.x(), 5.0f) || !nearEqual(mp.y(), 0.0f) || !nearEqual(mp.z(), -5.0f))
        {
            return 12;
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
            return 13;
        }

        // Further ticks must not change the pose.
        for (int i = 0; i < 20; ++i)
        {
            mp.tick(1.0f);
        }

        if (!nearEqual(mp.x(), x_before) || !nearEqual(mp.y(), y_before) || !nearEqual(mp.z(), z_before))
        {
            return 14;
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

        for (int i = 0; i < 20 && mp.motionState() != MotionState::AtTarget; ++i)
        {
            mp.tick(2.0f);
            x_max = (mp.x() > x_max) ? mp.x() : x_max;
            y_max = (mp.y() > y_max) ? mp.y() : y_max;
            z_max = (mp.z() > z_max) ? mp.z() : z_max;
        }

        if (!nearEqual(mp.x(), 10.0f) || !nearEqual(mp.y(), 10.0f) || !nearEqual(mp.z(), 10.0f))
        {
            return 15;
        }

        if (x_max > 10.0f + 1e-3f || y_max > 10.0f + 1e-3f || z_max > 10.0f + 1e-3f)
        {
            return 16;
        }

        MotionPlanner mp2;
        mp2.setMotionParams(10.0f, 0.0f);
        mp2.moveTo(-10.0f, -10.0f, -10.0f);

        float x_min = mp2.x();
        float y_min = mp2.y();
        float z_min = mp2.z();

        for (int i = 0; i < 20 && mp2.motionState() != MotionState::AtTarget; ++i)
        {
            mp2.tick(2.0f);
            x_min = (mp2.x() < x_min) ? mp2.x() : x_min;
            y_min = (mp2.y() < y_min) ? mp2.y() : y_min;
            z_min = (mp2.z() < z_min) ? mp2.z() : z_min;
        }

        if (!nearEqual(mp2.x(), -10.0f) || !nearEqual(mp2.y(), -10.0f) || !nearEqual(mp2.z(), -10.0f))
        {
            return 17;
        }

        if (x_min < -10.0f - 1e-3f || y_min < -10.0f - 1e-3f || z_min < -10.0f - 1e-3f)
        {
            return 18;
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
            return 19;
        }

        mp.tick(-1.0f);
        if (!nearEqual(mp.x(), x0) || !nearEqual(mp.y(), y0) || !nearEqual(mp.z(), z0))
        {
            return 20;
        }
    }

    // Test 7 – moveTo(currentPose) results in AtTarget.
    {
        MotionPlanner mp;
        mp.setMotionParams(10.0f, 0.0f);
        mp.moveTo(0.0f, 0.0f, 0.0f);

        if (mp.motionState() != MotionState::AtTarget)
        {
            return 21;
        }

        mp.tick(1.0f);

        if (!nearEqual(mp.x(), 0.0f) || !nearEqual(mp.y(), 0.0f) || !nearEqual(mp.z(), 0.0f))
        {
            return 22;
        }
    }

    // Test 8 – setMotionParams / sequential: only Z moves first on equal XYZ goal above.
    {
        MotionPlanner mp_fast;
        mp_fast.setMotionParams(5.0f, 0.0f);
        mp_fast.moveTo(10.0f, 10.0f, 10.0f);
        mp_fast.tick(1.0f);

        // First leg is Z-up: X/Y still 0, Z=5.
        if (!nearEqual(mp_fast.x(), 0.0f) || !nearEqual(mp_fast.y(), 0.0f) || !nearEqual(mp_fast.z(), 5.0f))
        {
            return 23;
        }

        if (mp_fast.motionState() != MotionState::Moving)
        {
            return 24;
        }

        MotionPlanner mp_slow;
        mp_slow.setMotionParams(10.0f, 0.0f);
        mp_slow.moveTo(10.0f, 0.0f, 0.0f);
        mp_slow.tick(1.0f);

        if (!nearEqual(mp_slow.x(), 10.0f) || !nearEqual(mp_slow.y(), 0.0f) || !nearEqual(mp_slow.z(), 0.0f))
        {
            return 25;
        }

        if (mp_slow.motionState() != MotionState::AtTarget)
        {
            return 26;
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

        const auto planned = live.sampleWaypointPath(waypoints, 3, 10.0f, 0.0f, 0.05f, 60.0f);

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

    // Single-axis invariant: never two axes move in the same tick.
    {
        MotionPlanner mp;
        mp.setMotionParams(10.0f, 0.0f);
        mp.setCurrentPose(0.0f, 0.0f, 0.0f);
        mp.moveTo(20.0f, 30.0f, 40.0f);

        float px = mp.x();
        float py = mp.y();
        float pz = mp.z();
        for (int i = 0; i < 500 && mp.motionState() == MotionState::Moving; ++i)
        {
            mp.tick(0.05f);
            if (countMovingAxes(mp, px, py, pz) > 1)
            {
                return 40;
            }
            px = mp.x();
            py = mp.y();
            pz = mp.z();
        }
        if (mp.motionState() != MotionState::AtTarget)
        {
            return 41;
        }
    }

    // Order: X before Y when both change at constant Z.
    {
        MotionPlanner mp;
        mp.setMotionParams(10.0f, 0.0f);
        mp.setCurrentPose(0.0f, 0.0f, 40.0f);
        mp.moveTo(20.0f, 30.0f, 40.0f);

        bool saw_x = false;
        bool y_before_x = false;
        float px = mp.x();
        float py = mp.y();
        for (int i = 0; i < 500 && mp.motionState() == MotionState::Moving; ++i)
        {
            mp.tick(0.05f);
            const bool dx = std::fabs(mp.x() - px) > 1.0e-6f;
            const bool dy = std::fabs(mp.y() - py) > 1.0e-6f;
            if (dy && !saw_x)
            {
                y_before_x = true;
            }
            if (dx)
            {
                saw_x = true;
            }
            px = mp.x();
            py = mp.y();
        }
        if (y_before_x || !saw_x || !nearEqual(mp.x(), 20.0f) || !nearEqual(mp.y(), 30.0f))
        {
            return 42;
        }
    }

    // Order: Z-up before X when climbing.
    {
        MotionPlanner mp;
        mp.setMotionParams(10.0f, 0.0f);
        mp.setCurrentPose(0.0f, 0.0f, 0.0f);
        mp.moveTo(10.0f, 0.0f, 20.0f);

        float px = mp.x();
        float pz = mp.z();
        bool saw_z = false;
        bool x_before_z = false;
        for (int i = 0; i < 500 && mp.motionState() == MotionState::Moving; ++i)
        {
            mp.tick(0.05f);
            const bool dx = std::fabs(mp.x() - px) > 1.0e-6f;
            const bool dz = std::fabs(mp.z() - pz) > 1.0e-6f;
            if (dx && !saw_z)
            {
                x_before_z = true;
            }
            if (dz)
            {
                saw_z = true;
            }
            px = mp.x();
            pz = mp.z();
        }
        if (x_before_z || !saw_z)
        {
            return 43;
        }
    }

    // Idempotent re-issue: same goal does not restart the active leg.
    {
        MotionPlanner mp;
        mp.setMotionParams(10.0f, 0.0f);
        mp.moveTo(0.0f, 0.0f, 30.0f);
        mp.tick(1.0f);
        const float z_mid = mp.z();
        mp.moveTo(0.0f, 0.0f, 30.0f); // same goal
        mp.tick(1.0f);
        if (!(mp.z() > z_mid + 1.0f))
        {
            return 44; // must continue toward 30, not restart from 0
        }
    }

    // Corner blend: X→Y starts Y before X fully arrives when R > 0.
    {
        MotionPlanner mp;
        mp.setCurrentPose(0.0f, 0.0f, 0.0f);
        mp.setMotionParams(10.0f, 50.0f, 2.0f);
        mp.moveTo(10.0f, 10.0f, 0.0f);
        bool saw_both = false;
        for (int i = 0; i < 500; ++i)
        {
            mp.tick(0.01f);
            const bool x_moving = std::fabs(mp.x() - 10.0f) > 1e-3f;
            const bool y_moving = mp.y() > 1e-3f && std::fabs(mp.y() - 10.0f) > 1e-3f;
            if (x_moving && y_moving)
            {
                saw_both = true;
                break;
            }
            if (mp.motionState() == MotionState::AtTarget)
            {
                break;
            }
        }
        if (!saw_both)
        {
            return 50;
        }
        for (int i = 0; i < 2000 && mp.motionState() == MotionState::Moving; ++i)
        {
            mp.tick(0.01f);
        }
        if (mp.motionState() != MotionState::AtTarget || std::fabs(mp.x() - 10.0f) > 1e-2f ||
            std::fabs(mp.y() - 10.0f) > 1e-2f)
        {
            return 51;
        }
    }

    // R == 0: never two axes moving at once (full stop between legs).
    {
        MotionPlanner mp;
        mp.setCurrentPose(0.0f, 0.0f, 0.0f);
        mp.setMotionParams(10.0f, 50.0f, 0.0f);
        mp.moveTo(10.0f, 10.0f, 0.0f);
        for (int i = 0; i < 2000 && mp.motionState() == MotionState::Moving; ++i)
        {
            const float x0 = mp.x();
            const float y0 = mp.y();
            mp.tick(0.01f);
            const bool dx = std::fabs(mp.x() - x0) > 1e-6f;
            const bool dy = std::fabs(mp.y() - y0) > 1e-6f;
            if (dx && dy)
            {
                return 52;
            }
        }
        if (mp.motionState() != MotionState::AtTarget)
        {
            return 53;
        }
    }

    // Machine pattern: re-issue identical moveTo every tick must not cancel blend
    // or hang at the corner (regression: blending_ cleared before same-goal return).
    {
        MotionPlanner mp;
        mp.setCurrentPose(0.0f, 0.0f, 0.0f);
        // Matches typical GUI profile: v=10, a=10, R=5 (= v^2/(2a)).
        mp.setMotionParams(10.0f, 10.0f, 5.0f);
        mp.moveTo(20.0f, 20.0f, 0.0f);
        bool saw_both = false;
        for (int i = 0; i < 5000; ++i)
        {
            mp.moveTo(20.0f, 20.0f, 0.0f); // same as Machine::tickActiveMotion
            const float x0 = mp.x();
            const float y0 = mp.y();
            mp.tick(0.01f);
            const bool dx = std::fabs(mp.x() - x0) > 1e-6f;
            const bool dy = std::fabs(mp.y() - y0) > 1e-6f;
            if (dx && dy)
            {
                saw_both = true;
            }
            if (mp.motionState() == MotionState::AtTarget)
            {
                break;
            }
        }
        if (!saw_both)
        {
            return 57;
        }
        if (mp.motionState() != MotionState::AtTarget || std::fabs(mp.x() - 20.0f) > 1e-2f ||
            std::fabs(mp.y() - 20.0f) > 1e-2f)
        {
            return 58;
        }
    }

    // All MotionPlanner tests passed.
    return 0;
}
