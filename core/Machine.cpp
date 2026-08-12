#include "Machine.hpp"

#include <cmath>

// Construct machine in Idle with deterministic motion parameters. // ctor
Machine::Machine()
    : motion_planner_() // create planner
    , machine_state_(MachineState::Idle) // initial state
    , default_max_velocity_(10.0f) // defaults for deterministic test motion
    , default_acceleration_(0.0f) // constant velocity mode
    , stored_target_x_(0.0f) // init stored pose
    , stored_target_y_(0.0f) // init stored pose
    , stored_target_z_(0.0f) // init stored pose
    , commanded_target_x_(0.0f) // init commanded baseline
    , commanded_target_y_(0.0f) // init commanded baseline
    , host_inputs_{} // init POD inputs
    , host_outputs_{} // init POD outputs
    , force_params_{} // default M10 parameters
    , force_phase_(ForceControlPhase::NormalZApproach) // no XY control until slow phase
    , x_force_correction_active_(false)
    , y_force_correction_active_(false)
{
    // Set default motion parameters so moveTo() actually moves. // default motion params
    motion_planner_.setMotionParams(default_max_velocity_, default_acceleration_); // apply deterministic defaults
}

Machine::Machine(const ForceControlParams& params)
    : motion_planner_() // create planner
    , machine_state_(MachineState::Idle) // initial state
    , default_max_velocity_(10.0f) // defaults for deterministic test motion
    , default_acceleration_(0.0f) // constant velocity mode
    , stored_target_x_(0.0f) // init stored pose
    , stored_target_y_(0.0f) // init stored pose
    , stored_target_z_(0.0f) // init stored pose
    , commanded_target_x_(0.0f) // init commanded baseline
    , commanded_target_y_(0.0f) // init commanded baseline
    , host_inputs_{} // init POD inputs
    , host_outputs_{} // init POD outputs
    , force_params_(params) // deterministic M10 params
    , force_phase_(ForceControlPhase::NormalZApproach)
    , x_force_correction_active_(false)
    , y_force_correction_active_(false)
{
    // Set default motion parameters so moveTo() actually moves. // default motion params
    motion_planner_.setMotionParams(default_max_velocity_, default_acceleration_); // apply deterministic defaults
}

MachineState Machine::machineState() const
{
    return machine_state_; // return state
}

MotionState Machine::motionState() const
{
    return motion_planner_.motionState(); // expose planner state only
}

void Machine::setHostInputs(const HostInputs& inputs)
{
    host_inputs_ = inputs; // store snapshot only
}

const HostInputs& Machine::hostInputs() const
{
    return host_inputs_; // return stored snapshot only
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
    commanded_target_x_ = x; // baseline for max XY correction clamp
    commanded_target_y_ = y; // baseline for max XY correction clamp

    // Reset M10 internal phases/hysteresis for the new move. // M10 reset
    force_phase_ = ForceControlPhase::NormalZApproach;
    x_force_correction_active_ = false;
    y_force_correction_active_ = false;

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

    // Only Active state advances the MotionPlanner and evaluates M10. // active only
    if (machine_state_ != MachineState::Active)
    {
        return; // no motion while paused/stopped/idle/fault
    }

    // 2. Fz-Safety first: abs(Fz) >= threshold => Fault + freeze + no completion. // M10 safety
    const float fz = host_inputs_.force.fz;
    if (std::fabs(fz) >= force_params_.fz_safety_threshold_N)
    {
        motion_planner_.stopMotion(); // freeze pose deterministically
        machine_state_ = MachineState::Fault; // safety fault
        return; // strict priority: no XY correction and no planner tick in this tick
    }

    // 3. Detect SlowZApproach for XY control. // M10 slow phase detection
    const float z_remaining = stored_target_z_ - motion_planner_.z();
    const bool slow_z = std::fabs(z_remaining) <= force_params_.slow_z_distance_threshold_units;
    force_phase_ = slow_z ? ForceControlPhase::SlowZApproach : ForceControlPhase::NormalZApproach;

    // Reset hysteresis states outside slow phase. // M10 ignore Fx/Fy in Normal phase
    if (force_phase_ != ForceControlPhase::SlowZApproach)
    {
        x_force_correction_active_ = false;
        y_force_correction_active_ = false;
    }

    // 4. Fx-Korrektur (continuous, independent, hysteresis) before MotionPlanner tick. // M10 XY X
    if (force_phase_ == ForceControlPhase::SlowZApproach)
    {
        const float fx = host_inputs_.force.fx;
        const float absFx = std::fabs(fx);

        if (!x_force_correction_active_ && absFx >= force_params_.fx_threshold_on_N)
        {
            x_force_correction_active_ = true;
        }
        if (x_force_correction_active_ && absFx <= force_params_.fx_threshold_off_N)
        {
            x_force_correction_active_ = false;
        }

        if (x_force_correction_active_)
        {
            // Force in +X => target steps in -X direction. // sign direction
            if (fx > 0.0f)
            {
                stored_target_x_ -= force_params_.dx_step_units;
            }
            else if (fx < 0.0f)
            {
                stored_target_x_ += force_params_.dx_step_units;
            }

            // Clamp corrected X target independently around commanded moveTo() target.
            const float min_x = commanded_target_x_ - force_params_.max_x_correction_units;
            const float max_x = commanded_target_x_ + force_params_.max_x_correction_units;
            if (stored_target_x_ < min_x)
            {
                stored_target_x_ = min_x;
            }
            if (stored_target_x_ > max_x)
            {
                stored_target_x_ = max_x;
            }
        }
    }

    // 5. Fy-Korrektur (continuous, independent, hysteresis) before MotionPlanner tick. // M10 XY Y
    if (force_phase_ == ForceControlPhase::SlowZApproach)
    {
        const float fy = host_inputs_.force.fy;
        const float absFy = std::fabs(fy);

        if (!y_force_correction_active_ && absFy >= force_params_.fy_threshold_on_N)
        {
            y_force_correction_active_ = true;
        }
        if (y_force_correction_active_ && absFy <= force_params_.fy_threshold_off_N)
        {
            y_force_correction_active_ = false;
        }

        if (y_force_correction_active_)
        {
            // Force in +Y => target steps in -Y direction. // sign direction
            if (fy > 0.0f)
            {
                stored_target_y_ -= force_params_.dy_step_units;
            }
            else if (fy < 0.0f)
            {
                stored_target_y_ += force_params_.dy_step_units;
            }

            // Clamp corrected Y target independently around commanded moveTo() target.
            const float min_y = commanded_target_y_ - force_params_.max_y_correction_units;
            const float max_y = commanded_target_y_ + force_params_.max_y_correction_units;
            if (stored_target_y_ < min_y)
            {
                stored_target_y_ = min_y;
            }
            if (stored_target_y_ > max_y)
            {
                stored_target_y_ = max_y;
            }
        }
    }

    // Update only X/Y targets; must not touch Z targets. // M10 setXYTargets
    motion_planner_.setXYTargets(stored_target_x_, stored_target_y_);

    // 6. Normal MotionPlanner tick last. // delegate tick
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

