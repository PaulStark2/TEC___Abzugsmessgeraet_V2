#include "ProbeCalibration.hpp"

bool ProbeCalibration::start(Machine& machine)
{
    if (machine.machineState() == MachineState::Fault)
    {
        state_ = State::Error;
        return false;
    }
    state_ = State::Running;
    saw_trigger_ = false;
    result_ = TestPoint{machine.x(), machine.y(), machine.z()};
    const float search = machine.machineConfig().probe.search_distance;
    // PLACEHOLDER: single +X search move. Replace with the historical multi-touch protocol.
    machine.moveTo(machine.x() + search, machine.y(), machine.z());
    return true;
}

void ProbeCalibration::tick(float dt, Machine& machine)
{
    (void)dt;
    if (state_ != State::Running)
    {
        return;
    }
    if (machine.machineState() == MachineState::Fault ||
        machine.machineState() == MachineState::Stopped)
    {
        state_ = State::Error;
        return;
    }
    if (machine.hostInputs().probeTriggered && !saw_trigger_)
    {
        saw_trigger_ = true;
        result_ = TestPoint{machine.x(), machine.y(), machine.z()};
        machine.setProbeCenter(result_);
        state_ = State::Complete;
        return;
    }
    if (machine.motionState() == MotionState::AtTarget ||
        machine.machineState() == MachineState::Idle)
    {
        result_ = TestPoint{machine.x(), machine.y(), machine.z()};
        machine.setProbeCenter(result_);
        state_ = State::Complete;
    }
}

void ProbeCalibration::abort()
{
    if (state_ == State::Running)
    {
        state_ = State::Error;
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
