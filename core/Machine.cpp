#include "Machine.hpp"

#include <cmath>

namespace
{

bool isBlockedTowardTarget(float position, float target, bool limit_min, bool limit_max)
{
    if (limit_min && target < position - kPositionTolerance)
    {
        return true;
    }
    if (limit_max && target > position + kPositionTolerance)
    {
        return true;
    }
    return false;
}

} // namespace

Machine::Machine()
    : motion_planner_()
    , machine_state_(MachineState::Idle)
    , default_max_velocity_(10.0f)
    , default_acceleration_(0.0f)
    , stored_target_x_(0.0f)
    , stored_target_y_(0.0f)
    , stored_target_z_(0.0f)
    , commanded_target_x_(0.0f)
    , commanded_target_y_(0.0f)
    , host_inputs_{}
    , host_outputs_{}
    , force_params_{}
    , force_phase_(ForceControlPhase::NormalZApproach)
    , x_force_correction_active_(false)
    , y_force_correction_active_(false)
    , blocked_directions_(0)
    , homing_state_(HomingState::None)
    , is_homed_(false)
{
    motion_planner_.setMotionParams(default_max_velocity_, default_acceleration_);
}

Machine::Machine(const ForceControlParams& params)
    : motion_planner_()
    , machine_state_(MachineState::Idle)
    , default_max_velocity_(10.0f)
    , default_acceleration_(0.0f)
    , stored_target_x_(0.0f)
    , stored_target_y_(0.0f)
    , stored_target_z_(0.0f)
    , commanded_target_x_(0.0f)
    , commanded_target_y_(0.0f)
    , host_inputs_{}
    , host_outputs_{}
    , force_params_(params)
    , force_phase_(ForceControlPhase::NormalZApproach)
    , x_force_correction_active_(false)
    , y_force_correction_active_(false)
    , blocked_directions_(0)
    , homing_state_(HomingState::None)
    , is_homed_(false)
{
    motion_planner_.setMotionParams(default_max_velocity_, default_acceleration_);
}

MachineState Machine::machineState() const
{
    return machine_state_;
}

MotionState Machine::motionState() const
{
    return motion_planner_.motionState();
}

HomingState Machine::homingState() const
{
    return homing_state_;
}

bool Machine::isHomed() const
{
    return is_homed_;
}

uint8_t Machine::blockedDirections() const
{
    return blocked_directions_;
}

bool Machine::limitWarningActive() const
{
    return blocked_directions_ != 0 || activeLimitSwitchMask() != 0;
}

void Machine::setHostInputs(const HostInputs& inputs)
{
    host_inputs_ = inputs;
}

const HostInputs& Machine::hostInputs() const
{
    return host_inputs_;
}

HostOutputs Machine::hostOutputs() const
{
    return host_outputs_;
}

void Machine::setSpreaderExtend(bool extend)
{
    if (machine_state_ == MachineState::Fault)
    {
        return;
    }

    host_outputs_.spreaderExtend = extend;
    updatePneumaticStub();
}

void Machine::setFixtureExtend(bool extend)
{
    if (machine_state_ == MachineState::Fault)
    {
        return;
    }

    host_outputs_.fixtureExtend = extend;
    updatePneumaticStub();
}

void Machine::updatePneumaticStub()
{
    host_outputs_.spreaderExtended = host_outputs_.spreaderExtend;
    host_outputs_.fixtureExtended = host_outputs_.fixtureExtend;
}

uint8_t Machine::activeLimitSwitchMask() const
{
    uint8_t mask = 0;
    if (host_inputs_.limitXMin)
    {
        mask |= static_cast<uint8_t>(1u << kLimitDirXMin);
    }
    if (host_inputs_.limitXMax)
    {
        mask |= static_cast<uint8_t>(1u << kLimitDirXMax);
    }
    if (host_inputs_.limitYMin)
    {
        mask |= static_cast<uint8_t>(1u << kLimitDirYMin);
    }
    if (host_inputs_.limitYMax)
    {
        mask |= static_cast<uint8_t>(1u << kLimitDirYMax);
    }
    if (host_inputs_.limitZMin)
    {
        mask |= static_cast<uint8_t>(1u << kLimitDirZMin);
    }
    if (host_inputs_.limitZMax)
    {
        mask |= static_cast<uint8_t>(1u << kLimitDirZMax);
    }
    return mask;
}

uint8_t Machine::computeBlockedDirections(float target_x, float target_y, float target_z) const
{
    const float pos_x = motion_planner_.x();
    const float pos_y = motion_planner_.y();
    const float pos_z = motion_planner_.z();

    uint8_t blocked = 0;

    if (isBlockedTowardTarget(pos_x, target_x, host_inputs_.limitXMin, host_inputs_.limitXMax))
    {
        if (host_inputs_.limitXMin && target_x < pos_x - kPositionTolerance)
        {
            blocked |= static_cast<uint8_t>(1u << kLimitDirXMin);
        }
        if (host_inputs_.limitXMax && target_x > pos_x + kPositionTolerance)
        {
            blocked |= static_cast<uint8_t>(1u << kLimitDirXMax);
        }
    }
    if (isBlockedTowardTarget(pos_y, target_y, host_inputs_.limitYMin, host_inputs_.limitYMax))
    {
        if (host_inputs_.limitYMin && target_y < pos_y - kPositionTolerance)
        {
            blocked |= static_cast<uint8_t>(1u << kLimitDirYMin);
        }
        if (host_inputs_.limitYMax && target_y > pos_y + kPositionTolerance)
        {
            blocked |= static_cast<uint8_t>(1u << kLimitDirYMax);
        }
    }
    if (isBlockedTowardTarget(pos_z, target_z, host_inputs_.limitZMin, host_inputs_.limitZMax))
    {
        if (host_inputs_.limitZMin && target_z < pos_z - kPositionTolerance)
        {
            blocked |= static_cast<uint8_t>(1u << kLimitDirZMin);
        }
        if (host_inputs_.limitZMax && target_z > pos_z + kPositionTolerance)
        {
            blocked |= static_cast<uint8_t>(1u << kLimitDirZMax);
        }
    }

    return blocked;
}

void Machine::applyEffectiveTargets(float target_x, float target_y, float target_z)
{
    blocked_directions_ = computeBlockedDirections(target_x, target_y, target_z);

    const float pos_x = motion_planner_.x();
    const float pos_y = motion_planner_.y();
    const float pos_z = motion_planner_.z();

    float effective_x = target_x;
    float effective_y = target_y;
    float effective_z = target_z;

    if ((blocked_directions_ & (1u << kLimitDirXMin)) != 0 ||
        (blocked_directions_ & (1u << kLimitDirXMax)) != 0)
    {
        effective_x = pos_x;
    }
    if ((blocked_directions_ & (1u << kLimitDirYMin)) != 0 ||
        (blocked_directions_ & (1u << kLimitDirYMax)) != 0)
    {
        effective_y = pos_y;
    }
    if ((blocked_directions_ & (1u << kLimitDirZMin)) != 0 ||
        (blocked_directions_ & (1u << kLimitDirZMax)) != 0)
    {
        effective_z = pos_z;
    }

    motion_planner_.moveTo(effective_x, effective_y, effective_z);
}

void Machine::enterSafetyFault()
{
    motion_planner_.stopMotion();
    machine_state_ = MachineState::Fault;
    if (homing_state_ == HomingState::HomingZ || homing_state_ == HomingState::HomingX ||
        homing_state_ == HomingState::HomingY)
    {
        homing_state_ = HomingState::HomingError;
    }
}

void Machine::clearFault()
{
    if (machine_state_ != MachineState::Fault)
    {
        return;
    }
    if (host_inputs_.e_stop)
    {
        return;
    }
    const float fz = host_inputs_.force.fz;
    if (std::fabs(fz) >= force_params_.fz_safety_threshold_N)
    {
        return;
    }

    machine_state_ = MachineState::Idle;
}

bool Machine::home()
{
    if (machine_state_ == MachineState::Fault)
    {
        return false;
    }

    is_homed_ = false;
    homing_state_ = HomingState::HomingZ;
    machine_state_ = MachineState::Homing;

    motion_planner_.setMotionParams(kHomingVelocity, default_acceleration_);
    beginHomingPhase(HomingState::HomingZ);
    return true;
}

void Machine::beginHomingPhase(HomingState phase)
{
    homing_state_ = phase;

    const float pos_x = motion_planner_.x();
    const float pos_y = motion_planner_.y();
    const float pos_z = motion_planner_.z();

    stored_target_x_ = pos_x;
    stored_target_y_ = pos_y;
    stored_target_z_ = pos_z;
    commanded_target_x_ = pos_x;
    commanded_target_y_ = pos_y;

    switch (phase)
    {
    case HomingState::HomingZ:
        stored_target_z_ = pos_z + kHomingOvershoot;
        break;
    case HomingState::HomingX:
        stored_target_x_ = pos_x + kHomingOvershoot;
        stored_target_z_ = pos_z;
        break;
    case HomingState::HomingY:
        stored_target_y_ = pos_y + kHomingOvershoot;
        stored_target_x_ = pos_x;
        stored_target_z_ = pos_z;
        break;
    default:
        break;
    }

    force_phase_ = ForceControlPhase::NormalZApproach;
    x_force_correction_active_ = false;
    y_force_correction_active_ = false;

    motion_planner_.setMotionParams(kHomingVelocity, default_acceleration_);
    applyEffectiveTargets(stored_target_x_, stored_target_y_, stored_target_z_);
}

bool Machine::homingPhaseLimitReached(HomingState phase) const
{
    switch (phase)
    {
    case HomingState::HomingZ:
        return host_inputs_.limitZMax;
    case HomingState::HomingX:
        return host_inputs_.limitXMax;
    case HomingState::HomingY:
        return host_inputs_.limitYMax;
    default:
        return false;
    }
}

void Machine::finishHomingSuccess()
{
    homing_state_ = HomingState::Homed;
    is_homed_ = true;
    machine_state_ = MachineState::Idle;
    motion_planner_.setMotionParams(default_max_velocity_, default_acceleration_);
    motion_planner_.stopMotion();
    blocked_directions_ = 0;
}

void Machine::finishHomingError()
{
    homing_state_ = HomingState::HomingError;
    motion_planner_.setMotionParams(default_max_velocity_, default_acceleration_);
    motion_planner_.stopMotion();
}

void Machine::tickHoming(float dt)
{
    if (host_inputs_.e_stop)
    {
        enterSafetyFault();
        return;
    }

    const float fz = host_inputs_.force.fz;
    if (std::fabs(fz) >= force_params_.fz_safety_threshold_N)
    {
        enterSafetyFault();
        return;
    }

    if (homingPhaseLimitReached(homing_state_))
    {
        motion_planner_.stopMotion();

        switch (homing_state_)
        {
        case HomingState::HomingZ:
            beginHomingPhase(HomingState::HomingX);
            break;
        case HomingState::HomingX:
            beginHomingPhase(HomingState::HomingY);
            break;
        case HomingState::HomingY:
            finishHomingSuccess();
            return;
        default:
            break;
        }
    }

    if (machine_state_ != MachineState::Homing)
    {
        return;
    }

    // Hold non-homing axes at current position; drive only the active homing axis toward +limit.
    const float pos_x = motion_planner_.x();
    const float pos_y = motion_planner_.y();
    const float pos_z = motion_planner_.z();

    float tx = pos_x;
    float ty = pos_y;
    float tz = pos_z;

    switch (homing_state_)
    {
    case HomingState::HomingZ:
        tz = stored_target_z_;
        break;
    case HomingState::HomingX:
        tx = stored_target_x_;
        tz = pos_z;
        break;
    case HomingState::HomingY:
        ty = stored_target_y_;
        tx = pos_x;
        tz = pos_z;
        break;
    default:
        break;
    }

    // Homing ignores soft-limit blocking toward the reference; limit reached ends the phase above.
    blocked_directions_ = 0;
    motion_planner_.moveTo(tx, ty, tz);
    motion_planner_.tick(dt);
}

void Machine::moveTo(float x, float y, float z)
{
    if (machine_state_ == MachineState::Fault)
    {
        return;
    }
    if (machine_state_ == MachineState::Homing)
    {
        return;
    }

    stored_target_x_ = x;
    stored_target_y_ = y;
    stored_target_z_ = z;
    commanded_target_x_ = x;
    commanded_target_y_ = y;

    force_phase_ = ForceControlPhase::NormalZApproach;
    x_force_correction_active_ = false;
    y_force_correction_active_ = false;

    motion_planner_.setMotionParams(default_max_velocity_, default_acceleration_);
    applyEffectiveTargets(stored_target_x_, stored_target_y_, stored_target_z_);

    machine_state_ = (motion_planner_.motionState() == MotionState::AtTarget) ? MachineState::Idle
                                                                               : MachineState::Active;
}

void Machine::tick(float dt)
{
    if (dt <= 0.0f)
    {
        return;
    }

    if (machine_state_ == MachineState::Fault)
    {
        return;
    }

    if (host_inputs_.e_stop)
    {
        enterSafetyFault();
        return;
    }

    if (machine_state_ == MachineState::Homing)
    {
        tickHoming(dt);
        return;
    }

    if (machine_state_ == MachineState::Active)
    {
        tickActiveMotion(dt);
    }
}

void Machine::tickActiveMotion(float dt)
{
    const float fz = host_inputs_.force.fz;
    if (std::fabs(fz) >= force_params_.fz_safety_threshold_N)
    {
        enterSafetyFault();
        return;
    }

    const float z_remaining = stored_target_z_ - motion_planner_.z();
    const bool slow_z = std::fabs(z_remaining) <= force_params_.slow_z_distance_threshold_units;
    force_phase_ = slow_z ? ForceControlPhase::SlowZApproach : ForceControlPhase::NormalZApproach;

    if (force_phase_ != ForceControlPhase::SlowZApproach)
    {
        x_force_correction_active_ = false;
        y_force_correction_active_ = false;
    }

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
            if (fx > 0.0f)
            {
                stored_target_x_ -= force_params_.dx_step_units;
            }
            else if (fx < 0.0f)
            {
                stored_target_x_ += force_params_.dx_step_units;
            }

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
            if (fy > 0.0f)
            {
                stored_target_y_ -= force_params_.dy_step_units;
            }
            else if (fy < 0.0f)
            {
                stored_target_y_ += force_params_.dy_step_units;
            }

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

    applyEffectiveTargets(stored_target_x_, stored_target_y_, stored_target_z_);
    motion_planner_.tick(dt);

    if (motion_planner_.motionState() == MotionState::AtTarget)
    {
        machine_state_ = MachineState::Idle;
    }
}

void Machine::pause()
{
    if (machine_state_ != MachineState::Active)
    {
        return;
    }

    motion_planner_.stopMotion();
    machine_state_ = MachineState::Paused;
}

void Machine::resume()
{
    if (machine_state_ != MachineState::Paused)
    {
        return;
    }

    motion_planner_.setMotionParams(default_max_velocity_, default_acceleration_);
    applyEffectiveTargets(stored_target_x_, stored_target_y_, stored_target_z_);
    machine_state_ = (motion_planner_.motionState() == MotionState::AtTarget) ? MachineState::Idle
                                                                               : MachineState::Active;
}

void Machine::stop()
{
    if (machine_state_ == MachineState::Stopped)
    {
        return;
    }
    if (machine_state_ == MachineState::Fault)
    {
        return;
    }

    motion_planner_.stopMotion();
    motion_planner_.setMotionParams(default_max_velocity_, default_acceleration_);

    if (machine_state_ == MachineState::Homing)
    {
        finishHomingError();
        machine_state_ = MachineState::Stopped;
        return;
    }

    machine_state_ = MachineState::Stopped;
}

float Machine::x() const
{
    return motion_planner_.x();
}

float Machine::y() const
{
    return motion_planner_.y();
}

float Machine::z() const
{
    return motion_planner_.z();
}
