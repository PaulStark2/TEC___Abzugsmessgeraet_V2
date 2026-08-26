#include "ProbeSearch.hpp"

#include "HostInputs.hpp"

#include <cmath>

namespace
{

bool isXySearch(ProbeSearchDir dir)
{
    return dir != ProbeSearchDir::NegZ;
}

bool finiteRequest(const ProbeSearch::Request& request)
{
    if (!(std::isfinite(request.reference.x) && std::isfinite(request.reference.y) &&
          std::isfinite(request.reference.z) && request.approach_distance > 0.0f &&
          request.search_distance > 0.0f && request.search_depth > 0.0f &&
          request.lateral_offset > 0.0f && request.xy_keepout_radius >= 0.0f && request.backoff >= 0.0f &&
          request.approach_speed > 0.0f && request.transfer_speed > 0.0f &&
          request.fast_speed > 0.0f && request.fine_speed > 0.0f && request.max_search_travel >= 0.0f &&
          request.search_timeout_s >= 0.0f))
    {
        return false;
    }
    if (isXySearch(request.dir))
    {
        if (!request.have_measured_center_z || !std::isfinite(request.measured_center_z))
        {
            return false;
        }
        if (!(request.lateral_offset > request.xy_keepout_radius))
        {
            return false;
        }
    }
    return true;
}

} // namespace

bool ProbeSearch::start(Machine& machine, const Request& request)
{
    if (machine.machineState() == MachineState::Fault || !finiteRequest(request))
    {
        enterError(ErrorReason::InvalidRequest);
        return false;
    }

    request_ = request;
    result_ = TestPoint{};
    fine_sum_ = TestPoint{};
    brake_trigger_ = TestPoint{};
    fine_done_ = 0;
    have_fast_ = false;
    braking_ = false;
    retreat_after_brake_ = false;
    search_elapsed_s_ = 0.0f;
    active_search_timeout_s_ = 0.0f;
    error_reason_ = ErrorReason::None;
    machine.consumeProbeTrigger();
    machine.setCollisionContext(CollisionContext::Approach);
    commandAlignHub(machine);
    state_ = State::AlignHub;
    if (motionSettled(machine))
    {
        tick(0.0f, machine);
    }
    return state_ != State::Error;
}

void ProbeSearch::tick(float dt, Machine& machine)
{
    if (state_ == State::Idle || state_ == State::Complete || state_ == State::Error)
    {
        return;
    }
    if (faulted(machine))
    {
        enterError(ErrorReason::Fault);
        return;
    }

    switch (state_)
    {
    case State::AlignHub:
        if (motionSettled(machine))
        {
            advanceAfterHubAligned(machine);
            if (motionSettled(machine) && state_ != State::Complete && state_ != State::Error)
            {
                tick(0.0f, machine);
            }
        }
        break;
    case State::TraverseStart:
        if (motionSettled(machine))
        {
            commandDescend(machine);
            state_ = State::Descend;
            if (motionSettled(machine))
            {
                tick(0.0f, machine);
            }
        }
        break;
    case State::Descend:
        if (motionSettled(machine))
        {
            beginSearch(machine, request_.fast_speed, State::FastSearch);
        }
        break;
    case State::FastSearch:
    case State::FineSearch:
    {
        if (braking_)
        {
            if (!motionSettled(machine))
            {
                break;
            }
            braking_ = false;
            if (retreat_after_brake_)
            {
                beginRetreat(machine);
            }
            else
            {
                commandBackoff(machine, brake_trigger_);
                state_ = State::Backoff;
            }
            break;
        }
        if (dt > 0.0f)
        {
            search_elapsed_s_ += dt;
        }
        TestPoint trigger{};
        if (tryContact(machine, trigger))
        {
            onProbeTrigger(machine, trigger);
        }
        else if (search_elapsed_s_ > active_search_timeout_s_ || motionSettled(machine))
        {
            enterError(ErrorReason::NoContact);
        }
        break;
    }
    case State::Backoff:
        if (motionSettled(machine))
        {
            beginSearch(machine, request_.fine_speed, State::FineSearch);
        }
        break;
    case State::RetreatClear:
        if (motionSettled(machine))
        {
            if (!isXySearch(request_.dir))
            {
                if (!have_fast_ && fine_done_ == 0)
                {
                    enterError(ErrorReason::NoContact);
                    return;
                }
                state_ = State::Complete;
                return;
            }
            commandRetreatToHub(machine);
            state_ = State::RetreatToHub;
            if (motionSettled(machine))
            {
                tick(0.0f, machine);
            }
        }
        break;
    case State::RetreatToHub:
        if (motionSettled(machine))
        {
            if (!have_fast_ && fine_done_ == 0)
            {
                enterError(ErrorReason::NoContact);
                return;
            }
            state_ = State::Complete;
        }
        break;
    default:
        break;
    }
}

void ProbeSearch::abort()
{
    if (state_ != State::Idle && state_ != State::Complete)
    {
        enterError(ErrorReason::Aborted);
    }
}

bool ProbeSearch::isComplete() const
{
    return state_ == State::Complete;
}

bool ProbeSearch::isError() const
{
    return state_ == State::Error;
}

ProbeSearch::State ProbeSearch::state() const
{
    return state_;
}

ProbeSearch::ErrorReason ProbeSearch::errorReason() const
{
    return error_reason_;
}

TestPoint ProbeSearch::result() const
{
    return result_;
}

void ProbeSearch::commandAlignHub(Machine& machine)
{
    const TestPoint hub = hubPose();
    applyApproachProfile(machine);
    machine.moveTo(hub.x, hub.y, hub.z);
}

void ProbeSearch::commandTraverseStart(Machine& machine)
{
    const TestPoint start = startPose();
    applyTransferProfile(machine);
    switch (request_.dir)
    {
    case ProbeSearchDir::NegX:
    case ProbeSearchDir::PosX:
        machine.moveTo(start.x, machine.y(), machine.z());
        break;
    case ProbeSearchDir::NegY:
    case ProbeSearchDir::PosY:
        machine.moveTo(machine.x(), start.y, machine.z());
        break;
    case ProbeSearchDir::NegZ:
        break;
    }
}

void ProbeSearch::commandDescend(Machine& machine)
{
    applyApproachProfile(machine);
    machine.setCollisionContext(CollisionContext::Contact);
    machine.moveTo(machine.x(), machine.y(), xyWorkZ());
}

void ProbeSearch::commandSearch(Machine& machine, float speed)
{
    const TestPoint bound = searchBound();
    machine.setCollisionContext(CollisionContext::Contact);
    applySearchProfile(machine, speed);
    switch (request_.dir)
    {
    case ProbeSearchDir::NegX:
    case ProbeSearchDir::PosX:
        machine.moveTo(bound.x, machine.y(), machine.z());
        break;
    case ProbeSearchDir::NegY:
    case ProbeSearchDir::PosY:
        machine.moveTo(machine.x(), bound.y, machine.z());
        break;
    case ProbeSearchDir::NegZ:
        machine.moveTo(machine.x(), machine.y(), bound.z);
        break;
    }
}

void ProbeSearch::commandBackoff(Machine& machine, const TestPoint& trigger_pose)
{
    const TestPoint back = backoffPose(trigger_pose);
    applyTransferProfile(machine);
    switch (request_.dir)
    {
    case ProbeSearchDir::NegX:
    case ProbeSearchDir::PosX:
        machine.moveTo(back.x, machine.y(), machine.z());
        break;
    case ProbeSearchDir::NegY:
    case ProbeSearchDir::PosY:
        machine.moveTo(machine.x(), back.y, machine.z());
        break;
    case ProbeSearchDir::NegZ:
        machine.moveTo(machine.x(), machine.y(), back.z);
        break;
    }
}

void ProbeSearch::commandRetreatClear(Machine& machine)
{
    machine.setCollisionContext(CollisionContext::Approach);
    applyTransferProfile(machine);
    if (!isXySearch(request_.dir))
    {
        const TestPoint hub = hubPose();
        machine.moveTo(machine.x(), machine.y(), hub.z);
        return;
    }
    const TestPoint start = startPose();
    switch (request_.dir)
    {
    case ProbeSearchDir::NegX:
    case ProbeSearchDir::PosX:
        machine.moveTo(start.x, machine.y(), machine.z());
        break;
    case ProbeSearchDir::NegY:
    case ProbeSearchDir::PosY:
        machine.moveTo(machine.x(), start.y, machine.z());
        break;
    default:
        break;
    }
}

void ProbeSearch::commandRetreatToHub(Machine& machine)
{
    const TestPoint hub = hubPose();
    machine.setCollisionContext(CollisionContext::Approach);
    applyTransferProfile(machine);
    machine.moveTo(hub.x, hub.y, hub.z);
}

void ProbeSearch::beginRetreat(Machine& machine)
{
    commandRetreatClear(machine);
    state_ = State::RetreatClear;
    if (motionSettled(machine))
    {
        tick(0.0f, machine);
    }
}

void ProbeSearch::beginSearch(Machine& machine, float speed, State next)
{
    machine.consumeProbeTrigger();
    commandSearch(machine, speed);
    state_ = next;
    search_elapsed_s_ = 0.0f;
    active_search_timeout_s_ = searchTimeoutS(speed);
    TestPoint already{};
    if (tryContact(machine, already))
    {
        onProbeTrigger(machine, already);
    }
}

void ProbeSearch::advanceAfterHubAligned(Machine& machine)
{
    if (!isXySearch(request_.dir))
    {
        beginSearch(machine, request_.fast_speed, State::FastSearch);
        return;
    }
    commandTraverseStart(machine);
    state_ = State::TraverseStart;
}

void ProbeSearch::applyApproachProfile(Machine& machine) const
{
    const MachineConfig::Probe& probe = machine.machineConfig().probe;
    machine.setActiveMotionProfile(request_.approach_speed, probe.a_probe_transfer,
                                   probe.corner_probe_transfer);
}

void ProbeSearch::applyTransferProfile(Machine& machine) const
{
    const MachineConfig::Probe& probe = machine.machineConfig().probe;
    machine.setActiveMotionProfile(request_.transfer_speed, probe.a_probe_transfer,
                                   probe.corner_probe_transfer);
}

void ProbeSearch::applySearchProfile(Machine& machine, float speed) const
{
    const MachineConfig::Probe& probe = machine.machineConfig().probe;
    const float a = (probe.a_probe_brake > 0.0f) ? probe.a_probe_brake : probe.a_probe_search;
    machine.setActiveMotionProfile(speed, a, 0.0f);
}

void ProbeSearch::onProbeTrigger(Machine& machine, const TestPoint& trigger)
{
    brake_trigger_ = trigger;
    if (state_ == State::FastSearch)
    {
        have_fast_ = true;
        result_ = trigger;
        retreat_after_brake_ = (request_.num_fine_searches == 0);
    }
    else
    {
        fine_sum_.x += trigger.x;
        fine_sum_.y += trigger.y;
        fine_sum_.z += trigger.z;
        ++fine_done_;
        if (fine_done_ >= request_.num_fine_searches)
        {
            const float n = static_cast<float>(fine_done_);
            result_.x = fine_sum_.x / n;
            result_.y = fine_sum_.y / n;
            result_.z = fine_sum_.z / n;
            retreat_after_brake_ = true;
        }
        else
        {
            retreat_after_brake_ = false;
        }
    }
    machine.brakeToStop(machine.machineConfig().probe.a_probe_brake);
    braking_ = true;
}

void ProbeSearch::enterError(ErrorReason reason)
{
    braking_ = false;
    error_reason_ = reason;
    state_ = State::Error;
}

bool ProbeSearch::motionSettled(const Machine& machine) const
{
    return machine.motionState() == MotionState::AtTarget ||
           machine.machineState() == MachineState::Idle;
}

bool ProbeSearch::faulted(const Machine& machine) const
{
    return machine.machineState() == MachineState::Fault ||
           machine.machineState() == MachineState::Stopped;
}

TestPoint ProbeSearch::hubPose() const
{
    return request_.reference;
}

TestPoint ProbeSearch::startPose() const
{
    TestPoint p = hubPose();
    switch (request_.dir)
    {
    case ProbeSearchDir::NegX:
        p.x = request_.reference.x - request_.lateral_offset;
        break;
    case ProbeSearchDir::PosX:
        p.x = request_.reference.x + request_.lateral_offset;
        break;
    case ProbeSearchDir::NegY:
        p.y = request_.reference.y - request_.lateral_offset;
        break;
    case ProbeSearchDir::PosY:
        p.y = request_.reference.y + request_.lateral_offset;
        break;
    case ProbeSearchDir::NegZ:
        break;
    }
    return p;
}

TestPoint ProbeSearch::searchBound() const
{
    TestPoint p = hubPose();
    const float keep = request_.xy_keepout_radius;
    switch (request_.dir)
    {
    case ProbeSearchDir::NegX:
        p.x = request_.reference.x - keep;
        p.y = request_.reference.y;
        p.z = xyWorkZ();
        break;
    case ProbeSearchDir::PosX:
        p.x = request_.reference.x + keep;
        p.y = request_.reference.y;
        p.z = xyWorkZ();
        break;
    case ProbeSearchDir::NegY:
        p.x = request_.reference.x;
        p.y = request_.reference.y - keep;
        p.z = xyWorkZ();
        break;
    case ProbeSearchDir::PosY:
        p.x = request_.reference.x;
        p.y = request_.reference.y + keep;
        p.z = xyWorkZ();
        break;
    case ProbeSearchDir::NegZ:
        p.z = request_.reference.z - maxSearchTravel();
        break;
    }
    return p;
}

TestPoint ProbeSearch::backoffPose(const TestPoint& trigger_pose) const
{
    TestPoint p = trigger_pose;
    switch (request_.dir)
    {
    case ProbeSearchDir::NegX:
        p.x = trigger_pose.x - request_.backoff;
        break;
    case ProbeSearchDir::PosX:
        p.x = trigger_pose.x + request_.backoff;
        break;
    case ProbeSearchDir::NegY:
        p.y = trigger_pose.y - request_.backoff;
        break;
    case ProbeSearchDir::PosY:
        p.y = trigger_pose.y + request_.backoff;
        break;
    case ProbeSearchDir::NegZ:
        p.z = trigger_pose.z + request_.backoff;
        break;
    }
    return p;
}

float ProbeSearch::maxSearchTravel() const
{
    if (request_.max_search_travel > 0.0f)
    {
        return request_.max_search_travel;
    }
    if (!isXySearch(request_.dir))
    {
        return request_.approach_distance + request_.search_distance + request_.search_depth;
    }
    const float travel = request_.lateral_offset - request_.xy_keepout_radius;
    return travel > 0.0f ? travel : request_.lateral_offset;
}

float ProbeSearch::xyWorkZ() const
{
    return request_.measured_center_z - request_.search_depth;
}

float ProbeSearch::searchTimeoutS(float speed) const
{
    if (request_.search_timeout_s > 0.0f)
    {
        return request_.search_timeout_s;
    }
    const float v = (speed > 0.0f) ? speed : request_.fast_speed;
    return maxSearchTravel() / v * 2.0f + 1.0f;
}

bool ProbeSearch::tryContact(Machine& machine, TestPoint& pose_out)
{
    if (machine.consumeProbeTrigger(pose_out))
    {
        return true;
    }
    const HostInputs& in = machine.hostInputs();
    if (in.probeTriggered && in.probeTriggerPoseValid && std::isfinite(in.probeTriggerX) &&
        std::isfinite(in.probeTriggerY) && std::isfinite(in.probeTriggerZ))
    {
        pose_out = TestPoint{in.probeTriggerX, in.probeTriggerY, in.probeTriggerZ};
        return true;
    }
    return false;
}
