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

void MotionPlanner::setMotionParams(float maxVelocity, float acceleration, float corner_radius)
{
    axis_x_.setVelocity(maxVelocity);
    axis_x_.setAcceleration(acceleration);

    axis_y_.setVelocity(maxVelocity);
    axis_y_.setAcceleration(acceleration);

    axis_z_.setVelocity(maxVelocity);
    axis_z_.setAcceleration(acceleration);

    corner_radius_ = (corner_radius < 0.0f) ? 0.0f : corner_radius;
}

void MotionPlanner::brakeToStop(float acceleration)
{
    const float a = (acceleration < 0.0f) ? 0.0f : acceleration;
    blending_ = false;
    // Hold at the current raw pose and raise accel so the trapezoid stops quickly.
    axis_x_.setAcceleration(a);
    axis_y_.setAcceleration(a);
    axis_z_.setAcceleration(a);
    const float px = axis_x_.position();
    const float py = axis_y_.position();
    const float pz = axis_z_.position();
    axis_x_.setTarget(px);
    axis_y_.setTarget(py);
    axis_z_.setTarget(pz);
    clearLegs();
    // Keep has_goal_ so Machine::tickActiveMotion re-issuing the same pose
    // does not rebuild legs and cancel the deceleration. Goals are machine-frame.
    goal_x_ = px - offset_x_;
    goal_y_ = py - offset_y_;
    goal_z_ = pz - offset_z_;
    has_goal_ = true;
    motion_state_ = MotionState::Moving;
}

void MotionPlanner::moveTo(float x, float y, float z)
{
    axis_x_.setEnabled(true);
    axis_y_.setEnabled(true);
    axis_z_.setEnabled(true);

    // Re-issuing the same goal while already executing/finished: keep progress.
    // Machine::tickActiveMotion calls moveTo every tick with unchanged targets.
    // Do not clear blending_ here — that would cancel an in-progress corner blend.
    if (has_goal_ && nearGoal(x, goal_x_) && nearGoal(y, goal_y_) && nearGoal(z, goal_z_) &&
        (motion_state_ == MotionState::Moving || motion_state_ == MotionState::AtTarget))
    {
        return;
    }

    // Soft XY update during an in-progress Z leg to the same Z goal (pre-clamp).
    if (has_goal_ && motion_state_ == MotionState::Moving && nearGoal(z, goal_z_) &&
        currentLegIsZToGoal() && (!nearGoal(x, goal_x_) || !nearGoal(y, goal_y_)))
    {
        softUpdateXyDuringZ(x, y);
        return;
    }

    // New / changed goal: drop any active blend and rebuild legs.
    blending_ = false;
    goal_x_ = x;
    goal_y_ = y;
    goal_z_ = z;
    has_goal_ = true;

    buildLegsFromCurrent(x, y, z);
    if (leg_count_ == 0)
    {
        holdAllAxes();
        motion_state_ = MotionState::AtTarget;
        return;
    }

    leg_index_ = 0;
    startLeg(0);
    motion_state_ = MotionState::Moving;
}

void MotionPlanner::setXYTargets(float x, float y)
{
    // Keep current Z goal (or current pose if none) and apply sequential moveTo rules.
    const float tz = has_goal_ ? goal_z_ : z();
    moveTo(x, y, tz);
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

    // Brake-to-stop path: no legs, just settle all axes to held targets.
    if (leg_count_ == 0)
    {
        if (motion_state_ == MotionState::Moving)
        {
            axis_x_.tick(dt);
            axis_y_.tick(dt);
            axis_z_.tick(dt);
            if (axis_x_.isAtTarget() && axis_y_.isAtTarget() && axis_z_.isAtTarget())
            {
                motion_state_ = MotionState::AtTarget;
            }
        }
        return;
    }

    if (motion_state_ == MotionState::AtTarget || leg_index_ >= leg_count_)
    {
        return;
    }

    if (blending_)
    {
        const LegAxis outgoing = legs_[leg_index_].axis;
        const LegAxis incoming = legs_[leg_index_ + 1].axis;
        tickAxis(outgoing, dt);
        tickAxis(incoming, dt);
        if (axisAtTarget(outgoing))
        {
            ++leg_index_;
            blending_ = false;
            if (canStartBlend())
            {
                beginBlend();
            }
            else if (axisAtTarget(incoming))
            {
                advanceToNextLeg();
            }
        }
        return;
    }

    const LegAxis active = legs_[leg_index_].axis;
    tickAxis(active, dt);

    // Finish the current leg before considering a blend (remaining==0 still
    // satisfies remaining <= R and would otherwise re-enter beginBlend forever).
    if (axisAtTarget(active))
    {
        advanceToNextLeg();
        return;
    }

    if (canStartBlend())
    {
        beginBlend();
    }
}

void MotionPlanner::stopMotion()
{
    motion_state_ = MotionState::Idle;
    clearLegs();
    has_goal_ = false;
    blending_ = false;

    holdAllAxes();

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
    clearLegs();
    has_goal_ = false;
    blending_ = false;
    motion_state_ = MotionState::Idle;
}

void MotionPlanner::referenceMachinePose(float mx, float my, float mz)
{
    // Keep raw pose; choose offsets so machine coords become (mx,my,mz).
    offset_x_ = axis_x_.position() - mx;
    offset_y_ = axis_y_.position() - my;
    offset_z_ = axis_z_.position() - mz;

    clearLegs();
    has_goal_ = false;
    blending_ = false;
    holdAllAxes();
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
    float max_time_per_leg,
    float corner_radius) const
{
    std::vector<TcpPose> samples;
    if (waypoints == nullptr || waypoint_count == 0 || dt <= 0.0f || max_time_per_leg <= 0.0f)
    {
        return samples;
    }

    MotionPlanner sim = *this;
    sim.setMotionParams(max_velocity, acceleration, corner_radius);
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

void MotionPlanner::clearLegs()
{
    leg_count_ = 0;
    leg_index_ = 0;
    blending_ = false;
}

void MotionPlanner::holdAllAxes()
{
    axis_x_.setTarget(axis_x_.position());
    axis_y_.setTarget(axis_y_.position());
    axis_z_.setTarget(axis_z_.position());
}

void MotionPlanner::buildLegsFromCurrent(float tx, float ty, float tz)
{
    clearLegs();
    const float cx = x();
    const float cy = y();
    const float cz = z();

    // Z-up before any XY traverse when the target is above the current pose.
    if (tz > cz + kPositionTolerance)
    {
        legs_[leg_count_].axis = LegAxis::Z;
        legs_[leg_count_].machine_target = tz;
        ++leg_count_;
    }

    if (std::fabs(tx - cx) > kPositionTolerance)
    {
        legs_[leg_count_].axis = LegAxis::X;
        legs_[leg_count_].machine_target = tx;
        ++leg_count_;
    }

    if (std::fabs(ty - cy) > kPositionTolerance)
    {
        legs_[leg_count_].axis = LegAxis::Y;
        legs_[leg_count_].machine_target = ty;
        ++leg_count_;
    }

    // Z-down after XY when the target is below the start pose (XY ran at start Z).
    if (tz + kPositionTolerance < cz)
    {
        legs_[leg_count_].axis = LegAxis::Z;
        legs_[leg_count_].machine_target = tz;
        ++leg_count_;
    }
}

void MotionPlanner::startLeg(uint8_t index)
{
    holdAllAxes();
    if (index >= leg_count_)
    {
        return;
    }

    const Leg& leg = legs_[index];
    switch (leg.axis)
    {
    case LegAxis::X:
        axis_x_.setTarget(leg.machine_target + offset_x_);
        break;
    case LegAxis::Y:
        axis_y_.setTarget(leg.machine_target + offset_y_);
        break;
    case LegAxis::Z:
        axis_z_.setTarget(leg.machine_target + offset_z_);
        break;
    }
}

void MotionPlanner::startNextLegWithoutHold()
{
    if (leg_index_ + 1 >= leg_count_)
    {
        return;
    }
    const Leg& leg = legs_[leg_index_ + 1];
    switch (leg.axis)
    {
    case LegAxis::X:
        axis_x_.setTarget(leg.machine_target + offset_x_);
        break;
    case LegAxis::Y:
        axis_y_.setTarget(leg.machine_target + offset_y_);
        break;
    case LegAxis::Z:
        axis_z_.setTarget(leg.machine_target + offset_z_);
        break;
    }
}

void MotionPlanner::advanceToNextLeg()
{
    blending_ = false;
    ++leg_index_;
    if (leg_index_ >= leg_count_)
    {
        holdAllAxes();
        motion_state_ = MotionState::AtTarget;
        return;
    }
    startLeg(leg_index_);
    motion_state_ = MotionState::Moving;
}

bool MotionPlanner::nearGoal(float a, float b) const
{
    return std::fabs(a - b) <= kPositionTolerance;
}

bool MotionPlanner::currentLegIsZToGoal() const
{
    if (leg_index_ >= leg_count_)
    {
        return false;
    }
    const Leg& leg = legs_[leg_index_];
    return leg.axis == LegAxis::Z && nearGoal(leg.machine_target, goal_z_);
}

void MotionPlanner::softUpdateXyDuringZ(float tx, float ty)
{
    goal_x_ = tx;
    goal_y_ = ty;
    blending_ = false;

    // Keep the active Z leg; replace any trailing legs with X then Y (Z already at goal).
    const uint8_t keep = static_cast<uint8_t>(leg_index_ + 1);
    leg_count_ = keep;

    const float cx = x();
    const float cy = y();
    if (std::fabs(tx - cx) > kPositionTolerance && leg_count_ < kMaxLegs)
    {
        legs_[leg_count_].axis = LegAxis::X;
        legs_[leg_count_].machine_target = tx;
        ++leg_count_;
    }
    if (std::fabs(ty - cy) > kPositionTolerance && leg_count_ < kMaxLegs)
    {
        legs_[leg_count_].axis = LegAxis::Y;
        legs_[leg_count_].machine_target = ty;
        ++leg_count_;
    }

    // Hold X/Y while Z continues (startLeg already held them when Z began).
    axis_x_.setTarget(axis_x_.position());
    axis_y_.setTarget(axis_y_.position());
    motion_state_ = MotionState::Moving;
}

Axis& MotionPlanner::axisFor(LegAxis axis)
{
    switch (axis)
    {
    case LegAxis::X:
        return axis_x_;
    case LegAxis::Y:
        return axis_y_;
    case LegAxis::Z:
    default:
        return axis_z_;
    }
}

const Axis& MotionPlanner::axisFor(LegAxis axis) const
{
    switch (axis)
    {
    case LegAxis::X:
        return axis_x_;
    case LegAxis::Y:
        return axis_y_;
    case LegAxis::Z:
    default:
        return axis_z_;
    }
}

float MotionPlanner::remainingOnLeg(uint8_t index) const
{
    if (index >= leg_count_)
    {
        return 0.0f;
    }
    const Leg& leg = legs_[index];
    float machine_pos = 0.0f;
    switch (leg.axis)
    {
    case LegAxis::X:
        machine_pos = x();
        break;
    case LegAxis::Y:
        machine_pos = y();
        break;
    case LegAxis::Z:
        machine_pos = z();
        break;
    }
    return std::fabs(leg.machine_target - machine_pos);
}

bool MotionPlanner::canStartBlend() const
{
    if (corner_radius_ <= 0.0f || blending_)
    {
        return false;
    }
    if (leg_index_ + 1u >= leg_count_)
    {
        return false;
    }
    if (legs_[leg_index_].axis == legs_[leg_index_ + 1u].axis)
    {
        return false;
    }
    // Already finished this leg: advanceToNextLeg handles the handoff.
    if (axisAtTarget(legs_[leg_index_].axis))
    {
        return false;
    }
    return remainingOnLeg(leg_index_) <= corner_radius_ + kPositionTolerance;
}

void MotionPlanner::beginBlend()
{
    startNextLegWithoutHold();
    blending_ = true;
    motion_state_ = MotionState::Moving;
}

void MotionPlanner::tickAxis(LegAxis axis, float dt)
{
    axisFor(axis).tick(dt);
}

bool MotionPlanner::axisAtTarget(LegAxis axis) const
{
    return axisFor(axis).isAtTarget();
}
