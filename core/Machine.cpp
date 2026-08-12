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
    return host_outputs_; // return outputs snapshot
}

void Machine::setSpreaderExtend(bool extend)
{
    // Pneumatic commands are no-op while Fault is latched. // M11 fault guard
    if (machine_state_ == MachineState::Fault)
    {
        return; // no-op
    }

    host_outputs_.spreaderExtend = extend; // command Spreizer
    updatePneumaticStub(); // deterministic feedback
}

void Machine::setFixtureExtend(bool extend)
{
    // Pneumatic commands are no-op while Fault is latched. // M11 fault guard
    if (machine_state_ == MachineState::Fault)
    {
        return; // no-op
    }

    host_outputs_.fixtureExtend = extend; // command Bauplattenhalter
    updatePneumaticStub(); // deterministic feedback
}

void Machine::updatePneumaticStub()
{
    // Deterministic stub: Extended mirrors Extend immediately. // M11 stub
    host_outputs_.spreaderExtended = host_outputs_.spreaderExtend;
    host_outputs_.fixtureExtended = host_outputs_.fixtureExtend;
}

bool Machine::isCommandedIntoActiveLimit() const
{
    const float pos_x = motion_planner_.x(); // current X
    const float pos_y = motion_planner_.y(); // current Y
    const float pos_z = motion_planner_.z(); // current Z

    // Directional limit: fault only when commanding motion toward an active limit. // M11 limits
    if (host_inputs_.limitXMin && stored_target_x_ < pos_x - kPositionTolerance)
    {
        return true; // commanding further -X into min limit
    }
    if (host_inputs_.limitXMax && stored_target_x_ > pos_x + kPositionTolerance)
    {
        return true; // commanding further +X into max limit
    }
    if (host_inputs_.limitYMin && stored_target_y_ < pos_y - kPositionTolerance)
    {
        return true; // commanding further -Y into min limit
    }
    if (host_inputs_.limitYMax && stored_target_y_ > pos_y + kPositionTolerance)
    {
        return true; // commanding further +Y into max limit
    }
    if (host_inputs_.limitZMin && stored_target_z_ < pos_z - kPositionTolerance)
    {
        return true; // commanding further -Z into min limit
    }
    if (host_inputs_.limitZMax && stored_target_z_ > pos_z + kPositionTolerance)
    {
        return true; // commanding further +Z into max limit
    }

    return false; // no limit violation
}

void Machine::enterSafetyFault()
{
    // Safety-Fault stops motion only; pneumatic outputs are never modified here. // M11 safety
    motion_planner_.stopMotion(); // freeze pose deterministically
    machine_state_ = MachineState::Fault; // latched safety fault
}

void Machine::clearFault()
{
    // Fault clears only when latched and e_stop is released. // M11 clear
    if (machine_state_ != MachineState::Fault)
    {
        return; // no-op
    }
    if (host_inputs_.e_stop)
    {
        return; // e_stop still active
    }

    machine_state_ = MachineState::Idle; // clear latched fault; pneumatics unchanged
}

void Machine::moveTo(float x, float y, float z)
{
    // moveTo is no-op while Fault is latched. // M11 fault guard
    if (machine_state_ == MachineState::Fault)
    {
        return; // no-op
    }

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

    // Motion-Safety is independent of pneumatic actuators (never modified by safety). // M11 doc
    // Software STOP (stop()) => MachineState::Stopped; hardware E-Stop (e_stop) => Fault.

    // Latched Fault: no further motion/safety evaluation until clearFault(). // M11 fault latch
    if (machine_state_ == MachineState::Fault)
    {
        return; // no-op while fault latched
    }

    // 1. E-Stop (highest priority): Idle, Active, Paused, Stopped => Fault. // M11 e-stop
    if (host_inputs_.e_stop)
    {
        enterSafetyFault(); // stop motion only; pneumatics unchanged
        return; // strict priority
    }

    // 2. Directional limits while Active. // M11 limits
    if (machine_state_ == MachineState::Active && isCommandedIntoActiveLimit())
    {
        enterSafetyFault(); // stop motion only; pneumatics unchanged
        return; // strict priority
    }

    // Only Active state advances the MotionPlanner and evaluates M10. // active only
    if (machine_state_ != MachineState::Active)
    {
        return; // no motion while paused/stopped/idle
    }

    // 3. Fz-Safety: abs(Fz) >= threshold => Fault + freeze + no completion. // M10 safety
    const float fz = host_inputs_.force.fz;
    if (std::fabs(fz) >= force_params_.fz_safety_threshold_N)
    {
        enterSafetyFault(); // stop motion only; pneumatics unchanged
        return; // strict priority: no XY correction and no planner tick in this tick
    }

    // Detect SlowZApproach for XY control. // M10 slow phase detection
    const float z_remaining = stored_target_z_ - motion_planner_.z();
    const bool slow_z = std::fabs(z_remaining) <= force_params_.slow_z_distance_threshold_units;
    force_phase_ = slow_z ? ForceControlPhase::SlowZApproach : ForceControlPhase::NormalZApproach;

    // Reset hysteresis states outside slow phase. // M10 ignore Fx/Fy in Normal phase
    if (force_phase_ != ForceControlPhase::SlowZApproach)
    {
        x_force_correction_active_ = false;
        y_force_correction_active_ = false;
    }

    // Fx-Korrektur (continuous, independent, hysteresis) before MotionPlanner tick. // M10 XY X
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

    // Fy-Korrektur (continuous, independent, hysteresis) before MotionPlanner tick. // M10 XY Y
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

    // Normal MotionPlanner tick last. // delegate tick
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
    // Software STOP: operator command => Stopped (distinct from E-Stop => Fault). // M11 doc
    if (machine_state_ == MachineState::Stopped)
    {
        return; // already stopped
    }
    if (machine_state_ == MachineState::Fault)
    {
        return; // keep fault latched
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
