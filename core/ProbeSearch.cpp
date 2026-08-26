#include "ProbeSearch.hpp"

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
          request.lateral_offset > 0.0f && request.xy_inner_radius >= 0.0f && request.backoff >= 0.0f &&
          request.approach_speed > 0.0f && request.transfer_speed > 0.0f &&
          request.fast_speed > 0.0f && request.fine_speed > 0.0f))
    {
        return false;
    }
    if (isXySearch(request.dir) && !(request.lateral_offset > request.xy_inner_radius))
    {
        return false;
    }
    return true;
}

} // namespace

bool ProbeSearch::start(Machine& machine, const Request& request)
{
    if (machine.machineState() == MachineState::Fault || !finiteRequest(request))
    {
        state_ = State::Error;
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
    machine.consumeProbeTrigger();
    machine.setCollisionContext(CollisionContext::Approach);
    // One multi-axis move to hub (Z then X/Y) so corner_probe_transfer can blend.
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
    (void)dt;
    if (state_ == State::Idle || state_ == State::Complete || state_ == State::Error)
    {
        return;
    }
    if (faulted(machine))
    {
        enterError();
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
            machine.consumeProbeTrigger();
            commandSearch(machine, request_.fast_speed);
            state_ = State::FastSearch;
        }
        break;
    case State::FastSearch:
    case State::FineSearch:
    {
        // After trigger: brakeToStop then wait for settle before backoff/retreat.
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
        TestPoint trigger{};
        if (consumeTrigger(machine, trigger))
        {
            onProbeTrigger(machine, trigger);
        }
        else if (motionSettled(machine))
        {
            enterError();
        }
        else if (isXySearch(request_.dir))
        {
            const float dx = machine.x() - request_.reference.x;
            const float dy = machine.y() - request_.reference.y;
            const float r = std::sqrt(dx * dx + dy * dy);
            if (r + 1.0e-3f < request_.xy_inner_radius)
            {
                enterError();
            }
        }
        break;
    }
    case State::Backoff:
        if (motionSettled(machine))
        {
            machine.consumeProbeTrigger();
            commandSearch(machine, request_.fine_speed);
            state_ = State::FineSearch;
        }
        break;
    case State::RetreatClear:
        if (motionSettled(machine))
        {
            if (!isXySearch(request_.dir))
            {
                if (!have_fast_ && fine_done_ == 0)
                {
                    enterError();
                    return;
                }
                state_ = State::Complete;
                return;
            }
            // Raise to hub Z and return XY in one moveTo (blend-capable).
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
                enterError();
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
        enterError();
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
    // Single-axis lateral move at hub Z to this direction's outer start.
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
    const TestPoint target = searchTarget();
    // Prep Z into the search plane is still outside contact risk along the search
    // axis — use approach speed. Slow speeds apply only in FastSearch/FineSearch.
    applyApproachProfile(machine);
    machine.setCollisionContext(CollisionContext::Contact);
    machine.moveTo(machine.x(), machine.y(), target.z);
}

void ProbeSearch::commandSearch(Machine& machine, float speed)
{
    const TestPoint target = searchTarget();
    machine.setCollisionContext(CollisionContext::Contact);
    applySearchProfile(machine, speed);
    switch (request_.dir)
    {
    case ProbeSearchDir::NegX:
    case ProbeSearchDir::PosX:
        machine.moveTo(target.x, machine.y(), machine.z());
        break;
    case ProbeSearchDir::NegY:
    case ProbeSearchDir::PosY:
        machine.moveTo(machine.x(), target.y, machine.z());
        break;
    case ProbeSearchDir::NegZ:
        machine.moveTo(machine.x(), machine.y(), target.z);
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
    // Along the search axis back to the direction start at current (work) Z.
    // For NegZ this is hub Z (P_z height) at hub XY.
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

void ProbeSearch::advanceAfterHubAligned(Machine& machine)
{
    if (!isXySearch(request_.dir))
    {
        machine.consumeProbeTrigger();
        commandSearch(machine, request_.fast_speed);
        state_ = State::FastSearch;
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
    // No corner blend during contact search (single-axis legs).
    const MachineConfig::Probe& probe = machine.machineConfig().probe;
    machine.setActiveMotionProfile(speed, probe.a_probe_search, 0.0f);
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

void ProbeSearch::enterError()
{
    braking_ = false;
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
    TestPoint p = request_.reference;
    p.z = request_.reference.z + request_.approach_distance;
    return p;
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

TestPoint ProbeSearch::searchTarget() const
{
    TestPoint p = request_.reference;
    p.z = request_.reference.z - request_.search_depth;
    const float inner = request_.xy_inner_radius;
    switch (request_.dir)
    {
    case ProbeSearchDir::NegX:
        p.x = request_.reference.x - inner;
        p.y = request_.reference.y;
        break;
    case ProbeSearchDir::PosX:
        p.x = request_.reference.x + inner;
        p.y = request_.reference.y;
        break;
    case ProbeSearchDir::NegY:
        p.x = request_.reference.x;
        p.y = request_.reference.y - inner;
        break;
    case ProbeSearchDir::PosY:
        p.x = request_.reference.x;
        p.y = request_.reference.y + inner;
        break;
    case ProbeSearchDir::NegZ:
        p.x = request_.reference.x;
        p.y = request_.reference.y;
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

bool ProbeSearch::consumeTrigger(Machine& machine, TestPoint& pose_out)
{
    return machine.consumeProbeTrigger(pose_out);
}
