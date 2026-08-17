#include "MotionPlanner.hpp"

// Construct a planner in the idle state.
MotionPlanner::MotionPlanner()
    : axis_x_()
    , axis_y_()
    , axis_z_()
    , motion_state_(MotionState::Idle)
{
}

void MotionPlanner::setMotionParams(float maxVelocity, float acceleration)
{
    // Axis clamps negative values to 0, keeping behavior deterministic.
    axis_x_.setVelocity(maxVelocity);
    axis_x_.setAcceleration(acceleration);

    axis_y_.setVelocity(maxVelocity);
    axis_y_.setAcceleration(acceleration);

    axis_z_.setVelocity(maxVelocity);
    axis_z_.setAcceleration(acceleration);
}

void MotionPlanner::moveTo(float x, float y, float z)
{
    // Always accept new move commands for a planner-driven motion.
    axis_x_.setEnabled(true);
    axis_y_.setEnabled(true);
    axis_z_.setEnabled(true);

    axis_x_.setTarget(x);
    axis_y_.setTarget(y);
    axis_z_.setTarget(z);

    const bool all_at_target = axis_x_.isAtTarget() && axis_y_.isAtTarget() && axis_z_.isAtTarget();
    motion_state_ = all_at_target ? MotionState::AtTarget : MotionState::Moving;
}

void MotionPlanner::setXYTargets(float x, float y)
{
    // Only update axis targets; do not touch motion_state_ or axis positions.
    axis_x_.setTarget(x);
    axis_y_.setTarget(y);
}

void MotionPlanner::tick(float dt)
{
    // Do not advance time with non-positive dt.
    if (dt <= 0.0f)
    {
        return;
    }

    // Idle means: do not execute axis updates.
    if (motion_state_ == MotionState::Idle)
    {
        return;
    }

    axis_x_.tick(dt);
    axis_y_.tick(dt);
    axis_z_.tick(dt);

    const bool all_at_target = axis_x_.isAtTarget() && axis_y_.isAtTarget() && axis_z_.isAtTarget();
    motion_state_ = all_at_target ? MotionState::AtTarget : MotionState::Moving;
}

void MotionPlanner::stopMotion()
{
    // Freeze semantics: switch the planner to Idle and make each Axis its own target.
    // This avoids using Axis::setEnabled(false) as a substitute for MotionPlanner state.
    motion_state_ = MotionState::Idle;

    const float x = axis_x_.position();
    const float y = axis_y_.position();
    const float z = axis_z_.position();

    axis_x_.setTarget(x);
    axis_y_.setTarget(y);
    axis_z_.setTarget(z);

    // Ensure further tick(dt) calls (if ever executed) cannot start a new motion.
    axis_x_.setVelocity(0.0f);
    axis_y_.setVelocity(0.0f);
    axis_z_.setVelocity(0.0f);
}

MotionState MotionPlanner::motionState() const
{
    return motion_state_;
}

float MotionPlanner::x() const
{
    return axis_x_.position();
}

float MotionPlanner::y() const
{
    return axis_y_.position();
}

float MotionPlanner::z() const
{
    return axis_z_.position();
}

void MotionPlanner::setCurrentPose(float x, float y, float z)
{
    axis_x_.snapTo(x);
    axis_y_.snapTo(y);
    axis_z_.snapTo(z);
    motion_state_ = MotionState::Idle;
}

