#pragma once

#include "Machine.hpp"
#include "ProbeGeometry.hpp"
#include "TestPoint.hpp"
#include "Types.hpp"

#include <cstdint>

// Non-blocking replacement for ABB SearchL: hub transfer, directional start,
// trigger-driven search until probe contact or timeout, backoff, optional fine
// search, retreat. Contact search / backoff stay single-axis. Safe transfer to
// hub uses one multi-axis moveTo so corner_probe_transfer can blend X/Y (and Z-up).
//
// request.reference is the hub TCP (operator jog or ProbeApproach). It is not an
// assumed disc center and not a search destination.
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

    enum class ErrorReason : uint8_t
    {
        None = 0,
        NoContact = 1,
        Fault = 2,
        InvalidRequest = 3,
        Aborted = 4
    };

    struct Request
    {
        TestPoint reference{}; // hub TCP pose (not disc center)
        ProbeSearchDir dir = ProbeSearchDir::NegZ;
        float approach_distance = 10.0f; // Z max-travel fallback together with search_*
        float search_distance = 10.0f;
        float search_depth = 5.0f; // XY work height = measured_center_z − search_depth
        float measured_center_z = 0.0f; // required for XY; unused for Z
        bool have_measured_center_z = false;
        float lateral_offset = 10.0f; // start distance from hub along the search axis
        float xy_keepout_radius = 0.0f; // last-resort XY bound toward hub; 0 = hub axis
        float backoff = 1.0f;
        float approach_speed = 10.0f;
        float transfer_speed = 10.0f; // backoff / retreat / lateral hub transfers
        float fast_speed = 2.0f;
        float fine_speed = 1.0f;
        float max_search_travel = 0.0f; // 0 = derive from request distances
        float search_timeout_s = 0.0f; // 0 = derive from travel / speed
        unsigned num_fine_searches = 0;
    };

    bool start(Machine& machine, const Request& request);
    void tick(float dt, Machine& machine);
    void abort();

    bool isComplete() const;
    bool isError() const;
    State state() const;
    ErrorReason errorReason() const;
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
    void beginSearch(Machine& machine, float speed, State next);
    void enterError(ErrorReason reason);
    bool motionSettled(const Machine& machine) const;
    bool faulted(const Machine& machine) const;
    TestPoint hubPose() const;
    TestPoint startPose() const;
    TestPoint searchBound() const;
    TestPoint backoffPose(const TestPoint& trigger_pose) const;
    float maxSearchTravel() const;
    float xyWorkZ() const;
    float searchTimeoutS(float speed) const;
    bool tryContact(Machine& machine, TestPoint& pose_out);
    void advanceAfterHubAligned(Machine& machine);
    void applyApproachProfile(Machine& machine) const;
    void applyTransferProfile(Machine& machine) const;
    void applySearchProfile(Machine& machine, float speed) const;
    void onProbeTrigger(Machine& machine, const TestPoint& trigger);

    State state_ = State::Idle;
    ErrorReason error_reason_ = ErrorReason::None;
    Request request_{};
    TestPoint result_{};
    TestPoint fine_sum_{};
    TestPoint brake_trigger_{};
    unsigned fine_done_ = 0;
    bool have_fast_ = false;
    bool braking_ = false; // waiting for brakeToStop settle before backoff/retreat
    bool retreat_after_brake_ = false;
    float search_elapsed_s_ = 0.0f;
    float active_search_timeout_s_ = 0.0f;
};
