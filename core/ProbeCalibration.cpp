#include "ProbeCalibration.hpp"

#include "ProbeGeometry.hpp"

#include <cmath>
#include <cstdio>

bool ProbeCalibration::start(Machine& machine, const TestPoint& hub, StorePolicy policy)
{
    if (machine.machineState() == MachineState::Fault)
    {
        enterError();
        return false;
    }
    if (policy == StorePolicy::OffsetOnly && !machine.calibrationSet().probe_center_valid)
    {
        enterError();
        return false;
    }

    policy_ = policy;
    hub_ = hub;
    result_ = TestPoint{};
    tool_offset_ = TestPoint{};

    const MachineConfig::Probe& pc = machine.machineConfig().probe;
    BoreSearch::Params params{};
    params.wall_force_N = pc.wall_force_N;
    params.floor_force_N = pc.floor_force_N;
    params.xy_search_limit = pc.xy_search_limit;
    params.z_search_limit = pc.z_search_limit;
    params.z_lift_mm = pc.z_lift_mm;
    params.timeout_s = pc.timeout_s;
    params.refine_passes = pc.refine_passes;
    params.do_floor = false;
    params.z_first = true;

    machine.setCollisionContext(CollisionContext::Contact);
    machine.setActiveMotionProfile(pc.v_search, machine.machineConfig().motion.a_testpoints,
                                   machine.machineConfig().motion.corner_testpoints);
    if (!search_.start(machine, params))
    {
        enterError();
        return false;
    }
    state_ = State::Running;
    return true;
}

void ProbeCalibration::tick(float dt, Machine& machine)
{
    if (state_ != State::Running)
    {
        return;
    }
    if (machine.machineState() == MachineState::Fault ||
        machine.machineState() == MachineState::Stopped)
    {
        search_.abort();
        enterError();
        return;
    }

    search_.tick(dt, machine);
    if (search_.isError())
    {
        enterError();
        return;
    }
    if (!search_.isComplete())
    {
        return;
    }
    if (!finishAndStore(machine))
    {
        return;
    }
}

void ProbeCalibration::abort(Machine& machine)
{
    (void)machine;
    if (state_ == State::Running)
    {
        search_.abort();
        enterError();
    }
}

bool ProbeCalibration::isComplete() const
{
    return state_ == State::Complete;
}

bool ProbeCalibration::isError() const
{
    return state_ == State::Error;
}

ProbeCalibration::State ProbeCalibration::state() const
{
    return state_;
}

TestPoint ProbeCalibration::result() const
{
    return result_;
}

TestPoint ProbeCalibration::toolOffset() const
{
    return tool_offset_;
}

ProbeCalibration::StorePolicy ProbeCalibration::storePolicy() const
{
    return policy_;
}

BoreSearch::ErrorReason ProbeCalibration::searchErrorReason() const
{
    return search_.errorReason();
}

bool ProbeCalibration::finishAndStore(Machine& machine)
{
    const BoreSearch::Result& bore = search_.result();
    result_.x = bore.cx;
    result_.y = bore.cy;
    result_.z = bore.cz;
    if (!std::isfinite(result_.x) || !std::isfinite(result_.y) || !std::isfinite(result_.z))
    {
        enterError();
        return false;
    }

    if (policy_ == StorePolicy::Install)
    {
        tool_offset_ = TestPoint{0.0f, 0.0f, 0.0f};
        machine.setProbeCenter(result_);
        machine.setToolOffset(tool_offset_);
    }
    else
    {
        const CalibrationSet& calib = machine.calibrationSet();
        if (!calib.probe_center_valid)
        {
            enterError();
            return false;
        }
        if (!computeToolOffset(calib.probe_center, result_, tool_offset_))
        {
            enterError();
            return false;
        }
        machine.setToolOffset(tool_offset_);
    }

    std::fprintf(stderr,
                 "[ProbeCalib] bore OK policy=%u center=(%.3f,%.3f,%.3f) r=%.3f hub=(%.3f,%.3f,%.3f)\n",
                 static_cast<unsigned>(policy_), static_cast<double>(result_.x),
                 static_cast<double>(result_.y), static_cast<double>(result_.z),
                 static_cast<double>(bore.radius), static_cast<double>(hub_.x),
                 static_cast<double>(hub_.y), static_cast<double>(hub_.z));
    state_ = State::Complete;
    return true;
}

void ProbeCalibration::enterError()
{
    state_ = State::Error;
}
