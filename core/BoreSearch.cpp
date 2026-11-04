#include "BoreSearch.hpp"

#include <cmath>

namespace
{

bool needsFloor(const BoreSearch::Params& p)
{
    return p.do_floor || p.z_first;
}

bool paramsOk(const BoreSearch::Params& p)
{
    if (!(p.wall_force_N > 0.0f) || !(p.xy_search_limit > 0.0f) || !(p.timeout_s > 0.0f))
    {
        return false;
    }
    if (needsFloor(p))
    {
        if (!(p.floor_force_N > 0.0f) || !(p.z_search_limit > 0.0f) || !(p.z_lift_mm >= 0.0f))
        {
            return false;
        }
    }
    return std::isfinite(p.wall_force_N) && std::isfinite(p.xy_search_limit) &&
           std::isfinite(p.timeout_s);
}

} // namespace

bool BoreSearch::start(Machine& machine, const Params& params)
{
    if (machine.machineState() == MachineState::Fault || !paramsOk(params))
    {
        enterError(ErrorReason::InvalidRequest);
        return false;
    }

    params_ = params;
    result_ = Result{};
    error_ = ErrorReason::None;
    refine_left_ = params_.refine_passes;
    elapsed_s_ = 0.0f;
    origin_z_ = machine.z();
    floor_z_ = machine.z();
    if (params_.z_first)
    {
        state_ = State::FloorDown;
        motion_ = Motion::Command;
        edge_armed_ = false;
        return true;
    }
    resetXySearch(machine);
    state_ = State::FirstOut;
    motion_ = Motion::Command;
    return true;
}

void BoreSearch::abort()
{
    if (state_ == State::Idle || state_ == State::Complete || state_ == State::Error)
    {
        return;
    }
    enterError(ErrorReason::Aborted);
}

bool BoreSearch::isComplete() const
{
    return state_ == State::Complete;
}

bool BoreSearch::isError() const
{
    return state_ == State::Error;
}

bool BoreSearch::isBusy() const
{
    return state_ != State::Idle && state_ != State::Complete && state_ != State::Error;
}

BoreSearch::State BoreSearch::state() const
{
    return state_;
}

BoreSearch::ErrorReason BoreSearch::errorReason() const
{
    return error_;
}

const BoreSearch::Result& BoreSearch::result() const
{
    return result_;
}

const BoreSearch::Params& BoreSearch::params() const
{
    return params_;
}

void BoreSearch::enterError(ErrorReason reason)
{
    state_ = State::Error;
    error_ = reason;
    motion_ = Motion::Command;
}

bool BoreSearch::motionSettled(const Machine& machine)
{
    return machine.motionState() == MotionState::AtTarget ||
           machine.machineState() == MachineState::Idle;
}

int BoreSearch::seekSign(float component, float tolerance) const
{
    if (std::fabs(component) >= tolerance)
    {
        return component > 0.0f ? -1 : 1;
    }
    return 1;
}

void BoreSearch::resetXySearch(Machine& machine)
{
    // Keeps elapsed_s_ and refine_left_: one timeout budget for the whole search.
    state_ = State::FirstOut;
    motion_ = Motion::Command;
    axis_x_ = true;
    axis_chosen_ = false;
    second_chosen_ = false;
    seek_sign_ = 1;
    edge_armed_ = false;
    origin_x_ = machine.x();
    origin_y_ = machine.y();
    point_count_ = 0;
}

void BoreSearch::commandTravel(Machine& machine)
{
    const float fx = machine.hostInputs().force.fx;
    const float fy = machine.hostInputs().force.fy;
    const float tol = params_.wall_force_N;

    if (state_ == State::FirstOut && !axis_chosen_)
    {
        const bool y_wins = std::fabs(fy) > std::fabs(fx) && std::fabs(fy) >= tol;
        axis_x_ = !y_wins;
        axis_chosen_ = true;
    }
    if (state_ == State::SecondOut && !second_chosen_)
    {
        axis_x_ = !axis_x_;
        second_chosen_ = true;
    }

    const float component = axis_x_ ? fx : fy;
    int sign = seek_sign_;
    if (state_ == State::FirstOut || state_ == State::SecondOut)
    {
        // Out legs: unload from contact force, or default + when free.
        sign = seekSign(component, tol);
    }
    else
    {
        // Back legs: always reverse the Out seek. Do not re-run seekSign —
        // after Stop the filtered force often dips below tol and would default
        // to +1 again, re-capturing the same wall (then a degenerate chord).
        sign = -seek_sign_;
    }
    // Motion sign and the force sign of the wall being sought are the same.
    seek_sign_ = sign;
    const float sought = static_cast<float>(sign) * component;
    edge_armed_ = sought < tol;
    const float limit = params_.xy_search_limit;
    const float goal =
        (axis_x_ ? origin_x_ : origin_y_) + static_cast<float>(sign) * limit;
    if (axis_x_)
    {
        machine.moveTo(goal, machine.y(), machine.z());
    }
    else
    {
        machine.moveTo(machine.x(), goal, machine.z());
    }
    motion_ = Motion::Move;
}

void BoreSearch::finishWall(Machine& machine)
{
    if (point_count_ < 4)
    {
        points_[point_count_] = BorePoint{machine.x(), machine.y()};
        ++point_count_;
    }
    motion_ = Motion::Command;
    if (state_ == State::FirstOut)
    {
        state_ = State::FirstBack;
    }
    else if (state_ == State::FirstBack)
    {
        state_ = State::ToMid;
    }
    else if (state_ == State::SecondOut)
    {
        state_ = State::SecondBack;
    }
    else
    {
        state_ = State::ToCenter;
    }
}

void BoreSearch::finishCenter(Machine& machine)
{
    const float fx = machine.hostInputs().force.fx;
    const float fy = machine.hostInputs().force.fy;
    const float fz = machine.hostInputs().force.fz;
    const float tol = params_.wall_force_N;
    const float cx = 0.5f * (points_[2].x + points_[3].x);
    const float cy = 0.5f * (points_[2].y + points_[3].y);
    const float radius =
        0.5f * std::hypot(points_[2].x - points_[3].x, points_[2].y - points_[3].y);
    const bool anomaly = std::fabs(fx) > tol || std::fabs(fy) > tol;

    if (!anomaly && refine_left_ > 0)
    {
        --refine_left_;
        resetXySearch(machine);
        return;
    }

    result_.cx = cx;
    result_.cy = cy;
    // z_first: keep floor contact Z already stored after FloorLift.
    if (!params_.z_first)
    {
        result_.cz = machine.z();
    }
    result_.radius = radius;
    result_.anomaly = anomaly;
    result_.fx = fx;
    result_.fy = fy;
    result_.fz = fz;
    for (int i = 0; i < 4; ++i)
    {
        result_.points[i] = points_[i];
    }

    // z_first already did floor before XY — never run floor again.
    if (params_.do_floor && !params_.z_first)
    {
        origin_z_ = machine.z();
        state_ = State::FloorDown;
        motion_ = Motion::Command;
        edge_armed_ = false;
        return;
    }

    state_ = State::Complete;
    motion_ = Motion::Command;
}

void BoreSearch::tick(float dt, Machine& machine)
{
    if (state_ == State::Idle || state_ == State::Complete || state_ == State::Error)
    {
        return;
    }

    if (machine.machineState() == MachineState::Fault)
    {
        enterError(ErrorReason::Fault);
        return;
    }

    elapsed_s_ += dt;
    if (elapsed_s_ >= params_.timeout_s)
    {
        enterError(ErrorReason::Timeout);
        return;
    }

    if (state_ == State::FloorDown || state_ == State::FloorLift)
    {
        if (state_ == State::FloorDown)
        {
            if (motion_ == Motion::Command)
            {
                edge_armed_ = machine.hostInputs().force.fz > -params_.floor_force_N;
                machine.moveTo(machine.x(), machine.y(), origin_z_ - params_.z_search_limit);
                motion_ = Motion::Move;
                return;
            }
            if (motion_ == Motion::Stop)
            {
                if (motionSettled(machine))
                {
                    floor_z_ = machine.z();
                    state_ = State::FloorLift;
                    motion_ = Motion::Command;
                }
                return;
            }
            const float fz = machine.hostInputs().force.fz;
            // Contact from above: rising |Fz|. Floor uses magnitude against threshold.
            const float mag = std::fabs(fz);
            if (!edge_armed_)
            {
                if (mag < params_.floor_force_N)
                {
                    edge_armed_ = true;
                }
            }
            else if (mag >= params_.floor_force_N)
            {
                machine.moveTo(machine.x(), machine.y(), machine.z());
                motion_ = Motion::Stop;
                return;
            }
            // Abort when travel ends without contact (soft-clamped goal or full limit).
            // Do not require |z−origin|≥limit — that hangs forever if Z cannot reach the
            // commanded depth (workspace / soft limit).
            if (motionSettled(machine))
            {
                enterError(ErrorReason::FloorNotFound);
            }
            return;
        }

        // FloorLift
        if (motion_ == Motion::Command)
        {
            const float lift_z = floor_z_ + params_.z_lift_mm;
            machine.moveTo(machine.x(), machine.y(), lift_z);
            motion_ = Motion::Move;
            return;
        }
        if (!motionSettled(machine))
        {
            return;
        }
        result_.fz = machine.hostInputs().force.fz;
        if (params_.z_first)
        {
            // Durable probe_center.z = floor contact; XY runs at lifted work height.
            result_.cz = floor_z_;
            resetXySearch(machine);
            return;
        }
        // Plate teach: keep prior semantics (Z after lift).
        result_.cz = machine.z();
        state_ = State::Complete;
        motion_ = Motion::Command;
        return;
    }

    if (state_ == State::ToMid || state_ == State::ToCenter)
    {
        if (motion_ == Motion::Command)
        {
            if (state_ == State::ToMid)
            {
                const float mid_x = 0.5f * (points_[0].x + points_[1].x);
                const float mid_y = 0.5f * (points_[0].y + points_[1].y);
                machine.moveTo(mid_x, mid_y, machine.z());
            }
            else
            {
                const float mid_x = 0.5f * (points_[2].x + points_[3].x);
                const float mid_y = 0.5f * (points_[2].y + points_[3].y);
                machine.moveTo(mid_x, mid_y, machine.z());
            }
            motion_ = Motion::Move;
            return;
        }
        if (!motionSettled(machine))
        {
            return;
        }
        if (state_ == State::ToMid)
        {
            state_ = State::SecondOut;
            motion_ = Motion::Command;
            return;
        }
        finishCenter(machine);
        return;
    }

    if (motion_ == Motion::Command)
    {
        commandTravel(machine);
        return;
    }

    if (motion_ == Motion::Stop)
    {
        if (motionSettled(machine))
        {
            finishWall(machine);
        }
        return;
    }

    const float fx = machine.hostInputs().force.fx;
    const float fy = machine.hostInputs().force.fy;
    const float component = axis_x_ ? fx : fy;
    const float along = axis_x_ ? machine.x() : machine.y();
    const float origin = axis_x_ ? origin_x_ : origin_y_;
    const float tol = params_.wall_force_N;
    const float sought = static_cast<float>(seek_sign_) * component;

    if (!edge_armed_)
    {
        if (sought < tol)
        {
            edge_armed_ = true;
        }
    }
    else if (sought >= tol)
    {
        machine.moveTo(machine.x(), machine.y(), machine.z());
        motion_ = Motion::Stop;
        return;
    }

    // Only abort when settled at this leg's seek goal — not merely |along−origin|≥limit.
    // After FirstOut capture the tool often sits near +limit; FirstBack would otherwise
    // trip WallNotFound immediately before reverse travel starts.
    const float goal_along = origin + static_cast<float>(seek_sign_) * params_.xy_search_limit;
    if (std::fabs(along - goal_along) <= 1.0e-3f && motionSettled(machine))
    {
        enterError(ErrorReason::WallNotFound);
    }
}
