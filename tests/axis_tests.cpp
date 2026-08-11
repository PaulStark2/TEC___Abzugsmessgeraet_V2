// Deterministic unit tests for the Core Axis model.
#include "Axis.hpp"

// Include absolute value helper for overshoot checks.
#include <cmath>

// Include standard assert macro for deterministic test failures.
#include <cstdlib>

// Return zero when all tests pass and non-zero on failure.
int main()
{
    // Test movement toward a positive target.
    {
        // Create an axis starting at the origin.
        Axis axis;

        // Command motion toward a positive target.
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

    // Test movement toward a negative target.
    {
        // Create an axis starting above the target.
        Axis axis;

        // Command motion toward a lower target position.
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

    // Test that a tick can reach the target exactly.
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

    // Test that the axis never overshoots the target.
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

    // Test that zero velocity prevents motion.
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
            return 12;
        }
        if (axis.isAtTarget())
        {
            return 13;
        }
    }

    // Test already-at-target behavior.
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
            return 14;
        }
        if (!axis.isAtTarget())
        {
            return 15;
        }
    }

    // All deterministic Axis tests passed.
    return 0;
}
