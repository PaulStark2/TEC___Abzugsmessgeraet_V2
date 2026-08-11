// Deterministic unit tests for the Core Axis model.
#include "Axis.hpp"

// Include absolute value helper for comparisons.
#include <cmath>

// Compare two floats within a deterministic tolerance.
static bool nearEqual(float a, float b, float epsilon = 1e-4f)
{
    return std::fabs(a - b) <= epsilon;
}

// Return zero when all tests pass and non-zero on failure.
int main()
{
    // Milestone 2 compatibility: movement toward a positive target.
    {
        // Create an axis starting at the origin.
        Axis axis;

        // Command constant-velocity motion toward a positive target.
        axis.setPosition(0.0f);
        axis.setTarget(10.0f);
        axis.setVelocity(1.0f);
        axis.setEnabled(true);

        // Advance halfway toward the positive target.
        axis.tick(5.0f);
        if (axis.position() != 5.0f)
        {
            return 1;
        }
        if (axis.isAtTarget())
        {
            return 2;
        }

        // Complete the move to the positive target.
        axis.tick(5.0f);
        if (axis.position() != 10.0f)
        {
            return 3;
        }
        if (!axis.isAtTarget())
        {
            return 4;
        }
    }

    // Milestone 2 compatibility: movement toward a negative target.
    {
        // Create an axis starting above the target.
        Axis axis;

        // Command constant-velocity motion toward a lower target.
        axis.setPosition(10.0f);
        axis.setTarget(0.0f);
        axis.setVelocity(2.0f);
        axis.setEnabled(true);

        // Advance enough to reach the negative-direction target.
        axis.tick(5.0f);
        if (axis.position() != 0.0f)
        {
            return 5;
        }
        if (!axis.isAtTarget())
        {
            return 6;
        }
    }

    // Milestone 2 compatibility: exact target in one tick.
    {
        // Create an axis with a partial step remaining.
        Axis axis;

        // Configure values that produce an exact final position.
        axis.setPosition(0.0f);
        axis.setTarget(3.0f);
        axis.setVelocity(10.0f);
        axis.setEnabled(true);

        // Move exactly to the target in one tick.
        axis.tick(0.3f);
        if (axis.position() != 3.0f)
        {
            return 7;
        }
        if (!axis.isAtTarget())
        {
            return 8;
        }
    }

    // Milestone 2 compatibility: no overshoot with constant velocity.
    {
        // Create an axis with a large commanded step.
        Axis axis;

        // Configure a step larger than the remaining distance.
        axis.setPosition(0.0f);
        axis.setTarget(1.0f);
        axis.setVelocity(100.0f);
        axis.setEnabled(true);

        // Attempt to move far beyond the target in one tick.
        axis.tick(0.01f);
        if (axis.position() != 1.0f)
        {
            return 9;
        }
        if (axis.position() > 1.0f)
        {
            return 10;
        }
        if (!axis.isAtTarget())
        {
            return 11;
        }
    }

    // Milestone 2 compatibility: zero velocity prevents motion.
    {
        // Create an axis with a distant target.
        Axis axis;

        // Keep commanded velocity at zero.
        axis.setPosition(0.0f);
        axis.setTarget(10.0f);
        axis.setVelocity(0.0f);
        axis.setEnabled(true);

        // Attempt to move with zero velocity.
        axis.tick(1.0f);
        if (axis.position() != 0.0f)
        {
            return 10;
        }
        if (axis.isAtTarget())
        {
            return 11;
        }
    }

    // Milestone 2 compatibility: already-at-target behavior.
    {
        // Create an axis already equal to its target.
        Axis axis;

        // Set position and target to the same value.
        axis.setPosition(5.0f);
        axis.setTarget(5.0f);
        axis.setVelocity(1.0f);
        axis.setEnabled(true);

        // Tick should leave position unchanged.
        axis.tick(1.0f);
        if (axis.position() != 5.0f)
        {
            return 12;
        }
        if (!axis.isAtTarget())
        {
            return 13;
        }
        if (axis.currentVelocity() != 0.0f)
        {
            return 14;
        }
    }

    // Trapezoidal profile: acceleration from rest.
    {
        // Create an axis configured for trapezoidal motion.
        Axis axis;

        // Start from rest with a distant positive target.
        axis.setPosition(0.0f);
        axis.setTarget(100.0f);
        axis.setVelocity(10.0f);
        axis.setAcceleration(2.0f);
        axis.setEnabled(true);

        // Advance 0.5 seconds at 0.01 s per tick.
        for (int i = 0; i < 50; ++i)
        {
            axis.tick(0.01f);
        }

        // Expect speed v = a * t = 2 * 0.5 = 1.0 after acceleration phase.
        if (!nearEqual(axis.currentVelocity(), 1.0f))
        {
            return 15;
        }
        if (axis.position() <= 0.0f)
        {
            return 16;
        }
    }

    // Trapezoidal profile: reaching maximum velocity on a long move.
    {
        // Create an axis configured for trapezoidal motion.
        Axis axis;

        // Start from rest with a very long move.
        axis.setPosition(0.0f);
        axis.setTarget(1000.0f);
        axis.setVelocity(10.0f);
        axis.setAcceleration(2.0f);
        axis.setEnabled(true);

        // Advance until maximum speed should be reached (t = v_max / a = 5 s).
        for (int i = 0; i < 500; ++i)
        {
            axis.tick(0.01f);
        }

        // Expect cruise speed equal to the commanded maximum velocity.
        if (!nearEqual(axis.currentVelocity(), 10.0f))
        {
            return 17;
        }
    }

    // Trapezoidal profile: constant-velocity phase.
    {
        // Create an axis configured for trapezoidal motion.
        Axis axis;

        // Start from rest with a long move.
        axis.setPosition(0.0f);
        axis.setTarget(1000.0f);
        axis.setVelocity(10.0f);
        axis.setAcceleration(2.0f);
        axis.setEnabled(true);

        // Advance past the acceleration phase into cruise.
        for (int i = 0; i < 600; ++i)
        {
            axis.tick(0.01f);
        }

        // Record speed during the cruise phase.
        const float cruise_velocity = axis.currentVelocity();
        if (!nearEqual(cruise_velocity, 10.0f))
        {
            return 18;
        }

        // Advance several more cruise ticks.
        for (int i = 0; i < 50; ++i)
        {
            axis.tick(0.01f);
        }

        // Speed should remain at maximum during cruise.
        if (!nearEqual(axis.currentVelocity(), cruise_velocity))
        {
            return 19;
        }
    }

    // Trapezoidal profile: deceleration before the target.
    {
        // Create an axis near the end of a long move.
        Axis axis;

        // Configure a move that will decelerate before the target.
        axis.setPosition(0.0f);
        axis.setTarget(100.0f);
        axis.setVelocity(10.0f);
        axis.setAcceleration(2.0f);
        axis.setEnabled(true);

        // Advance into the deceleration phase (decel begins at t = 10 s).
        for (int i = 0; i < 1050; ++i)
        {
            axis.tick(0.01f);
        }

        // Speed should be below maximum during deceleration.
        if (axis.currentVelocity() >= 10.0f)
        {
            return 20;
        }
        if (axis.currentVelocity() <= 0.0f)
        {
            return 21;
        }
    }

    // Trapezoidal profile: exact stop at the target.
    {
        // Create an axis configured for trapezoidal motion.
        Axis axis;

        // Start a complete move from rest.
        axis.setPosition(0.0f);
        axis.setTarget(50.0f);
        axis.setVelocity(10.0f);
        axis.setAcceleration(2.0f);
        axis.setEnabled(true);

        // Advance until the target is reached.
        for (int i = 0; i < 5000; ++i)
        {
            axis.tick(0.01f);
            if (axis.isAtTarget())
            {
                break;
            }
        }

        // Expect an exact stop at the target with zero velocity.
        if (!nearEqual(axis.position(), 50.0f))
        {
            return 22;
        }
        if (!axis.isAtTarget())
        {
            return 23;
        }
        if (axis.currentVelocity() != 0.0f)
        {
            return 24;
        }
    }

    // Trapezoidal profile: no overshoot during the full move.
    {
        // Create an axis configured for trapezoidal motion.
        Axis axis;

        // Start a move and track the maximum position reached.
        axis.setPosition(0.0f);
        axis.setTarget(30.0f);
        axis.setVelocity(10.0f);
        axis.setAcceleration(2.0f);
        axis.setEnabled(true);

        float max_position = 0.0f;
        for (int i = 0; i < 5000; ++i)
        {
            axis.tick(0.01f);
            if (axis.position() > max_position)
            {
                max_position = axis.position();
            }
            if (axis.isAtTarget())
            {
                break;
            }
        }

        // Position must never exceed the target.
        if (max_position > 30.0f + 1e-4f)
        {
            return 25;
        }
        if (!axis.isAtTarget())
        {
            return 26;
        }
    }

    // Trapezoidal profile: negative-direction movement.
    {
        // Create an axis starting above the target.
        Axis axis;

        // Command motion toward a lower target with trapezoidal profile.
        axis.setPosition(50.0f);
        axis.setTarget(0.0f);
        axis.setVelocity(10.0f);
        axis.setAcceleration(2.0f);
        axis.setEnabled(true);

        // Advance until the target is reached.
        for (int i = 0; i < 5000; ++i)
        {
            axis.tick(0.01f);
            if (axis.isAtTarget())
            {
                break;
            }
        }

        // Expect negative motion ending at the target with zero velocity.
        if (!nearEqual(axis.position(), 0.0f))
        {
            return 27;
        }
        if (!axis.isAtTarget())
        {
            return 28;
        }
        if (axis.currentVelocity() != 0.0f)
        {
            return 29;
        }
    }

    // Trapezoidal profile: short move with triangular velocity profile.
    {
        // Create an axis configured for a short move.
        Axis axis;

        // Distance is too short to reach maximum velocity.
        axis.setPosition(0.0f);
        axis.setTarget(5.0f);
        axis.setVelocity(10.0f);
        axis.setAcceleration(2.0f);
        axis.setEnabled(true);

        float max_abs_velocity = 0.0f;

        // Advance until the target is reached.
        for (int i = 0; i < 5000; ++i)
        {
            axis.tick(0.01f);
            const float abs_velocity = std::fabs(axis.currentVelocity());
            if (abs_velocity > max_abs_velocity)
            {
                max_abs_velocity = abs_velocity;
            }
            if (axis.isAtTarget())
            {
                break;
            }
        }

        // Peak speed for triangular profile is sqrt(a * distance) = sqrt(10).
        const float expected_peak = std::sqrt(10.0f);
        if (!nearEqual(max_abs_velocity, expected_peak, 0.05f))
        {
            return 30;
        }
        if (max_abs_velocity >= 10.0f)
        {
            return 31;
        }
        if (!axis.isAtTarget())
        {
            return 32;
        }
    }

    // Trapezoidal profile: zero-distance movement.
    {
        // Create an axis already at its target.
        Axis axis;

        // Command a move with zero remaining distance.
        axis.setPosition(3.0f);
        axis.setTarget(3.0f);
        axis.setVelocity(10.0f);
        axis.setAcceleration(2.0f);
        axis.setEnabled(true);

        // Tick should not change position or velocity.
        axis.tick(0.01f);
        if (!nearEqual(axis.position(), 3.0f))
        {
            return 33;
        }
        if (!axis.isAtTarget())
        {
            return 34;
        }
        if (axis.currentVelocity() != 0.0f)
        {
            return 35;
        }
    }

    // Trapezoidal profile: zero maximum velocity prevents motion.
    {
        // Create an axis with zero maximum velocity.
        Axis axis;

        // Command a move that cannot start because speed is zero.
        axis.setPosition(0.0f);
        axis.setTarget(10.0f);
        axis.setVelocity(0.0f);
        axis.setAcceleration(2.0f);
        axis.setEnabled(true);

        // Tick should leave position unchanged.
        axis.tick(0.01f);
        if (axis.position() != 0.0f)
        {
            return 36;
        }
    }

    // Trapezoidal profile: zero dt does not advance motion.
    {
        // Create an axis configured for trapezoidal motion.
        Axis axis;

        // Start from rest.
        axis.setPosition(0.0f);
        axis.setTarget(10.0f);
        axis.setVelocity(10.0f);
        axis.setAcceleration(2.0f);
        axis.setEnabled(true);

        // Zero dt should be ignored.
        axis.tick(0.0f);
        if (axis.position() != 0.0f)
        {
            return 37;
        }
        if (axis.currentVelocity() != 0.0f)
        {
            return 38;
        }
    }

    // Trapezoidal profile: negative dt does not advance motion.
    {
        // Create an axis configured for trapezoidal motion.
        Axis axis;

        // Start from rest.
        axis.setPosition(0.0f);
        axis.setTarget(10.0f);
        axis.setVelocity(10.0f);
        axis.setAcceleration(2.0f);
        axis.setEnabled(true);

        // Negative dt should be ignored.
        axis.tick(-0.01f);
        if (axis.position() != 0.0f)
        {
            return 39;
        }
        if (axis.currentVelocity() != 0.0f)
        {
            return 40;
        }
    }

    // Trapezoidal profile: disabled axis does not move.
    {
        // Create a disabled axis configured for motion.
        Axis axis;

        // Disable the axis before ticking.
        axis.setPosition(0.0f);
        axis.setTarget(10.0f);
        axis.setVelocity(10.0f);
        axis.setAcceleration(2.0f);
        axis.setEnabled(false);

        // Tick should not change position or velocity.
        axis.tick(0.01f);
        if (axis.position() != 0.0f)
        {
            return 41;
        }
        if (axis.currentVelocity() != 0.0f)
        {
            return 42;
        }
    }

    // All deterministic Axis tests passed.
    return 0;
}
