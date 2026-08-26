#pragma once

#include "Axis.hpp"
#include "Types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

// MotionPlanner coordinates three Axis instances (X/Y/Z) with sequential
// single-axis legs and optional two-axis corner blending.
 //
 // Leg order (Safe-Z compatible):
 //   1) Z-up if target.z > current.z
 //   2) X
 //   3) Y
 //   4) Z-down if target.z < current.z
 //
 // When corner_radius > 0 and the next leg uses a different axis, the planner
 // starts the next axis once the remaining distance on the active axis is
 // <= corner_radius (both axes tick during the blend). Same-axis reversals
 // never blend. corner_radius == 0 restores full-stop handoff between legs.
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

    // Targets are in machine coordinates. Multi-axis goals are queued as
    // sequential single-axis legs (Z-up → X → Y → Z-down). Identical goals
    // while Moving/AtTarget are a no-op so callers may re-issue every tick.
    void moveTo(float x, float y, float z);

    // Update goal X/Y (force-based XY corrections). Same sequential rules;
    // an in-progress Z leg toward the unchanged Z goal is not interrupted.
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

    void setMotionParams(float maxVelocity, float acceleration, float corner_radius = 0.0f);

    // Command the active (and blend partner) axis to decelerate to a hold at the
    // current pose using the given acceleration (probe trigger brake).
    void brakeToStop(float acceleration);

    std::vector<TcpPose> sampleWaypointPath(const TcpPose* waypoints,
                                            std::size_t waypoint_count,
                                            float max_velocity,
                                            float acceleration,
                                            float dt,
                                            float max_time_per_leg,
                                            float corner_radius = 0.0f) const;

private:
    enum class LegAxis : uint8_t
    {
        X = 0,
        Y = 1,
        Z = 2
    };

    struct Leg
    {
        LegAxis axis = LegAxis::X;
        float machine_target = 0.0f;
    };

    static constexpr uint8_t kMaxLegs = 4;

    void clearLegs();
    void holdAllAxes();
    void buildLegsFromCurrent(float tx, float ty, float tz);
    void startLeg(uint8_t index);
    void startNextLegWithoutHold();
    void advanceToNextLeg();
    bool nearGoal(float a, float b) const;
    bool currentLegIsZToGoal() const;
    void softUpdateXyDuringZ(float tx, float ty);
    Axis& axisFor(LegAxis axis);
    const Axis& axisFor(LegAxis axis) const;
    float remainingOnLeg(uint8_t index) const;
    bool canStartBlend() const;
    void beginBlend();
    void tickAxis(LegAxis axis, float dt);
    bool axisAtTarget(LegAxis axis) const;

    Axis axis_x_;
    Axis axis_y_;
    Axis axis_z_;

    // machine = raw - offset
    float offset_x_;
    float offset_y_;
    float offset_z_;

    MotionState motion_state_;

    Leg legs_[kMaxLegs]{};
    uint8_t leg_count_ = 0;
    uint8_t leg_index_ = 0;
    bool blending_ = false;

    float corner_radius_ = 0.0f;

    float goal_x_ = 0.0f;
    float goal_y_ = 0.0f;
    float goal_z_ = 0.0f;
    bool has_goal_ = false;
};
