#pragma once

#include "Axis.hpp"
#include "Types.hpp"

// MotionPlanner coordinates three independent Axis instances (X/Y/Z) to reach a common target pose.
class MotionPlanner
{
public:
    MotionPlanner();

    // Set pose targets for X/Y/Z.
    void moveTo(float x, float y, float z);

    // Non-blocking progress update.
    void tick(float dt);

    // Freeze the current pose deterministically (no further motion while in Idle).
    void stopMotion();

    // Return the current motion state.
    MotionState motionState() const;

    // Pose getters.
    float x() const;
    float y() const;
    float z() const;

    // Set the same motion parameters for all axes.
    void setMotionParams(float maxVelocity, float acceleration);

private:
    Axis axis_x_;
    Axis axis_y_;
    Axis axis_z_;

    MotionState motion_state_;
};

