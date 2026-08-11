// Include the Axis class declaration.
#include "Axis.hpp"

// Include absolute value helper for distance checks.
#include <cmath>

// Include Core position tolerance constant.
#include "Types.hpp"

// Start at the origin with zero velocity and motion enabled.
Axis::Axis()
    : position_(0.0f)
    , target_(0.0f)
    , velocity_(0.0f)
    , enabled_(true)
{
}

// Return the stored current position.
float Axis::position() const
{
    return position_;
}

// Return the stored target position.
float Axis::target() const
{
    return target_;
}

// Return the stored commanded velocity magnitude.
float Axis::velocity() const
{
    return velocity_;
}

// Return whether motion updates are enabled.
bool Axis::enabled() const
{
    return enabled_;
}

// Store a new current position.
void Axis::setPosition(float position)
{
    position_ = position;
}

// Store a new target position.
void Axis::setTarget(float target)
{
    target_ = target;
}

// Store a new non-negative commanded velocity magnitude.
void Axis::setVelocity(float velocity)
{
    // Negative commanded values are treated as zero velocity.
    velocity_ = (velocity < 0.0f) ? 0.0f : velocity;
}

// Enable or disable non-blocking motion updates.
void Axis::setEnabled(bool enabled)
{
    enabled_ = enabled;
}

// Compare current position to target using the Core tolerance.
bool Axis::isAtTarget() const
{
    // Compute signed distance remaining to the target.
    const float remaining = target_ - position_;

    // Treat positions within tolerance as equal to the target.
    return std::fabs(remaining) <= kPositionTolerance;
}

// Move toward the target at constant velocity without overshooting.
void Axis::tick(float dt)
{
    // Do not move a disabled axis.
    if (!enabled_)
    {
        return;
    }

    // Ignore non-positive time steps for deterministic updates.
    if (dt <= 0.0f)
    {
        return;
    }

    // Skip motion when the target is already reached.
    if (isAtTarget())
    {
        return;
    }

    // Compute signed distance remaining to the target.
    const float remaining = target_ - position_;

    // Determine travel direction as +1 or -1.
    const float direction = (remaining > 0.0f) ? 1.0f : -1.0f;

    // Compute distance traveled this tick at commanded velocity.
    const float step = velocity_ * dt;

    // Snap to target when the step would reach or pass it.
    if (step >= std::fabs(remaining))
    {
        position_ = target_;
    }
    else
    {
        // Move one step toward the target without overshooting.
        position_ += direction * step;
    }
}
