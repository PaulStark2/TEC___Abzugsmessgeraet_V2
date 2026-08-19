#include "ProcedureController.hpp"

#include <cmath>

ProcedureController::ProcedureController(Machine& machine)
    : machine_(machine)
    , program_()
    , process_state_(ProcessState::Idle)
    , phase_(ProcedurePhase::Idle)
    , current_index_(0)
    , insertion_z_(0.0f)
    , extraction_z_(0.0f)
    , point_x_(0.0f)
    , point_y_(0.0f)
    , optimize_attempts_(0)
    , cycle_iteration_(0)
    , cycle_leg_(0)
    , approach_leg_(0)
    , target_machine_{}
    , end_x_(0.0f)
    , end_y_(0.0f)
    , preclamp_elapsed_s_(0.0f)
    , phase2_last_error_sign_(0)
    , phase2_sign_flips_(0)
    , probe_calib_standalone_(false)
{
}

void ProcedureController::setProgram(const TestProgram& program)
{
    program_ = program;
}

bool ProcedureController::pauseAllowed() const
{
    switch (phase_)
    {
    case ProcedurePhase::Clamp:
    case ProcedurePhase::InsertInitial:
    case ProcedurePhase::OptimizeInsert:
    case ProcedurePhase::TestOrAge:
    case ProcedurePhase::Unclamp:
        return false;
    default:
        return true;
    }
}

bool ProcedureController::start()
{
    if (process_state_ == ProcessState::Running)
    {
        return false;
    }
    if (program_.size() == 0)
    {
        return false;
    }
    if (!machine_.calibrationSet().plate_origin_valid)
    {
        return false;
    }
    if (!machine_.isHomed())
    {
        return false;
    }
    if (!programPointsBelowSafeZ())
    {
        return false;
    }
    current_index_ = machine_.testConfiguration().active_point_index;
    if (current_index_ >= program_.size())
    {
        current_index_ = 0;
    }
    process_state_ = ProcessState::Running;
    setAutomaticProcessGuards(true);
    probe_calib_standalone_ = false;
    enterPhase(ProcedurePhase::ProbeApproach);
    if (phase_ == ProcedurePhase::ProbeApproach && motionSettled())
    {
        handleProbeApproach();
    }
    return true;
}

bool ProcedureController::startProbeCalibration()
{
    if (process_state_ == ProcessState::Running)
    {
        return false;
    }
    if (!machine_.isHomed())
    {
        return false;
    }
    if (!probe_calib_.start(machine_))
    {
        return false;
    }
    process_state_ = ProcessState::Running;
    setAutomaticProcessGuards(true);
    probe_calib_standalone_ = true;
    phase_ = ProcedurePhase::ProbeCalibrate;
    return true;
}

void ProcedureController::tick(float dt)
{
    if (process_state_ != ProcessState::Running)
    {
        return;
    }
    machine_.tick(dt);
    if (machine_.machineState() == MachineState::Stopped ||
        machine_.machineState() == MachineState::Fault)
    {
        abortProcess();
        return;
    }
    if (machine_.machineState() == MachineState::Paused)
    {
        return;
    }
    switch (phase_)
    {
    case ProcedurePhase::MoveSafePose:
        handleMoveSafePose();
        break;
    case ProcedurePhase::ProbeApproach:
        handleProbeApproach();
        break;
    case ProcedurePhase::PointApproach:
        handlePointApproach();
        break;
    case ProcedurePhase::PreClampXyCorrect:
        handlePreClamp(dt);
        break;
    case ProcedurePhase::Clamp:
        handleClamp();
        break;
    case ProcedurePhase::InsertInitial:
        handleInsertInitial();
        break;
    case ProcedurePhase::OptimizeInsert:
        handleOptimizeInsert();
        break;
    case ProcedurePhase::TestOrAge:
        handleTestOrAge();
        break;
    case ProcedurePhase::ProbeCalibrate:
        handleProbeCalibrate(dt);
        break;
    case ProcedurePhase::Unclamp:
        handleUnclamp();
        break;
    case ProcedurePhase::NextPoint:
        handleNextPoint();
        break;
    case ProcedurePhase::MoveSafeEnd:
        handleMoveSafeEnd();
        break;
    default:
        break;
    }
}

bool ProcedureController::pause()
{
    if (process_state_ != ProcessState::Running || !pauseAllowed())
    {
        return false;
    }
    machine_.pause();
    return machine_.machineState() == MachineState::Paused;
}

void ProcedureController::resume()
{
    if (process_state_ != ProcessState::Running)
    {
        return;
    }
    machine_.resume();
}

void ProcedureController::stop()
{
    if (process_state_ != ProcessState::Running)
    {
        return;
    }
    machine_.stop();
    abortProcess();
}

ProcessState ProcedureController::processState() const
{
    return process_state_;
}

ProcedurePhase ProcedureController::phase() const
{
    return phase_;
}

std::size_t ProcedureController::currentIndex() const
{
    return current_index_;
}

bool ProcedureController::currentPoint(TestPoint& out) const
{
    if (process_state_ == ProcessState::Idle)
    {
        return false;
    }
    return program_.pointAt(current_index_, out);
}

float ProcedureController::insertionZ() const
{
    return insertion_z_;
}

float ProcedureController::extractionZ() const
{
    return extraction_z_;
}

ForcePathRecorder& ProcedureController::forcePath()
{
    return force_path_;
}

const ForcePathRecorder& ProcedureController::forcePath() const
{
    return force_path_;
}

unsigned ProcedureController::testIteration() const
{
    return cycle_iteration_;
}

ProbeCalibration& ProcedureController::probeCalibration()
{
    return probe_calib_;
}

bool ProcedureController::motionSettled() const
{
    return machine_.motionState() == MotionState::AtTarget ||
           machine_.machineState() == MachineState::Idle;
}

bool ProcedureController::zMinBlocked() const
{
    return (machine_.blockedDirections() & (1u << kLimitDirZMin)) != 0;
}

void ProcedureController::enterPhase(ProcedurePhase phase)
{
    phase_ = phase;
    switch (phase)
    {
    case ProcedurePhase::MoveSafePose:
        machine_.setCollisionContext(CollisionContext::Approach);
        machine_.setActiveVelocity(machine_.machineConfig().motion.v_approach);
        approach_leg_ = 0;
        commandRaiseSafeZ();
        break;
    case ProcedurePhase::ProbeApproach:
        machine_.setCollisionContext(CollisionContext::Approach);
        machine_.setActiveVelocity(machine_.machineConfig().motion.v_approach);
        approach_leg_ = 0;
        commandRaiseSafeZ();
        break;
    case ProcedurePhase::MoveSafeEnd:
        machine_.setCollisionContext(CollisionContext::Approach);
        machine_.setActiveVelocity(machine_.machineConfig().motion.v_approach);
        approach_leg_ = 0;
        end_x_ = machine_.calibrationSet().home_offset.x;
        end_y_ = machine_.calibrationSet().home_offset.y;
        commandRaiseSafeZ();
        break;
    case ProcedurePhase::PointApproach:
        machine_.setPreClampXyEnabled(false);
        machine_.setCollisionContext(CollisionContext::Approach);
        machine_.setActiveVelocity(machine_.machineConfig().motion.v_approach);
        if (!resolveCurrentMachinePoint())
        {
            abortProcess();
            return;
        }
        approach_leg_ = 0;
        commandRaiseSafeZ();
        break;
    case ProcedurePhase::PreClampXyCorrect:
        machine_.setCollisionContext(CollisionContext::Contact);
        machine_.setActiveVelocity(machine_.machineConfig().motion.v_slow);
        preclamp_elapsed_s_ = 0.0f;
        machine_.loadPreClampForceParams();
        machine_.setPreClampXyEnabled(true);
        machine_.moveTo(machine_.x(), machine_.y(), machine_.z());
        break;
    case ProcedurePhase::Clamp:
        machine_.setPreClampXyEnabled(false);
        machine_.setCollisionContext(CollisionContext::Clamp);
        machine_.setSpreaderExtend(true);
        break;
    case ProcedurePhase::InsertInitial:
        machine_.setCollisionContext(CollisionContext::Contact);
        machine_.setActiveVelocity(machine_.machineConfig().motion.v_measure);
        point_x_ = machine_.x();
        point_y_ = machine_.y();
        machine_.moveTo(point_x_, point_y_,
                        machine_.z() - machine_.machineConfig().force.insert_search_distance);
        break;
    case ProcedurePhase::OptimizeInsert:
        optimize_attempts_ = 0;
        cycle_leg_ = 0;
        phase2_last_error_sign_ = 0;
        phase2_sign_flips_ = 0;
        machine_.setCollisionContext(CollisionContext::Contact);
        machine_.setActiveVelocity(machine_.machineConfig().motion.v_approach);
        machine_.moveTo(point_x_, point_y_, insertion_z_);
        break;
    case ProcedurePhase::TestOrAge:
        cycle_iteration_ = 0;
        cycle_leg_ = 0;
        force_path_.reset();
        force_path_.setIteration(0);
        machine_.setCollisionContext(CollisionContext::Measure);
        machine_.setActiveVelocity(machine_.machineConfig().motion.v_approach);
        machine_.moveTo(point_x_, point_y_, insertion_z_);
        break;
    case ProcedurePhase::Unclamp:
        machine_.setCollisionContext(CollisionContext::Unclamp);
        machine_.setSpreaderExtend(false);
        break;
    case ProcedurePhase::Completed:
        setAutomaticProcessGuards(false);
        process_state_ = ProcessState::Completed;
        break;
    case ProcedurePhase::Aborted:
        process_state_ = ProcessState::Aborted;
        break;
    default:
        break;
    }
}

bool ProcedureController::atSafeZ() const
{
    const float safe_z = machine_.machineConfig().motion.safe_z;
    return std::fabs(machine_.z() - safe_z) <= 1.0e-3f;
}

bool ProcedureController::resolveCurrentMachinePoint()
{
    TestPoint plate{};
    if (!program_.pointAt(current_index_, plate))
    {
        return false;
    }
    return toMachinePoint(machine_.calibrationSet(), plate, target_machine_);
}

bool ProcedureController::programPointsBelowSafeZ() const
{
    const float safe_z = machine_.machineConfig().motion.safe_z;
    for (std::size_t i = 0; i < program_.size(); ++i)
    {
        TestPoint plate{};
        if (!program_.pointAt(i, plate))
        {
            return false;
        }
        TestPoint machine_point{};
        if (!toMachinePoint(machine_.calibrationSet(), plate, machine_point))
        {
            return false;
        }
        if (!(machine_point.z < safe_z))
        {
            return false;
        }
    }
    return program_.size() > 0;
}

void ProcedureController::commandRaiseSafeZ()
{
    const float safe_z = machine_.machineConfig().motion.safe_z;
    machine_.moveTo(machine_.x(), machine_.y(), safe_z);
}

void ProcedureController::commandTraverseXY(float x, float y)
{
    const float safe_z = machine_.machineConfig().motion.safe_z;
    machine_.moveTo(x, y, safe_z);
}

void ProcedureController::commandLowerToZ(float z)
{
    machine_.moveTo(machine_.x(), machine_.y(), z);
}

void ProcedureController::setAutomaticProcessGuards(bool active)
{
    machine_.setCommunicationWatchdogEnabled(active);
    machine_.setRuntimeConfigLocked(active);
}

void ProcedureController::abortProcess()
{
    probe_calib_.abort();
    machine_.setPreClampXyEnabled(false);
    setAutomaticProcessGuards(false);
    if (machine_.machineState() != MachineState::Fault)
    {
        machine_.stop();
    }
    phase_ = ProcedurePhase::Aborted;
    process_state_ = ProcessState::Aborted;
}

void ProcedureController::handleMoveSafePose()
{
    if (!motionSettled())
    {
        return;
    }
    if (!atSafeZ())
    {
        abortProcess();
        return;
    }
    enterPhase(ProcedurePhase::PointApproach);
}

void ProcedureController::handlePointApproach()
{
    if (!motionSettled())
    {
        return;
    }
    if (approach_leg_ == 0)
    {
        if (!atSafeZ())
        {
            abortProcess();
            return;
        }
        commandTraverseXY(target_machine_.x, target_machine_.y);
        approach_leg_ = 1;
        return;
    }
    if (approach_leg_ == 1)
    {
        if (!atSafeZ())
        {
            abortProcess();
            return;
        }
        commandLowerToZ(target_machine_.z);
        approach_leg_ = 2;
        return;
    }
    enterPhase(ProcedurePhase::PreClampXyCorrect);
}

void ProcedureController::handleProbeApproach()
{
    if (!motionSettled())
    {
        return;
    }
    const TestPoint nominal = machine_.machineConfig().probe.nominal_probe;
    const float approach_z = nominal.z + machine_.machineConfig().probe.approach_distance;
    if (approach_leg_ == 0)
    {
        if (!atSafeZ())
        {
            abortProcess();
            return;
        }
        commandTraverseXY(nominal.x, nominal.y);
        approach_leg_ = 1;
        return;
    }
    if (approach_leg_ == 1)
    {
        if (!atSafeZ())
        {
            abortProcess();
            return;
        }
        commandLowerToZ(approach_z);
        approach_leg_ = 2;
        return;
    }
    if (!probe_calib_.start(machine_))
    {
        abortProcess();
        return;
    }
    phase_ = ProcedurePhase::ProbeCalibrate;
}

void ProcedureController::handleProbeCalibrate(float dt)
{
    probe_calib_.tick(dt, machine_);
    if (probe_calib_.isComplete())
    {
        if (probe_calib_standalone_)
        {
            machine_.stop();
            setAutomaticProcessGuards(false);
            phase_ = ProcedurePhase::Completed;
            process_state_ = ProcessState::Completed;
        }
        else
        {
            enterPhase(ProcedurePhase::MoveSafePose);
            if (motionSettled())
            {
                handleMoveSafePose();
            }
        }
    }
    else if (probe_calib_.isError())
    {
        abortProcess();
    }
}

void ProcedureController::handlePreClamp(float dt)
{
    const MachineConfig::Force& force_cfg = machine_.machineConfig().force;
    preclamp_elapsed_s_ += dt;
    const float fx = machine_.hostInputs().force.fx;
    const float fy = machine_.hostInputs().force.fy;
    if (std::fabs(fx) <= force_cfg.xy_tolerance_N && std::fabs(fy) <= force_cfg.xy_tolerance_N &&
        motionSettled())
    {
        machine_.setPreClampXyEnabled(false);
        enterPhase(ProcedurePhase::Clamp);
        return;
    }
    if (preclamp_elapsed_s_ >= force_cfg.xy_correct_timeout_s ||
        machine_.xyCorrectionSteps() >= force_cfg.xy_max_attempts)
    {
        abortProcess();
    }
}

void ProcedureController::handleClamp()
{
    if (!machine_.spreaderBusy() && machine_.hostOutputs().spreaderExtended)
    {
        enterPhase(ProcedurePhase::InsertInitial);
    }
}

void ProcedureController::handleInsertInitial()
{
    const float target = machine_.testConfiguration().target_force_N;
    const float fz = machine_.hostInputs().force.fz;
    if (fz >= target)
    {
        insertion_z_ = machine_.z();
        extraction_z_ = insertion_z_ + machine_.machineConfig().force.insertion_buffer;
        enterPhase(ProcedurePhase::OptimizeInsert);
        return;
    }
    if (motionSettled() || zMinBlocked())
    {
        abortProcess();
    }
}

void ProcedureController::handleOptimizeInsert()
{
    if (!motionSettled())
    {
        return;
    }
    if (zMinBlocked())
    {
        abortProcess();
        return;
    }
    if (cycle_leg_ == 0)
    {
        const float fz = machine_.hostInputs().force.fz;
        const float target = machine_.testConfiguration().target_force_N;
        const MachineConfig::Phase2& p2 = machine_.machineConfig().phase2;
        const float error = fz - target;
        if (std::fabs(error) <= p2.force_tolerance_N)
        {
            machine_.moveTo(point_x_, point_y_, extraction_z_);
            cycle_leg_ = 2;
            return;
        }
        const int8_t sign = (error > 0.0f) ? static_cast<int8_t>(1) : static_cast<int8_t>(-1);
        if (phase2_last_error_sign_ != 0 && sign != phase2_last_error_sign_)
        {
            ++phase2_sign_flips_;
        }
        if (sign != 0)
        {
            phase2_last_error_sign_ = sign;
        }
        if (phase2_sign_flips_ > p2.max_sign_flips)
        {
            abortProcess();
            return;
        }
        float dz = p2.z_correction_gain * error;
        if (dz > p2.z_correction_max)
        {
            dz = p2.z_correction_max;
        }
        if (dz < -p2.z_correction_max)
        {
            dz = -p2.z_correction_max;
        }
        if (std::fabs(dz) >= p2.minimum_correction)
        {
            insertion_z_ += dz;
        }
        const float depth_floor =
            target_machine_.z - machine_.machineConfig().force.insert_search_distance;
        if (insertion_z_ < depth_floor)
        {
            abortProcess();
            return;
        }
        ++optimize_attempts_;
        if (optimize_attempts_ >= p2.max_optimization_iterations)
        {
            abortProcess();
            return;
        }
        machine_.moveTo(point_x_, point_y_, extraction_z_);
        cycle_leg_ = 1;
        return;
    }
    if (cycle_leg_ == 2)
    {
        enterPhase(ProcedurePhase::TestOrAge);
        return;
    }
    machine_.setActiveVelocity(machine_.machineConfig().motion.v_approach);
    machine_.moveTo(point_x_, point_y_, insertion_z_);
    cycle_leg_ = 0;
}

void ProcedureController::handleTestOrAge()
{
    const bool record = machine_.testConfiguration().mode == TestMode::Test;
    if (record)
    {
        force_path_.push(machine_.z(), machine_.hostInputs().force.fz);
    }
    if (!motionSettled())
    {
        return;
    }
    if (cycle_leg_ == 0)
    {
        machine_.setActiveVelocity(machine_.machineConfig().motion.v_measure);
        machine_.setCollisionContext(CollisionContext::Retract);
        machine_.moveTo(point_x_, point_y_, extraction_z_);
        cycle_leg_ = 1;
        return;
    }
    ++cycle_iteration_;
    if (cycle_iteration_ >= machine_.testConfiguration().iterations)
    {
        enterPhase(ProcedurePhase::Unclamp);
        return;
    }
    force_path_.setIteration(static_cast<std::uint16_t>(cycle_iteration_));
    machine_.setActiveVelocity(machine_.machineConfig().motion.v_approach);
    machine_.setCollisionContext(CollisionContext::Contact);
    machine_.moveTo(point_x_, point_y_, insertion_z_);
    cycle_leg_ = 0;
}

void ProcedureController::handleUnclamp()
{
    if (!machine_.spreaderBusy() && !machine_.hostOutputs().spreaderExtended)
    {
        enterPhase(ProcedurePhase::NextPoint);
        handleNextPoint();
    }
}

void ProcedureController::handleNextPoint()
{
    if (current_index_ + 1 < program_.size())
    {
        ++current_index_;
        enterPhase(ProcedurePhase::PointApproach);
        return;
    }
    enterPhase(ProcedurePhase::MoveSafeEnd);
}

void ProcedureController::handleMoveSafeEnd()
{
    if (!motionSettled())
    {
        return;
    }
    if (approach_leg_ == 0)
    {
        if (!atSafeZ())
        {
            abortProcess();
            return;
        }
        commandTraverseXY(end_x_, end_y_);
        approach_leg_ = 1;
        return;
    }
    enterPhase(ProcedurePhase::Completed);
}
