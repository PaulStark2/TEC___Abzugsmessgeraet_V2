// Platform-independent single-axis motion model.
#pragma once

// Axis holds kinematic state and performs non-blocking updates.
class Axis
{
public:
    // Construct an axis at the origin with zero velocity and enabled.
    Axis();

    // Return the current axis position.
    float position() const;

    // Return the commanded target position.
    float target() const;

    // Return the commanded velocity magnitude.
    float velocity() const;

    // Return whether the axis accepts motion updates.
    bool enabled() const;

    // Set the current axis position.
    void setPosition(float position);

    // Set the commanded target position.
    void setTarget(float target);

    // Set the commanded velocity magnitude (must be non-negative).
    void setVelocity(float velocity);

    // Enable or disable axis motion updates.
    void setEnabled(bool enabled);

    // Return true when the axis is within tolerance of its target.
    bool isAtTarget() const;

    // Advance the axis toward its target using constant velocity.
    void tick(float dt);

private:
    // Current axis position.
    float position_;

    // Commanded target position.
    float target_;

    // Commanded velocity magnitude toward the target.
    float velocity_;

    // Whether tick() may move the axis.
    bool enabled_;
};
