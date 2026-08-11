#include "Machine.hpp"

// Construct machine in Idle with deterministic motion parameters. // ctor
Machine::Machine()
    : motion_planner_() // create planner
    , machine_state_(MachineState::Idle) // initial state
    , default_max_velocity_(10.0f) // defaults for deterministic test motion
    , default_acceleration_(0.0f) // constant velocity mode
    , stored_target_x_(0.0f) // init stored pose
    , stored_target_y_(0.0f) // init stored pose
    , stored_target_z_(0.0f) // init stored pose
    , host_inputs_{} // init POD inputs
    , host_outputs_{} // init POD outputs
{
    // Set default motion parameters so moveTo() actually moves. // default motion params
    motion_planner_.setMotionParams(default_max_velocity_, default_acceleration_); // apply deterministic defaults
}

MachineState Machine::machineState() const
{
    return machine_state_; // return state
}

void Machine::setHostInputs(const HostInputs& inputs)
{
    host_inputs_ = inputs; // store snapshot only
}

HostOutputs Machine::hostOutputs() const
{
    return host_outputs_; // return placeholders only
}

void Machine::moveTo(float x, float y, float z)
{
    // Store target for possible resume after pause. // store
    stored_target_x_ = x; // store x
    stored_target_y_ = y; // store y
    stored_target_z_ = z; // store z

    // MotionPlanner::stopMotion() resets Axis velocity to 0, so re-apply defaults. // restore params
    motion_planner_.setMotionParams(default_max_velocity_, default_acceleration_); // reset deterministic velocity

    // Delegate pose targets to the MotionPlanner only. // delegate
    motion_planner_.moveTo(x, y, z); // set planner targets

    // If already at target, keep machine Idle, otherwise Active. // set state
    machine_state_ = (motion_planner_.motionState() == MotionState::AtTarget)
                           ? MachineState::Idle
                           : MachineState::Active;
}

void Machine::tick(float dt)
{
    // dt <= 0 must not change positions. // guard
    if (dt <= 0.0f)
    {
        return; // no progress
    }

    // Only Active state advances the MotionPlanner. // active only
    if (machine_state_ != MachineState::Active)
    {
        return; // no motion while paused/stopped/idle/fault
    }

    // Advance planner by a single non-blocking tick. // delegate tick
    motion_planner_.tick(dt); // tick planner

    // When motion completes, return to Idle (no TestExecutor in M5). // done
    if (motion_planner_.motionState() == MotionState::AtTarget)
    {
        machine_state_ = MachineState::Idle; // update machine state
    }
}

void Machine::pause()
{
    // Only Active -> Paused is a valid transition. // rule
    if (machine_state_ != MachineState::Active)
    {
        return; // no-op
    }

    // Freeze planner and switch to Paused. // pause behavior
    motion_planner_.stopMotion(); // freeze movement
    machine_state_ = MachineState::Paused; // update machine state
}

void Machine::resume()
{
    // Only Paused -> Active is a valid transition. // rule
    if (machine_state_ != MachineState::Paused)
    {
        return; // no-op
    }

    // Restart motion toward stored target and switch to Active or Idle. // resume behavior
    motion_planner_.setMotionParams(default_max_velocity_, default_acceleration_); // restore velocity after stopMotion
    motion_planner_.moveTo(stored_target_x_, stored_target_y_, stored_target_z_); // restart planner
    machine_state_ = (motion_planner_.motionState() == MotionState::AtTarget)
                           ? MachineState::Idle
                           : MachineState::Active; // update machine state
}

void Machine::stop()
{
    // Stop works from any non-terminal state in M5. // policy
    if (machine_state_ == MachineState::Stopped)
    {
        return; // already stopped
    }
    if (machine_state_ == MachineState::Fault)
    {
        return; // keep fault placeholder unchanged in M5
    }

    // Freeze planner and switch to Stopped (resume must not reactivate). // stop behavior
    motion_planner_.stopMotion(); // freeze movement
    machine_state_ = MachineState::Stopped; // set stopped
}

float Machine::x() const
{
    return motion_planner_.x(); // delegate
}

float Machine::y() const
{
    return motion_planner_.y(); // delegate
}

float Machine::z() const
{
    return motion_planner_.z(); // delegate
}

