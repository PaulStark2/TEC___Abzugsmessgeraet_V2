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
          request.approach_speed > 0.0f && request.fast_speed > 0.0f && request.fine_speed > 0.0f))
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
    fine_done_ = 0;
    have_fast_ = false;
    machine.consumeProbeTrigger();
    machine.setCollisionContext(CollisionContext::Approach);
    commandApproachRaise(machine);
    state_ = State::RaiseApproach;
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
    case State::RaiseApproach:
        if (motionSettled(machine))
        {
            commandTraverseStart(machine);
            state_ = State::TraverseStart;
        }
        break;
    case State::TraverseStart:
        if (motionSettled(machine))
        {
            if (request_.dir == ProbeSearchDir::NegZ)
            {
                machine.consumeProbeTrigger();
                commandSearch(machine, request_.fast_speed);
                state_ = State::FastSearch;
            }
            else
            {
                commandDescend(machine);
                state_ = State::Descend;
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
        TestPoint trigger{};
        if (consumeTrigger(machine, trigger))
        {
            if (state_ == State::FastSearch)
            {
                // Fast trigger is used as the "wake-up" contact to enter the
                // backoff/fine-search sequence. If fine searches are enabled,
                // the final result is the mean of fine-search hits (below),
                // not the fast trigger alone.
                have_fast_ = true;
                result_ = trigger;
                if (request_.num_fine_searches == 0)
                {
                    commandRetreat(machine);
                    state_ = State::Retreat;
                }
                else
                {
                    commandBackoff(machine, trigger);
                    state_ = State::Backoff;
                }
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
                    commandRetreat(machine);
                    state_ = State::Retreat;
                }
                else
                {
                    commandBackoff(machine, trigger);
                    state_ = State::Backoff;
                }
            }
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
    case State::Retreat:
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

void ProbeSearch::commandApproachRaise(Machine& machine)
{
    const float approach_z = request_.reference.z + request_.approach_distance;
    machine.setActiveVelocity(request_.approach_speed);
    machine.moveTo(machine.x(), machine.y(), approach_z);
}

void ProbeSearch::commandTraverseStart(Machine& machine)
{
    const TestPoint start = startPose();
    machine.setActiveVelocity(request_.approach_speed);
    machine.moveTo(start.x, start.y, start.z);
}

void ProbeSearch::commandDescend(Machine& machine)
{
    const TestPoint start = startPose();
    const TestPoint target = searchTarget();
    // Prep Z into the search plane is still outside contact risk along the search
    // axis — use approach speed. Slow speeds apply only in FastSearch/FineSearch.
    machine.setActiveVelocity(request_.approach_speed);
    machine.setCollisionContext(CollisionContext::Contact);
    machine.moveTo(start.x, start.y, target.z);
}

void ProbeSearch::commandSearch(Machine& machine, float speed)
{
    const TestPoint target = searchTarget();
    machine.setCollisionContext(CollisionContext::Contact);
    machine.setActiveVelocity(speed);
    machine.moveTo(target.x, target.y, target.z);
}

void ProbeSearch::commandBackoff(Machine& machine, const TestPoint& trigger_pose)
{
    const TestPoint back = backoffPose(trigger_pose);
    machine.setActiveVelocity(request_.approach_speed);
    machine.moveTo(back.x, back.y, back.z);
}

void ProbeSearch::commandRetreat(Machine& machine)
{
    // Clear laterally to this direction's start pose (outer offset @ approach_z)
    // before Complete, so the next direction does not skim past the disc.
    const TestPoint clear = startPose();
    machine.setCollisionContext(CollisionContext::Approach);
    machine.setActiveVelocity(request_.approach_speed);
    machine.moveTo(clear.x, clear.y, clear.z);
}

void ProbeSearch::enterError()
{
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

TestPoint ProbeSearch::startPose() const
{
    TestPoint p = request_.reference;
    p.z = request_.reference.z + request_.approach_distance;
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
