#include "ProbeCalibration.hpp"

#include "ProbeGeometry.hpp"

#include <cmath>

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
    measured_center_z_ = 0.0f;
    have_measured_center_z_ = false;
    result_ = TestPoint{};
    tool_offset_ = TestPoint{};
    contact_z_ = TestPoint{};
    contact_neg_y_ = TestPoint{};
    contact_pos_y_ = TestPoint{};
    contact_neg_x_ = TestPoint{};
    contact_pos_x_ = TestPoint{};
    machine.consumeProbeTrigger();
    if (!startDirection(machine, ProbeSearchDir::NegZ, Step::MeasureZ))
    {
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

    switch (step_)
    {
    case Step::MeasureZ:
        contact_z_ = search_.result();
        {
            TestPoint z_center{};
            if (!contactToProbeCenter(contact_z_, ProbeSearchDir::NegZ, machine.machineConfig().probe.measuring_head_diameter,
                                      machine.machineConfig().probe.measuring_head_thickness,
                                      machine.machineConfig().probe.tool_diameter, z_center))
            {
                enterError();
                return;
            }
            measured_center_z_ = z_center.z;
            have_measured_center_z_ = true;
            float work_z = 0.0f;
            if (!xySearchWorkZ(measured_center_z_, machine.machineConfig().probe.search_depth,
                               machine.machineConfig().probe.measuring_head_thickness,
                               machine.machineConfig().probe.tool_cylindrical_height, work_z))
            {
                enterError();
                return;
            }
            (void)work_z;
        }
        if (!startDirection(machine, ProbeSearchDir::NegY, Step::MeasureNegY))
        {
            return;
        }
        break;
    case Step::MeasureNegY:
        contact_neg_y_ = search_.result();
        if (!startDirection(machine, ProbeSearchDir::PosY, Step::MeasurePosY))
        {
            return;
        }
        break;
    case Step::MeasurePosY:
        contact_pos_y_ = search_.result();
        if (!startDirection(machine, ProbeSearchDir::NegX, Step::MeasureNegX))
        {
            return;
        }
        break;
    case Step::MeasureNegX:
        contact_neg_x_ = search_.result();
        if (!startDirection(machine, ProbeSearchDir::PosX, Step::MeasurePosX))
        {
            return;
        }
        break;
    case Step::MeasurePosX:
        contact_pos_x_ = search_.result();
        step_ = Step::FitCenter;
        if (!finishAndStore(machine))
        {
            return;
        }
        break;
    default:
        enterError();
        break;
    }
}

void ProbeCalibration::abort()
{
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

ProbeCalibration::Step ProbeCalibration::step() const
{
    return step_;
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

ProbeSearch::ErrorReason ProbeCalibration::searchErrorReason() const
{
    return search_.errorReason();
}

ProbeSearch::Request ProbeCalibration::makeRequest(const Machine& machine, ProbeSearchDir dir) const
{
    const MachineConfig::Probe& probe = machine.machineConfig().probe;
    ProbeSearch::Request request{};
    request.reference = hub_;
    request.dir = dir;
    request.approach_distance = probe.approach_distance;
    request.search_distance = probe.search_distance;
    request.search_depth = probe.search_depth;
    request.measured_center_z = measured_center_z_;
    request.have_measured_center_z = have_measured_center_z_;
    request.lateral_offset =
        xyStartOffset(probe.search_distance, probe.measuring_head_diameter, probe.tool_diameter);
    request.xy_keepout_radius = 0.0f;
    request.backoff = probe.backoff;
    request.approach_speed = probe.v_approach_probe;
    request.transfer_speed = probe.v_transfer_probe;
    request.fast_speed = probe.v_search;
    request.fine_speed = probe.v_search_fine;
    request.num_fine_searches = probe.num_fine_searches;
    return request;
}

bool ProbeCalibration::startDirection(Machine& machine, ProbeSearchDir dir, Step next_step)
{
    if (!search_.start(machine, makeRequest(machine, dir)))
    {
        enterError();
        return false;
    }
    step_ = next_step;
    return true;
}

bool ProbeCalibration::finishAndStore(Machine& machine)
{
    const MachineConfig::Probe& probe = machine.machineConfig().probe;
    const float expected_r = expectedXyContactRadius(probe.measuring_head_diameter, probe.tool_diameter);
    const float radius_tol = (expected_r * 0.25f > 1.0f) ? expected_r * 0.25f : 1.0f;
    TestPoint xy_center{};
    float radius = 0.0f;
    if (!fitCircleCenterXY(contact_neg_x_, contact_pos_x_, contact_neg_y_, contact_pos_y_, expected_r,
                           radius_tol, xy_center, radius))
    {
        enterError();
        return false;
    }

    TestPoint z_center{};
    if (!contactToProbeCenter(contact_z_, ProbeSearchDir::NegZ, probe.measuring_head_diameter,
                              probe.measuring_head_thickness, probe.tool_diameter, z_center))
    {
        enterError();
        return false;
    }

    result_.x = xy_center.x;
    result_.y = xy_center.y;
    result_.z = z_center.z;

    if (policy_ == StorePolicy::Install)
    {
        // FullCalib / Geometry: durable plant probe reference; no tool offset yet.
        tool_offset_ = TestPoint{0.0f, 0.0f, 0.0f};
        machine.setProbeCenter(result_);
        machine.setToolOffset(tool_offset_);
    }
    else
    {
        // Procedure OffsetOnly: keep installed probeCenter; update tool offset only.
        // toolOffset = storedProbeCenter - measured (not probe_offset_reference).
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

    step_ = Step::Complete;
    state_ = State::Complete;
    return true;
}

void ProbeCalibration::enterError()
{
    step_ = Step::Error;
    state_ = State::Error;
}
