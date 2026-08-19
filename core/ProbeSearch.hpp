#pragma once

#include "Machine.hpp"
#include "ProbeGeometry.hpp"
#include "TestPoint.hpp"
#include "Types.hpp"

#include <cstdint>

// Non-blocking replacement for ABB SearchL: approach, fast search until
// authoritative probe trigger, backoff, optional fine searches, retreat.
class ProbeSearch
{
public:
    enum class State : uint8_t
    {
        Idle = 0,
        RaiseApproach = 1,
        TraverseStart = 2,
        Descend = 3,
        FastSearch = 4,
        Backoff = 5,
        FineSearch = 6,
        Retreat = 7,
        Complete = 8,
        Error = 9
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
    void commandApproachRaise(Machine& machine);
    void commandTraverseStart(Machine& machine);
    void commandDescend(Machine& machine);
    void commandSearch(Machine& machine, float speed);
    void commandBackoff(Machine& machine, const TestPoint& trigger_pose);
    void commandRetreat(Machine& machine);
    void enterError();
    bool motionSettled(const Machine& machine) const;
    bool faulted(const Machine& machine) const;
    TestPoint startPose() const;
    TestPoint searchTarget() const;
    TestPoint backoffPose(const TestPoint& trigger_pose) const;
    bool consumeTrigger(Machine& machine, TestPoint& pose_out);

    State state_ = State::Idle;
    Request request_{};
    TestPoint result_{};
    TestPoint fine_sum_{};
    unsigned fine_done_ = 0;
    bool have_fast_ = false;
};
