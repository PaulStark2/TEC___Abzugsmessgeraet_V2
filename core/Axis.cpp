// Include the Axis class declaration.
#include "Axis.hpp"

// Include absolute value and square root helpers.
#include <cmath>

// Include Core position tolerance constant.
#include "Types.hpp"

// Start at the origin with zero velocity and motion enabled.
Axis::Axis()
    : position_(0.0f)
    , target_(0.0f)
    , max_velocity_(0.0f)
    , current_velocity_(0.0f)
    , acceleration_(0.0f)
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

// Return the stored maximum velocity command.
float Axis::velocity() const
{
    return max_velocity_;
}

// Return the stored current signed velocity.
float Axis::currentVelocity() const
{
    return current_velocity_;
}

// Return the stored acceleration command.
float Axis::acceleration() const
{
    return acceleration_;
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

// Store a new non-negative maximum velocity command.
void Axis::setVelocity(float velocity)
{
    // Negative commanded values are treated as zero velocity.
    max_velocity_ = (velocity < 0.0f) ? 0.0f : velocity;
}

// Store a new non-negative acceleration command.
void Axis::setAcceleration(float acceleration)
{
    // Negative commanded values are treated as zero acceleration.
    acceleration_ = (acceleration < 0.0f) ? 0.0f : acceleration;
}

// Enable or disable non-blocking motion updates.
void Axis::setEnabled(bool enabled)
{
    enabled_ = enabled;
}

void Axis::snapTo(float position)
{
    position_ = position;
    target_ = position;
    current_velocity_ = 0.0f;
}

// Compare current position to target using the Core tolerance.
bool Axis::isAtTarget() const
{
    // Compute signed distance remaining to the target.
    const float remaining = target_ - position_;

    // Treat positions within tolerance as equal to the target.
    return std::fabs(remaining) <= kPositionTolerance;
}

// Compute signed acceleration for the trapezoidal velocity profile.
static float computeSignedAcceleration(
    float remaining,
    float current_velocity,
    float max_velocity,
    float acceleration)
{
    // Use absolute remaining distance for profile calculations.
    const float rem_abs = std::fabs(remaining);

    // Stop applying acceleration when the target is reached.
    if (rem_abs <= kPositionTolerance)
    {
        return 0.0f;
    }

    // Determine travel direction toward the target.
    const float direction = (remaining > 0.0f) ? 1.0f : -1.0f;

    // Decelerate when current velocity points away from the target.
    if (current_velocity * direction < 0.0f)
    {
        return -((current_velocity > 0.0f) ? 1.0f : -1.0f) * acceleration;
    }

    // Use absolute speed for profile distance calculations.
    const float v_abs = std::fabs(current_velocity);

    // Distance required to stop from the current speed.
    const float d_stop = (v_abs * v_abs) / (2.0f * acceleration);

    // Begin deceleration when remaining distance fits the stopping distance.
    if (rem_abs <= d_stop)
    {
        return -direction * acceleration;
    }

    // Distance to accelerate from current speed to maximum speed.
    const float d_acc = (max_velocity * max_velocity - v_abs * v_abs) / (2.0f * acceleration);

    // Distance to decelerate from maximum speed to zero.
    const float d_decel_from_max = (max_velocity * max_velocity) / (2.0f * acceleration);

    // Use a trapezoidal profile when enough distance is available.
    if (rem_abs >= d_acc + d_decel_from_max)
    {
        // Accelerate while below maximum speed.
        if (v_abs < max_velocity)
        {
            return direction * acceleration;
        }

        // Maintain maximum speed during the cruise phase.
        return 0.0f;
    }

    // Use a triangular profile when maximum speed cannot be reached.
    const float v_peak = std::sqrt(acceleration * rem_abs + 0.5f * v_abs * v_abs);

    // Accelerate while below the triangular peak speed.
    if (v_abs < v_peak)
    {
        return direction * acceleration;
    }

    // Decelerate after passing the triangular peak speed.
    return -direction * acceleration;
}

// Move toward the target using constant velocity when acceleration is zero.
static void tickConstantVelocity(
    float& position,
    float target,
    float& current_velocity,
    float max_velocity,
    float dt)
{
    // Compute signed distance remaining to the target.
    const float remaining = target - position;

    // Determine travel direction as +1 or -1.
    const float direction = (remaining > 0.0f) ? 1.0f : -1.0f;

    // Compute distance traveled this tick at maximum velocity.
    const float step = max_velocity * dt;

    // Snap to target when the step would reach or pass it.
    if (step >= std::fabs(remaining))
    {
        position = target;
        current_velocity = 0.0f;
    }
    else
    {
        // Move one step toward the target without overshooting.
        position += direction * step;
        current_velocity = direction * max_velocity;
    }
}

// Advance the axis toward its target using a trapezoidal velocity profile.
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

    // Hold zero velocity when the target is already reached.
    if (isAtTarget())
    {
        current_velocity_ = 0.0f;
        return;
    }

    // Do not move when maximum velocity is zero.
    if (max_velocity_ <= 0.0f)
    {
        return;
    }

    // Fall back to Milestone 2 constant-velocity motion when acceleration is zero.
    if (acceleration_ <= 0.0f)
    {
        tickConstantVelocity(position_, target_, current_velocity_, max_velocity_, dt);
        return;
    }

    // Compute signed distance remaining to the target.
    const float remaining = target_ - position_;

    // Determine travel direction toward the target.
    const float direction = (remaining > 0.0f) ? 1.0f : -1.0f;

    // Select acceleration for this tick based on the motion profile.
    const float a_cmd = computeSignedAcceleration(
        remaining,
        current_velocity_,
        max_velocity_,
        acceleration_);

    // Integrate velocity over the time step.
    float v_new = current_velocity_ + a_cmd * dt;

    // Prevent velocity from crossing zero when decelerating.
    if (current_velocity_ > 0.0f && a_cmd < 0.0f && v_new < 0.0f)
    {
        v_new = 0.0f;
    }
    else if (current_velocity_ < 0.0f && a_cmd > 0.0f && v_new > 0.0f)
    {
        v_new = 0.0f;
    }

    // Limit commanded speed to the maximum velocity when moving toward target.
    if (v_new * direction > 0.0f && std::fabs(v_new) > max_velocity_)
    {
        v_new = direction * max_velocity_;
    }

    // Integrate position using the average velocity over the time step.
    const float delta = (current_velocity_ + v_new) * 0.5f * dt;

    // Clamp to the target when the step would reach or pass it.
    if (std::fabs(delta) >= std::fabs(remaining) - kPositionTolerance)
    {
        position_ = target_;
        current_velocity_ = 0.0f;
    }
    else
    {
        // Apply the position update without overshooting.
        position_ += delta;
        current_velocity_ = v_new;
    }

    // Snap to target and stop when within tolerance after the update.
    if (isAtTarget())
    {
        position_ = target_;
        current_velocity_ = 0.0f;
    }
}
