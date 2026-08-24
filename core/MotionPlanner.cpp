#include "MotionPlanner.hpp"

#include <cmath>
#include <vector>

MotionPlanner::MotionPlanner()
    : axis_x_()
    , axis_y_()
    , axis_z_()
    , offset_x_(0.0f)
    , offset_y_(0.0f)
    , offset_z_(0.0f)
    , motion_state_(MotionState::Idle)
{
}

void MotionPlanner::setMotionParams(float maxVelocity, float acceleration)
{
    axis_x_.setVelocity(maxVelocity);
    axis_x_.setAcceleration(acceleration);

    axis_y_.setVelocity(maxVelocity);
    axis_y_.setAcceleration(acceleration);

    axis_z_.setVelocity(maxVelocity);
    axis_z_.setAcceleration(acceleration);
}

void MotionPlanner::moveTo(float x, float y, float z)
{
    axis_x_.setEnabled(true);
    axis_y_.setEnabled(true);
    axis_z_.setEnabled(true);

    // Machine targets → raw axis targets.
    axis_x_.setTarget(x + offset_x_);
    axis_y_.setTarget(y + offset_y_);
    axis_z_.setTarget(z + offset_z_);

    const bool all_at_target = axis_x_.isAtTarget() && axis_y_.isAtTarget() && axis_z_.isAtTarget();
    motion_state_ = all_at_target ? MotionState::AtTarget : MotionState::Moving;
}

void MotionPlanner::setXYTargets(float x, float y)
{
    axis_x_.setTarget(x + offset_x_);
    axis_y_.setTarget(y + offset_y_);
}

void MotionPlanner::tick(float dt)
{
    if (dt <= 0.0f)
    {
        return;
    }

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
    motion_state_ = MotionState::Idle;

    const float x = axis_x_.position();
    const float y = axis_y_.position();
    const float z = axis_z_.position();

    axis_x_.setTarget(x);
    axis_y_.setTarget(y);
    axis_z_.setTarget(z);

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
    return axis_x_.position() - offset_x_;
}

float MotionPlanner::y() const
{
    return axis_y_.position() - offset_y_;
}

float MotionPlanner::z() const
{
    return axis_z_.position() - offset_z_;
}

float MotionPlanner::rawX() const
{
    return axis_x_.position();
}

float MotionPlanner::rawY() const
{
    return axis_y_.position();
}

float MotionPlanner::rawZ() const
{
    return axis_z_.position();
}

float MotionPlanner::referenceOffsetX() const
{
    return offset_x_;
}

float MotionPlanner::referenceOffsetY() const
{
    return offset_y_;
}

float MotionPlanner::referenceOffsetZ() const
{
    return offset_z_;
}

float MotionPlanner::zVelocity() const
{
    return axis_z_.currentVelocity();
}

void MotionPlanner::setCurrentPose(float x, float y, float z)
{
    offset_x_ = 0.0f;
    offset_y_ = 0.0f;
    offset_z_ = 0.0f;
    axis_x_.snapTo(x);
    axis_y_.snapTo(y);
    axis_z_.snapTo(z);
    motion_state_ = MotionState::Idle;
}

void MotionPlanner::referenceMachinePose(float mx, float my, float mz)
{
    // Keep raw pose; choose offsets so machine coords become (mx,my,mz).
    offset_x_ = axis_x_.position() - mx;
    offset_y_ = axis_y_.position() - my;
    offset_z_ = axis_z_.position() - mz;

    // Hold current raw pose (no motion from re-reference alone).
    axis_x_.setTarget(axis_x_.position());
    axis_y_.setTarget(axis_y_.position());
    axis_z_.setTarget(axis_z_.position());
    motion_state_ = MotionState::Idle;
}

void MotionPlanner::shiftRawPose(float dx, float dy, float dz)
{
    // Keep reference offsets (MCS). Only the continuous integrator jumps — models step loss.
    axis_x_.snapTo(axis_x_.position() + dx);
    axis_y_.snapTo(axis_y_.position() + dy);
    axis_z_.snapTo(axis_z_.position() + dz);
}

std::vector<MotionPlanner::TcpPose> MotionPlanner::sampleWaypointPath(
    const TcpPose* waypoints,
    std::size_t waypoint_count,
    float max_velocity,
    float acceleration,
    float dt,
    float max_time_per_leg) const
{
    std::vector<TcpPose> samples;
    if (waypoints == nullptr || waypoint_count == 0 || dt <= 0.0f || max_time_per_leg <= 0.0f)
    {
        return samples;
    }

    MotionPlanner sim = *this;
    sim.setMotionParams(max_velocity, acceleration);
    samples.push_back(TcpPose{sim.x(), sim.y(), sim.z()});

    for (std::size_t i = 0; i < waypoint_count; ++i)
    {
        const TcpPose& wp = waypoints[i];
        sim.moveTo(wp.x, wp.y, wp.z);
        float elapsed = 0.0f;
        while (sim.motionState() == MotionState::Moving && elapsed < max_time_per_leg)
        {
            sim.tick(dt);
            elapsed += dt;
            samples.push_back(TcpPose{sim.x(), sim.y(), sim.z()});
        }
        if (sim.motionState() != MotionState::AtTarget)
        {
            break;
        }
    }
    return samples;
}
