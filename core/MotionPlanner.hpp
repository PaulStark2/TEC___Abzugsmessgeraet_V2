#pragma once

#include "Axis.hpp"
#include "Types.hpp"

#include <cstddef>
#include <vector>

// MotionPlanner coordinates three independent Axis instances (X/Y/Z).
//
 // Axis::position is the continuous commanded/raw pose (open-loop integration).
 // Machine coordinates are raw - reference_offset. Homing re-reference adjusts
 // offsets only — raw pose does not teleport.
class MotionPlanner
{
public:
    struct TcpPose
    {
        float x;
        float y;
        float z;
    };

    MotionPlanner();

    // Targets are in machine coordinates.
    void moveTo(float x, float y, float z);

    // Update only the X/Y machine targets (for force-based XY corrections).
    void setXYTargets(float x, float y);

    void tick(float dt);

    void stopMotion();

    MotionState motionState() const;

    // Machine-frame pose (raw - reference offset).
    float x() const;
    float y() const;
    float z() const;
    float zVelocity() const;

    // Continuous commanded/raw pose (does not jump on machine re-reference).
    float rawX() const;
    float rawY() const;
    float rawZ() const;

    // reference_offset: machine = raw - offset
    float referenceOffsetX() const;
    float referenceOffsetY() const;
    float referenceOffsetZ() const;

    // Snap raw pose and clear reference offsets (test inject / absolute set).
    void setCurrentPose(float x, float y, float z);

    // Re-reference machine frame at the current raw pose so machine coords become
    // (mx,my,mz) without moving the axes (no teleport of raw pose).
    void referenceMachinePose(float mx, float my, float mz);

    // Test/sim helper: shift continuous raw pose without changing reference offsets
    // (models open-loop step loss while MCS stays defined).
    void shiftRawPose(float dx, float dy, float dz);

    void setMotionParams(float maxVelocity, float acceleration);

    std::vector<TcpPose> sampleWaypointPath(const TcpPose* waypoints,
                                            std::size_t waypoint_count,
                                            float max_velocity,
                                            float acceleration,
                                            float dt,
                                            float max_time_per_leg) const;

private:
    Axis axis_x_;
    Axis axis_y_;
    Axis axis_z_;

    // machine = raw - offset
    float offset_x_;
    float offset_y_;
    float offset_z_;

    MotionState motion_state_;
};
