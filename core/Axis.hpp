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

    // Return the commanded maximum velocity magnitude.
    float velocity() const;

    // Return the current signed velocity.
    float currentVelocity() const;

    // Return the commanded acceleration magnitude.
    float acceleration() const;

    // Return whether the axis accepts motion updates.
    bool enabled() const;

    // Set the current axis position.
    void setPosition(float position);

    // Set the commanded target position.
    void setTarget(float target);

    // Set the commanded maximum velocity magnitude (must be non-negative).
    void setVelocity(float velocity);

    // Set the commanded acceleration magnitude (must be non-negative).
    void setAcceleration(float acceleration);

    // Enable or disable axis motion updates.
    void setEnabled(bool enabled);

    // Set position, target, and zero current velocity (home offset snap).
    void snapTo(float position);

    // Return true when the axis is within tolerance of its target.
    bool isAtTarget() const;

    // Advance the axis toward its target using a trapezoidal profile.
    void tick(float dt);

private:
    // Current axis position.
    float position_;

    // Commanded target position.
    float target_;

    // Commanded maximum velocity magnitude toward the target.
    float max_velocity_;

    // Current signed velocity in the direction of motion.
    float current_velocity_;

    // Commanded acceleration magnitude.
    float acceleration_;

    // Whether tick() may move the axis.
    bool enabled_;
};
