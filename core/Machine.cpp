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
    , default_corner_radius_(0.0f)
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
    , homing_retracting_(false)
    , homing_mode_(HomingMode::DefineMcs)
    , homing_axis_leg_(HomingAxisLeg::FastSeek)
    , homing_creep_back_traveled_(0.0f)
    , homing_have_creep_pre_(false)
    , homing_creep_pre_raw_x_(0.0f)
    , homing_creep_pre_raw_y_(0.0f)
    , homing_creep_pre_raw_z_(0.0f)
    , collision_context_(CollisionContext::Approach)
    , last_force_watchdog_trip_(ForceWatchdogTrip::None)
    , pre_clamp_xy_enabled_(false)
    , xy_correction_steps_(0)
    , last_fault_reason_(FaultReason::None)
    , force_sample_age_s_(0.0f)
    , last_force_sample_sequence_(0)
    , have_force_sample_(false)
    , force_sample_fresh_(false)
    , comm_watchdog_enabled_(false)
    , heartbeat_fresh_(false)
    , heartbeat_age_s_(0.0f)
    , last_heartbeat_sequence_(0)
    , have_heartbeat_(false)
    , runtime_config_locked_(false)
    , probe_trigger_latched_(false)
    , prev_probe_triggered_(false)
    , last_probe_trigger_sequence_(0)
{
    motion_planner_.setMotionParams(default_max_velocity_, default_acceleration_,
                                    default_corner_radius_);
}

Machine::Machine(const ForceControlParams& params)
    : motion_planner_()
    , machine_state_(MachineState::Idle)
    , default_max_velocity_(10.0f)
    , default_acceleration_(0.0f)
    , default_corner_radius_(0.0f)
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
    , homing_retracting_(false)
    , homing_mode_(HomingMode::DefineMcs)
    , homing_axis_leg_(HomingAxisLeg::FastSeek)
    , homing_creep_back_traveled_(0.0f)
    , homing_have_creep_pre_(false)
    , homing_creep_pre_raw_x_(0.0f)
    , homing_creep_pre_raw_y_(0.0f)
    , homing_creep_pre_raw_z_(0.0f)
    , collision_context_(CollisionContext::Approach)
    , last_force_watchdog_trip_(ForceWatchdogTrip::None)
    , pre_clamp_xy_enabled_(false)
    , xy_correction_steps_(0)
    , last_fault_reason_(FaultReason::None)
    , force_sample_age_s_(0.0f)
    , last_force_sample_sequence_(0)
    , have_force_sample_(false)
    , force_sample_fresh_(false)
    , comm_watchdog_enabled_(false)
    , heartbeat_fresh_(false)
    , heartbeat_age_s_(0.0f)
    , last_heartbeat_sequence_(0)
    , have_heartbeat_(false)
    , runtime_config_locked_(false)
    , probe_trigger_latched_(false)
    , prev_probe_triggered_(false)
    , last_probe_trigger_sequence_(0)
{
    motion_planner_.setMotionParams(default_max_velocity_, default_acceleration_,
                                    default_corner_radius_);
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

void Machine::invalidateHoming()
{
    is_homed_ = false;
    if (homing_state_ == HomingState::Homed)
    {
        homing_state_ = HomingState::None;
    }
}

uint8_t Machine::blockedDirections() const
{
    return blocked_directions_;
}

bool Machine::limitWarningActive() const
{
    // Homing intentionally drives into +max switches; that is not a fault/warning.
    if (machine_state_ == MachineState::Homing)
    {
        return false;
    }
    return blocked_directions_ != 0 || activeLimitSwitchMask() != 0;
}

void Machine::setHostInputs(const HostInputs& inputs)
{
    host_inputs_ = inputs;
    if (inputs.forceSampleValid &&
        (!have_force_sample_ || inputs.forceSampleSequence != last_force_sample_sequence_))
    {
        have_force_sample_ = true;
        last_force_sample_sequence_ = inputs.forceSampleSequence;
        force_sample_age_s_ = 0.0f;
        force_sample_fresh_ = true;
    }
    if (inputs.hostHeartbeatValid &&
        (!have_heartbeat_ || inputs.hostHeartbeatSequence != last_heartbeat_sequence_))
    {
        have_heartbeat_ = true;
        last_heartbeat_sequence_ = inputs.hostHeartbeatSequence;
        heartbeat_age_s_ = 0.0f;
        heartbeat_fresh_ = true;
    }
    const bool rising = inputs.probeTriggered && !prev_probe_triggered_;
    const bool new_seq = inputs.probeTriggerSequence != 0 &&
                         inputs.probeTriggerSequence != last_probe_trigger_sequence_;
    if (rising || new_seq)
    {
        if (inputs.probeTriggerSequence != 0)
        {
            last_probe_trigger_sequence_ = inputs.probeTriggerSequence;
        }
        if (!probe_trigger_latched_)
        {
            // Production hardening: Pose must be explicitly host-latched.
            // If pose is missing/invalid, we do NOT fall back to the current TCP.
            if (inputs.probeTriggerPoseValid && std::isfinite(inputs.probeTriggerX) &&
                std::isfinite(inputs.probeTriggerY) && std::isfinite(inputs.probeTriggerZ))
            {
                probe_trigger_latched_ = true;
                probe_trigger_pose_ = TestPoint{inputs.probeTriggerX, inputs.probeTriggerY, inputs.probeTriggerZ};
            }
        }
    }
    prev_probe_triggered_ = inputs.probeTriggered;
}

void Machine::setHostInputUpdateCallback(HostInputUpdateCallback cb)
{
    host_input_update_callback_ = std::move(cb);
}

void Machine::invokeHostInputUpdateCallback()
{
    if (host_input_update_callback_)
    {
        host_input_update_callback_(*this);
    }
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

    if (extend)
    {
        spreader_timer_.commandExtend(machine_config_.spreader.extend_time_s);
    }
    else
    {
        spreader_timer_.commandRetract(machine_config_.spreader.retract_time_s);
    }
    host_outputs_.spreaderExtend = spreader_timer_.command();
    host_outputs_.spreaderExtended = spreader_timer_.extended();
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
    host_outputs_.fixtureExtended = host_outputs_.fixtureExtend;
    host_outputs_.spreaderExtend = spreader_timer_.command();
    host_outputs_.spreaderExtended = spreader_timer_.extended();
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

void Machine::enterSafetyFault(FaultReason reason)
{
    motion_planner_.stopMotion();
    applySpreaderSafeState();
    machine_state_ = MachineState::Fault;
    last_fault_reason_ = reason;
    pre_clamp_xy_enabled_ = false;
    x_force_correction_active_ = false;
    y_force_correction_active_ = false;
    if (homing_state_ == HomingState::HomingZ || homing_state_ == HomingState::HomingX ||
        homing_state_ == HomingState::HomingY)
    {
        homing_state_ = HomingState::HomingError;
    }
}

void Machine::applySpreaderSafeState()
{
    spreader_timer_.enterSafeState();
    host_outputs_.spreaderExtend = spreader_timer_.command();
    host_outputs_.spreaderExtended = spreader_timer_.extended();
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
    last_fault_reason_ = FaultReason::None;
}

bool Machine::home()
{
    // Reference-only: never auto-define MCS (use defineMachineCoordinateSystem).
    if (!calibration_set_.machine_coordinate_system_valid)
    {
        return false;
    }
    return startHomingSequence(HomingMode::Reference);
}

bool Machine::defineMachineCoordinateSystem()
{
    return startHomingSequence(HomingMode::DefineMcs);
}

bool Machine::restoreMachineCoordinateSystem(const TestPoint& home_position)
{
    if (machine_state_ == MachineState::Fault || machine_state_ == MachineState::Homing)
    {
        return false;
    }
    if (machine_state_ == MachineState::Active || machine_state_ == MachineState::Paused)
    {
        return false;
    }
    calibration_set_.home_offset = home_position;
    calibration_set_.machine_coordinate_system_valid = true;
    return true;
}

bool Machine::machineCoordinateSystemValid() const
{
    return calibration_set_.machine_coordinate_system_valid;
}

bool Machine::confirmHomedIfAtHome(float tolerance_mm)
{
    if (!calibration_set_.machine_coordinate_system_valid)
    {
        return false;
    }
    if (machine_state_ == MachineState::Fault || machine_state_ == MachineState::Homing)
    {
        return false;
    }
    const TestPoint& home = calibration_set_.home_offset;
    const float tol = (tolerance_mm > 0.0f) ? tolerance_mm : 0.5f;
    if (std::fabs(motion_planner_.x() - home.x) > tol ||
        std::fabs(motion_planner_.y() - home.y) > tol ||
        std::fabs(motion_planner_.z() - home.z) > tol)
    {
        return false;
    }
    motion_planner_.stopMotion();
    stored_target_x_ = home.x;
    stored_target_y_ = home.y;
    stored_target_z_ = home.z;
    commanded_target_x_ = home.x;
    commanded_target_y_ = home.y;
    homing_state_ = HomingState::Homed;
    is_homed_ = true;
    homing_retracting_ = false;
    machine_state_ = MachineState::Idle;
    return true;
}

void Machine::shiftRawPose(float dx, float dy, float dz)
{
    motion_planner_.shiftRawPose(dx, dy, dz);
}

bool Machine::startHomingSequence(HomingMode mode)
{
    if (machine_state_ == MachineState::Fault)
    {
        return false;
    }

    homing_mode_ = mode;
    is_homed_ = false;
    homing_retracting_ = false;
    homing_axis_leg_ = HomingAxisLeg::FastSeek;
    homing_creep_back_traveled_ = 0.0f;
    homing_state_ = HomingState::HomingZ;
    machine_state_ = MachineState::Homing;

    setHomingMotionSpeed(machine_config_.motion.v_homing);
    beginHomingPhase(HomingState::HomingZ);
    return true;
}

void Machine::beginHomingPhase(HomingState phase)
{
    homing_state_ = phase;
    homing_retracting_ = false;
    homing_axis_leg_ = HomingAxisLeg::FastSeek;
    homing_creep_back_traveled_ = 0.0f;

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

    setHomingMotionSpeed(machine_config_.motion.v_homing);
    applyEffectiveTargets(stored_target_x_, stored_target_y_, stored_target_z_);
}

float Machine::homingRetractDistance(float configured) const
{
    const float distance = std::fabs(configured);
    return distance > 1.0e-4f ? distance : kDefaultHomingRetract;
}

float Machine::homingCreepVelocity() const
{
    const float from_cfg = machine_config_.motion.v_homing * kHomingCreepSpeedFactor;
    return from_cfg > kHomingCreepMinSpeed ? from_cfg : kHomingCreepMinSpeed;
}

void Machine::setHomingMotionSpeed(float velocity)
{
    const float v = (velocity > 0.0f) ? velocity : kHomingCreepMinSpeed;
    motion_planner_.setMotionParams(v, machine_config_.motion.a_homing, 0.0f);
}

void Machine::commandActiveAxisToward(float signed_delta)
{
    const float pos_x = motion_planner_.x();
    const float pos_y = motion_planner_.y();
    const float pos_z = motion_planner_.z();
    stored_target_x_ = pos_x;
    stored_target_y_ = pos_y;
    stored_target_z_ = pos_z;
    commanded_target_x_ = pos_x;
    commanded_target_y_ = pos_y;
    switch (homing_state_)
    {
    case HomingState::HomingZ:
        stored_target_z_ = pos_z + signed_delta;
        break;
    case HomingState::HomingX:
        stored_target_x_ = pos_x + signed_delta;
        commanded_target_x_ = stored_target_x_;
        break;
    case HomingState::HomingY:
        stored_target_y_ = pos_y + signed_delta;
        commanded_target_y_ = stored_target_y_;
        break;
    default:
        break;
    }
    applyEffectiveTargets(stored_target_x_, stored_target_y_, stored_target_z_);
}

void Machine::beginCreepBack()
{
    homing_axis_leg_ = HomingAxisLeg::CreepBack;
    homing_retracting_ = false;
    homing_creep_back_traveled_ = 0.0f;
    motion_planner_.stopMotion();
    setHomingMotionSpeed(homingCreepVelocity());
    commandActiveAxisToward(-kHomingCreepBackTravel);
}

void Machine::beginCreepForward()
{
    homing_axis_leg_ = HomingAxisLeg::CreepForward;
    homing_retracting_ = false;
    homing_have_creep_pre_ = false;
    motion_planner_.stopMotion();
    setHomingMotionSpeed(homingCreepVelocity());
    commandActiveAxisToward(kHomingOvershoot);
}

void Machine::beginHomingRetract(HomingState phase, float switch_raw_x, float switch_raw_y,
                                 float switch_raw_z)
{
    homing_axis_leg_ = HomingAxisLeg::RetractR;
    homing_retracting_ = true;
    motion_planner_.stopMotion();

    const TestPoint& cfg_r = machine_config_.calibration.home_offset;
    float retract = kDefaultHomingRetract;
    float x = motion_planner_.x();
    float y = motion_planner_.y();
    float z = motion_planner_.z();

    // Snap machine frame at the physical +max switch without moving raw (no teleport).
    // DefineMcs: identity on this axis at the creep rising-edge raw pose.
    // Reference: snap to calibrated switch pose (home + R) — corrects step loss, same MCS.
    switch (phase)
    {
    case HomingState::HomingZ:
        retract = homingRetractDistance(cfg_r.z);
        if (homing_mode_ == HomingMode::DefineMcs)
        {
            z = switch_raw_z;
        }
        else
        {
            z = calibration_set_.home_offset.z + retract;
        }
        break;
    case HomingState::HomingX:
        retract = homingRetractDistance(cfg_r.x);
        if (homing_mode_ == HomingMode::DefineMcs)
        {
            x = switch_raw_x;
        }
        else
        {
            x = calibration_set_.home_offset.x + retract;
        }
        break;
    case HomingState::HomingY:
        retract = homingRetractDistance(cfg_r.y);
        if (homing_mode_ == HomingMode::DefineMcs)
        {
            y = switch_raw_y;
        }
        else
        {
            y = calibration_set_.home_offset.y + retract;
        }
        break;
    default:
        break;
    }

    motion_planner_.referenceMachinePose(x, y, z);

    stored_target_x_ = x;
    stored_target_y_ = y;
    stored_target_z_ = z;
    commanded_target_x_ = x;
    commanded_target_y_ = y;

    // Retract toward working volume (−axis) by R.
    switch (phase)
    {
    case HomingState::HomingZ:
        stored_target_z_ =
            (homing_mode_ == HomingMode::DefineMcs) ? (z - retract) : calibration_set_.home_offset.z;
        break;
    case HomingState::HomingX:
        stored_target_x_ =
            (homing_mode_ == HomingMode::DefineMcs) ? (x - retract) : calibration_set_.home_offset.x;
        commanded_target_x_ = stored_target_x_;
        break;
    case HomingState::HomingY:
        stored_target_y_ =
            (homing_mode_ == HomingMode::DefineMcs) ? (y - retract) : calibration_set_.home_offset.y;
        commanded_target_y_ = stored_target_y_;
        break;
    default:
        break;
    }

    setHomingMotionSpeed(machine_config_.motion.v_homing);
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
    // Commanded retract targets after HomingY — preferred over live pose for DefineMcs latch.
    const float home_x = stored_target_x_;
    const float home_y = stored_target_y_;
    const float home_z = stored_target_z_;

    homing_state_ = HomingState::Homed;
    is_homed_ = true;
    homing_retracting_ = false;
    homing_axis_leg_ = HomingAxisLeg::FastSeek;
    machine_state_ = MachineState::Idle;
    motion_planner_.stopMotion();

    if (homing_mode_ == HomingMode::DefineMcs)
    {
        // Latch calibrated home pose. Intentional MCS (re)definition requires re-teach
        // of plate/probe in the new frame — home() never takes this path.
        calibration_set_.home_offset = TestPoint{home_x, home_y, home_z};
        calibration_set_.machine_coordinate_system_valid = true;
        calibration_set_.plate_origin_valid = false;
        calibration_set_.probe_center_valid = false;
        calibration_set_.tool_offset = TestPoint{0.0f, 0.0f, 0.0f};
        machine_config_.calibration.tool_offset = calibration_set_.tool_offset;
    }
    // Reference mode: home_offset / plate / probe / tool_offset unchanged.

    stored_target_x_ = home_x;
    stored_target_y_ = home_y;
    stored_target_z_ = home_z;
    commanded_target_x_ = stored_target_x_;
    commanded_target_y_ = stored_target_y_;
    motion_planner_.setMotionParams(default_max_velocity_, default_acceleration_,
                                    default_corner_radius_);
    blocked_directions_ = 0;
}

void Machine::finishHomingError()
{
    homing_state_ = HomingState::HomingError;
    homing_retracting_ = false;
    homing_axis_leg_ = HomingAxisLeg::FastSeek;
    motion_planner_.setMotionParams(default_max_velocity_, default_acceleration_,
                                    default_corner_radius_);
    motion_planner_.stopMotion();
}

void Machine::tickHoming(float dt)
{
    if (host_inputs_.e_stop)
    {
        enterSafetyFault(FaultReason::EStop);
        return;
    }

    const float fz = host_inputs_.force.fz;
    if (std::fabs(fz) >= force_params_.fz_safety_threshold_N)
    {
        enterSafetyFault(FaultReason::ForceHardLimit);
        return;
    }

    // Retract-R leg: leave the +max switch by R, then advance to next axis / finish.
    if (homing_axis_leg_ == HomingAxisLeg::RetractR || homing_retracting_)
    {
        blocked_directions_ = 0;
        motion_planner_.moveTo(stored_target_x_, stored_target_y_, stored_target_z_);
        motion_planner_.tick(dt);
        if (motion_planner_.motionState() == MotionState::AtTarget)
        {
            homing_retracting_ = false;
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
        return;
    }

    if (machine_state_ != MachineState::Homing)
    {
        return;
    }

    // DefineMcs creep-back: leave the switch until the +max bit clears.
    if (homing_axis_leg_ == HomingAxisLeg::CreepBack)
    {
        blocked_directions_ = 0;
        motion_planner_.moveTo(stored_target_x_, stored_target_y_, stored_target_z_);
        motion_planner_.tick(dt);
        invokeHostInputUpdateCallback();

        if (!homingPhaseLimitReached(homing_state_))
        {
            beginCreepForward();
            return;
        }

        if (motion_planner_.motionState() == MotionState::AtTarget)
        {
            homing_creep_back_traveled_ += kHomingCreepBackTravel;
            if (homing_creep_back_traveled_ >= kHomingCreepBackMaxTravel)
            {
                finishHomingError();
                return;
            }
            commandActiveAxisToward(-kHomingCreepBackTravel);
        }
        return;
    }

    // DefineMcs creep-forward: rising edge of +max at creep speed.
    if (homing_axis_leg_ == HomingAxisLeg::CreepForward)
    {
        auto latchCreepRisingEdge = [&](float pre_x, float pre_y, float pre_z, float post_x,
                                        float post_y, float post_z) {
            float sx = post_x;
            float sy = post_y;
            float sz = post_z;
            switch (homing_state_)
            {
            case HomingState::HomingX:
                sx = 0.5f * (pre_x + post_x);
                break;
            case HomingState::HomingY:
                sy = 0.5f * (pre_y + post_y);
                break;
            case HomingState::HomingZ:
                sz = 0.5f * (pre_z + post_z);
                break;
            default:
                break;
            }
            beginHomingRetract(homing_state_, sx, sy, sz);
        };

        // Limit already true at tick start: rising edge was the previous step.
        if (homingPhaseLimitReached(homing_state_))
        {
            const float post_x = motion_planner_.rawX();
            const float post_y = motion_planner_.rawY();
            const float post_z = motion_planner_.rawZ();
            if (homing_have_creep_pre_)
            {
                latchCreepRisingEdge(homing_creep_pre_raw_x_, homing_creep_pre_raw_y_,
                                     homing_creep_pre_raw_z_, post_x, post_y, post_z);
            }
            else
            {
                beginHomingRetract(homing_state_, post_x, post_y, post_z);
            }
            return;
        }

        homing_creep_pre_raw_x_ = motion_planner_.rawX();
        homing_creep_pre_raw_y_ = motion_planner_.rawY();
        homing_creep_pre_raw_z_ = motion_planner_.rawZ();
        homing_have_creep_pre_ = true;

        blocked_directions_ = 0;
        motion_planner_.moveTo(stored_target_x_, stored_target_y_, stored_target_z_);
        motion_planner_.tick(dt);
        invokeHostInputUpdateCallback();

        if (homingPhaseLimitReached(homing_state_))
        {
            latchCreepRisingEdge(homing_creep_pre_raw_x_, homing_creep_pre_raw_y_,
                                 homing_creep_pre_raw_z_, motion_planner_.rawX(),
                                 motion_planner_.rawY(), motion_planner_.rawZ());
        }
        return;
    }

    // Fast seek toward +max.
    if (homingPhaseLimitReached(homing_state_))
    {
        if (homing_mode_ == HomingMode::DefineMcs)
        {
            beginCreepBack();
        }
        else
        {
            beginHomingRetract(homing_state_, motion_planner_.rawX(), motion_planner_.rawY(),
                               motion_planner_.rawZ());
        }
        return;
    }

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

    blocked_directions_ = 0;
    motion_planner_.moveTo(tx, ty, tz);
    motion_planner_.tick(dt);

    invokeHostInputUpdateCallback();
    if (homingPhaseLimitReached(homing_state_))
    {
        if (homing_mode_ == HomingMode::DefineMcs)
        {
            beginCreepBack();
        }
        else
        {
            beginHomingRetract(homing_state_, motion_planner_.rawX(), motion_planner_.rawY(),
                               motion_planner_.rawZ());
        }
    }
}

void Machine::moveTo(float x, float y, float z)
{
    if (machine_state_ == MachineState::Fault)
    {
        return;
    }
    // Homing drives the planner directly in tickHoming(); do not accept or
    // invalidate via this entry point while Homing is active.
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

    motion_planner_.setMotionParams(default_max_velocity_, default_acceleration_,
                                    default_corner_radius_);
    applyEffectiveTargets(stored_target_x_, stored_target_y_, stored_target_z_);

    const bool started_motion = motion_planner_.motionState() != MotionState::AtTarget;
    machine_state_ = started_motion ? MachineState::Active : MachineState::Idle;

    // Homed (is_homed_) means "still at the last successful home pose".
    // Any real axis motion through this central entry point clears that flag
    // (no-op moveTo stays Homed). HomingState::Homed is kept so coordinate
    // reference remains valid for M19 after a jog; STOP/abort calls
    // invalidateHoming() and clears that too.
    if (started_motion && is_homed_)
    {
        is_homed_ = false;
    }
}

void Machine::evaluateHostSafety(float dt)
{
    if (machine_state_ == MachineState::Fault)
    {
        return;
    }

    if (host_inputs_.e_stop)
    {
        enterSafetyFault(FaultReason::EStop);
        return;
    }

    if (!host_inputs_.forceSampleValid)
    {
        enterSafetyFault(FaultReason::ForceInvalidSample);
        return;
    }

    last_force_watchdog_trip_ =
        evaluateForceWatchdog(host_inputs_.force, machine_config_, collision_context_);
    if (last_force_watchdog_trip_ == ForceWatchdogTrip::NonFinite)
    {
        enterSafetyFault(FaultReason::ForceNonFinite);
        return;
    }
    if (last_force_watchdog_trip_ == ForceWatchdogTrip::HardLimit)
    {
        enterSafetyFault(FaultReason::ForceHardLimit);
        return;
    }
    if (last_force_watchdog_trip_ == ForceWatchdogTrip::CollisionContext)
    {
        enterSafetyFault(FaultReason::ForceCollision);
        return;
    }

    if (force_sample_fresh_)
    {
        force_sample_age_s_ = 0.0f;
        force_sample_fresh_ = false;
    }
    else
    {
        force_sample_age_s_ += dt;
        if (force_sample_age_s_ >= machine_config_.force.sample_timeout_s)
        {
            enterSafetyFault(FaultReason::ForceStale);
            return;
        }
    }

    if (comm_watchdog_enabled_)
    {
        if (heartbeat_fresh_)
        {
            heartbeat_age_s_ = 0.0f;
            heartbeat_fresh_ = false;
        }
        else
        {
            heartbeat_age_s_ += dt;
            if (heartbeat_age_s_ >= machine_config_.communication.heartbeat_timeout_s)
            {
                enterSafetyFault(FaultReason::CommunicationTimeout);
                return;
            }
        }
    }
}

void Machine::advanceKinematics(float dt)
{
    if (dt <= 0.0f || machine_state_ == MachineState::Fault)
    {
        return;
    }

    spreader_timer_.tick(dt);
    host_outputs_.spreaderExtend = spreader_timer_.command();
    host_outputs_.spreaderExtended = spreader_timer_.extended();

    if (machine_state_ == MachineState::Homing)
    {
        tickHoming(dt);
        return;
    }

    if (machine_state_ == MachineState::Active ||
        (pre_clamp_xy_enabled_ && machine_state_ == MachineState::Idle))
    {
        if (pre_clamp_xy_enabled_ && machine_state_ == MachineState::Idle)
        {
            machine_state_ = MachineState::Active;
        }
        tickActiveMotion(dt);
    }
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

    evaluateHostSafety(dt);
    if (machine_state_ == MachineState::Fault)
    {
        return;
    }
    advanceKinematics(dt);
}

void Machine::tickActiveMotion(float dt)
{
    const float fz = host_inputs_.force.fz;
    if (std::fabs(fz) >= force_params_.fz_safety_threshold_N)
    {
        enterSafetyFault(FaultReason::ForceHardLimit);
        return;
    }

    const bool apply_xy = pre_clamp_xy_enabled_;
    force_phase_ = apply_xy ? ForceControlPhase::SlowZApproach : ForceControlPhase::NormalZApproach;

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
            ++xy_correction_steps_;
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
            ++xy_correction_steps_;
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

    if (motion_planner_.motionState() == MotionState::AtTarget && !pre_clamp_xy_enabled_)
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

    motion_planner_.setMotionParams(default_max_velocity_, default_acceleration_,
                                    default_corner_radius_);
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
    motion_planner_.setMotionParams(default_max_velocity_, default_acceleration_,
                                    default_corner_radius_);
    applySpreaderSafeState();

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

float Machine::rawX() const
{
    return motion_planner_.rawX();
}

float Machine::rawY() const
{
    return motion_planner_.rawY();
}

float Machine::rawZ() const
{
    return motion_planner_.rawZ();
}

float Machine::referenceOffsetX() const
{
    return motion_planner_.referenceOffsetX();
}

float Machine::referenceOffsetY() const
{
    return motion_planner_.referenceOffsetY();
}

float Machine::referenceOffsetZ() const
{
    return motion_planner_.referenceOffsetZ();
}

float Machine::zVelocity() const
{
    return motion_planner_.zVelocity();
}

void Machine::setCurrentPose(float x, float y, float z)
{
    if (machine_state_ == MachineState::Homing)
    {
        return;
    }

    motion_planner_.setCurrentPose(x, y, z);
    stored_target_x_ = x;
    stored_target_y_ = y;
    stored_target_z_ = z;
    commanded_target_x_ = x;
    commanded_target_y_ = y;
    machine_state_ = MachineState::Idle;

    // Absolute snap clears reference offsets — MCS mapping is no longer valid.
    calibration_set_.machine_coordinate_system_valid = false;
    if (is_homed_)
    {
        is_homed_ = false;
    }
}

bool Machine::trySetMachineConfig(const MachineConfig& incoming, ConfigValidationError& error)
{
    if (runtime_config_locked_)
    {
        error = ConfigValidationError::RuntimeLocked;
        return false;
    }
    if (!::tryApplyMachineConfig(machine_config_, incoming, error))
    {
        return false;
    }
    // MachineConfig.calibration.home_offset is the Homing retract distance from +max.
    // CalibrationSet.home_offset is the measured home pose after Homing (Safe-End XY).
    // Never overwrite the measured pose when applying machine config (e.g. CmdSetMachineConfig
    // at procedure start would otherwise park Safe-End at (0,0) / retract defaults).
    calibration_set_.tool_offset = machine_config_.calibration.tool_offset;
    default_max_velocity_ = machine_config_.motion.v_approach_testpoints;
    default_acceleration_ = machine_config_.motion.a_testpoints;
    default_corner_radius_ = machine_config_.motion.corner_testpoints;
    return true;
}

const MachineConfig& Machine::machineConfig() const
{
    return machine_config_;
}

bool Machine::trySetTestConfiguration(const TestConfiguration& incoming, ConfigValidationError& error)
{
    if (runtime_config_locked_)
    {
        error = ConfigValidationError::RuntimeLocked;
        return false;
    }
    return ::tryApplyTestConfiguration(test_configuration_, incoming, error);
}

const TestConfiguration& Machine::testConfiguration() const
{
    return test_configuration_;
}

void Machine::setPlateOrigin(const TestPoint& machine_pose)
{
    if (runtime_config_locked_)
    {
        return;
    }
    ::setPlateOrigin(calibration_set_, machine_pose);
}

void Machine::setProbeCenter(const TestPoint& machine_pose)
{
    ::setProbeCenter(calibration_set_, machine_pose);
}

void Machine::setToolOffset(const TestPoint& offset)
{
    calibration_set_.tool_offset = offset;
    machine_config_.calibration.tool_offset = offset;
}

const CalibrationSet& Machine::calibrationSet() const
{
    return calibration_set_;
}

void Machine::setActiveVelocity(float velocity)
{
    const float v = (velocity > 0.0f) ? velocity : machine_config_.motion.v_approach_testpoints;
    setActiveMotionProfile(v, default_acceleration_, default_corner_radius_);
}

void Machine::setActiveMotionProfile(float velocity, float acceleration, float corner_radius)
{
    const float v = (velocity > 0.0f) ? velocity : machine_config_.motion.v_approach_testpoints;
    default_max_velocity_ = v;
    default_acceleration_ = (acceleration < 0.0f) ? 0.0f : acceleration;
    default_corner_radius_ = (corner_radius < 0.0f) ? 0.0f : corner_radius;
    motion_planner_.setMotionParams(default_max_velocity_, default_acceleration_,
                                    default_corner_radius_);
}

void Machine::brakeToStop(float acceleration)
{
    // Snap commanded goals to the current pose so tickActiveMotion does not
    // re-drive the previous search/traverse target while decelerating.
    stored_target_x_ = motion_planner_.x();
    stored_target_y_ = motion_planner_.y();
    stored_target_z_ = motion_planner_.z();
    commanded_target_x_ = stored_target_x_;
    commanded_target_y_ = stored_target_y_;
    motion_planner_.brakeToStop(acceleration);
    if (machine_state_ != MachineState::Fault && machine_state_ != MachineState::Homing)
    {
        machine_state_ = MachineState::Active;
    }
}

void Machine::setPreClampXyEnabled(bool enabled)
{
    pre_clamp_xy_enabled_ = enabled;
    if (enabled)
    {
        xy_correction_steps_ = 0;
    }
    else
    {
        x_force_correction_active_ = false;
        y_force_correction_active_ = false;
        force_params_.fx_threshold_on_N = 100000.0f;
        force_params_.fx_threshold_off_N = 50000.0f;
        force_params_.fy_threshold_on_N = 100000.0f;
        force_params_.fy_threshold_off_N = 50000.0f;
    }
}

bool Machine::preClampXyEnabled() const
{
    return pre_clamp_xy_enabled_;
}

void Machine::loadPreClampForceParams()
{
    force_params_.fx_threshold_on_N = machine_config_.force.xy_tolerance_N;
    force_params_.fx_threshold_off_N = machine_config_.force.xy_tolerance_N * 0.5f;
    force_params_.fy_threshold_on_N = machine_config_.force.xy_tolerance_N;
    force_params_.fy_threshold_off_N = machine_config_.force.xy_tolerance_N * 0.5f;
    force_params_.dx_step_units = machine_config_.force.xy_correction_step;
    force_params_.dy_step_units = machine_config_.force.xy_correction_step;
    force_params_.max_x_correction_units = machine_config_.force.xy_max_correction;
    force_params_.max_y_correction_units = machine_config_.force.xy_max_correction;
}

unsigned Machine::xyCorrectionSteps() const
{
    return xy_correction_steps_;
}

void Machine::setCollisionContext(CollisionContext context)
{
    collision_context_ = context;
}

CollisionContext Machine::collisionContext() const
{
    return collision_context_;
}

ForceWatchdogTrip Machine::lastForceWatchdogTrip() const
{
    return last_force_watchdog_trip_;
}

FaultReason Machine::lastFaultReason() const
{
    return last_fault_reason_;
}

void Machine::latchFaultReason(FaultReason reason)
{
    if (machine_state_ == MachineState::Fault)
    {
        return;
    }
    last_fault_reason_ = reason;
}

float Machine::forceSampleAge() const
{
    return force_sample_age_s_;
}

bool Machine::spreaderBusy() const
{
    return spreader_timer_.isBusy();
}

SpreaderState Machine::spreaderState() const
{
    return spreader_timer_.state();
}

void Machine::setCommunicationWatchdogEnabled(bool enabled)
{
    comm_watchdog_enabled_ = enabled;
    if (enabled)
    {
        heartbeat_age_s_ = 0.0f;
        heartbeat_fresh_ = have_heartbeat_;
    }
}

bool Machine::communicationWatchdogEnabled() const
{
    return comm_watchdog_enabled_;
}

float Machine::hostHeartbeatAge() const
{
    return heartbeat_age_s_;
}

void Machine::setRuntimeConfigLocked(bool locked)
{
    runtime_config_locked_ = locked;
}

bool Machine::runtimeConfigLocked() const
{
    return runtime_config_locked_;
}

bool Machine::consumeProbeTrigger()
{
    TestPoint unused{};
    return consumeProbeTrigger(unused);
}

bool Machine::consumeProbeTrigger(TestPoint& pose_out)
{
    if (!probe_trigger_latched_)
    {
        return false;
    }
    probe_trigger_latched_ = false;
    pose_out = probe_trigger_pose_;
    return true;
}
