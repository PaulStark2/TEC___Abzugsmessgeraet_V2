#pragma once

#include "Machine.hpp"
#include "ProbeGeometry.hpp"
#include "TestPoint.hpp"
#include "Types.hpp"

#include <cstdint>

// Non-blocking replacement for ABB SearchL: approach, fast search until
// authoritative probe trigger, backoff, optional fine searches, retreat.
// Contact search / backoff stay single-axis. Safe transfer to hub uses one
 // multi-axis moveTo so corner_probe_transfer can blend X/Y (and Z-up).
class ProbeSearch
{
public:
    enum class State : uint8_t
    {
        Idle = 0,
        AlignHub = 1, // raise + XY to hub in one moveTo (was Raise/AlignX/AlignY)
        // Kept for wire/debug compatibility with older logs; unused by FSM.
        AlignHubX = 2,
        AlignHubY = 3,
        TraverseStart = 4,
        Descend = 5,
        FastSearch = 6,
        Backoff = 7,
        FineSearch = 8,
        RetreatClear = 9,
        RetreatToHub = 10, // raise + XY to hub (replaces RetreatRaise + RetreatHub)
        RetreatHub = 11, // legacy alias unused
        Complete = 12,
        Error = 13
    };

    struct Request
    {
        TestPoint reference{};
        ProbeSearchDir dir = ProbeSearchDir::NegZ;
        float approach_distance = 10.0f;
        float search_distance = 10.0f;
        float search_depth = 5.0f;
        float lateral_offset = 10.0f;
        float xy_inner_radius = 0.0f; // XY motion stop: |TCP_xy − probe axis|; 0 = unused (Z)
        float backoff = 1.0f;
        float approach_speed = 10.0f;
        float transfer_speed = 10.0f; // backoff / retreat / lateral hub transfers
        float fast_speed = 2.0f;
        float fine_speed = 1.0f;
        unsigned num_fine_searches = 0;
    };

    bool start(Machine& machine, const Request& request);
    void tick(float dt, Machine& machine);
    void abort();

    bool isComplete() const;
    bool isError() const;
    State state() const;
    TestPoint result() const; // TCP pose at trigger (averaged if fine searches ran)

private:
    void commandAlignHub(Machine& machine);
    void commandTraverseStart(Machine& machine);
    void commandDescend(Machine& machine);
    void commandSearch(Machine& machine, float speed);
    void commandBackoff(Machine& machine, const TestPoint& trigger_pose);
    void commandRetreatClear(Machine& machine);
    void commandRetreatToHub(Machine& machine);
    void beginRetreat(Machine& machine);
    void enterError();
    bool motionSettled(const Machine& machine) const;
    bool faulted(const Machine& machine) const;
    TestPoint hubPose() const;
    TestPoint startPose() const;
    TestPoint searchTarget() const;
    TestPoint backoffPose(const TestPoint& trigger_pose) const;
    bool consumeTrigger(Machine& machine, TestPoint& pose_out);
    void advanceAfterHubAligned(Machine& machine);
    void applyApproachProfile(Machine& machine) const;
    void applyTransferProfile(Machine& machine) const;
    void applySearchProfile(Machine& machine, float speed) const;
    void onProbeTrigger(Machine& machine, const TestPoint& trigger);

    State state_ = State::Idle;
    Request request_{};
    TestPoint result_{};
    TestPoint fine_sum_{};
    TestPoint brake_trigger_{};
    unsigned fine_done_ = 0;
    bool have_fast_ = false;
    bool braking_ = false; // waiting for brakeToStop settle before backoff/retreat
    bool retreat_after_brake_ = false;
};
