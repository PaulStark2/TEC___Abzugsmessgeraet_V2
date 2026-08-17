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
    current_index_ = machine_.testConfiguration().active_point_index;
    if (current_index_ >= program_.size())
    {
        current_index_ = 0;
    }
    process_state_ = ProcessState::Running;
    enterPhase(ProcedurePhase::MoveSafePose);
    if (phase_ == ProcedurePhase::MoveSafePose && motionSettled())
    {
        handleMoveSafePose();
    }
    return true;
}

bool ProcedureController::startProbeCalibration()
{
    if (process_state_ == ProcessState::Running)
    {
        return false;
    }
    if (!probe_calib_.start(machine_))
    {
        return false;
    }
    process_state_ = ProcessState::Running;
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
    case ProcedurePhase::PointApproach:
        handlePointApproach();
        break;
    case ProcedurePhase::PreClampXyCorrect:
        handlePreClamp();
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
        handleProbeCalibrate();
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
    case ProcedurePhase::MoveSafeEnd:
        machine_.setCollisionContext(CollisionContext::Approach);
        machine_.setActiveVelocity(machine_.machineConfig().motion.v_approach);
        commandSafePose();
        break;
    case ProcedurePhase::PointApproach:
        machine_.setPreClampXyEnabled(false);
        machine_.setCollisionContext(CollisionContext::Approach);
        machine_.setActiveVelocity(machine_.machineConfig().motion.v_approach);
        commandCurrentPlatePoint();
        break;
    case ProcedurePhase::PreClampXyCorrect:
        machine_.setCollisionContext(CollisionContext::Contact);
        machine_.setActiveVelocity(machine_.machineConfig().motion.v_slow);
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
                        machine_.z() - machine_.machineConfig().probe.search_distance);
        break;
    case ProcedurePhase::OptimizeInsert:
        optimize_attempts_ = 0;
        cycle_leg_ = 0;
        machine_.setCollisionContext(CollisionContext::Contact);
        machine_.setActiveVelocity(machine_.machineConfig().motion.v_approach);
        machine_.moveTo(point_x_, point_y_, insertion_z_);
        break;
    case ProcedurePhase::TestOrAge:
        cycle_iteration_ = 0;
        cycle_leg_ = 0;
        force_path_.reset();
        machine_.setCollisionContext(CollisionContext::Measure);
        machine_.setActiveVelocity(machine_.machineConfig().motion.v_approach);
        machine_.moveTo(point_x_, point_y_, insertion_z_);
        break;
    case ProcedurePhase::Unclamp:
        machine_.setCollisionContext(CollisionContext::Unclamp);
        machine_.setSpreaderExtend(false);
        break;
    case ProcedurePhase::Completed:
        process_state_ = ProcessState::Completed;
        break;
    case ProcedurePhase::Aborted:
        process_state_ = ProcessState::Aborted;
        break;
    default:
        break;
    }
}

void ProcedureController::commandSafePose()
{
    const TestPoint home = machine_.calibrationSet().home_offset;
    machine_.moveTo(home.x, home.y, home.z);
}

void ProcedureController::commandCurrentPlatePoint()
{
    TestPoint plate{};
    if (!program_.pointAt(current_index_, plate))
    {
        abortProcess();
        return;
    }
    TestPoint machine_point{};
    if (!toMachinePoint(machine_.calibrationSet(), plate, machine_point))
    {
        abortProcess();
        return;
    }
    machine_.moveTo(machine_point.x, machine_point.y, machine_point.z);
}

void ProcedureController::abortProcess()
{
    probe_calib_.abort();
    machine_.setPreClampXyEnabled(false);
    phase_ = ProcedurePhase::Aborted;
    process_state_ = ProcessState::Aborted;
}

void ProcedureController::handleMoveSafePose()
{
    if (motionSettled())
    {
        enterPhase(ProcedurePhase::PointApproach);
    }
}

void ProcedureController::handleProbeCalibrate()
{
    probe_calib_.tick(0.0f, machine_);
    if (probe_calib_.isComplete())
    {
        phase_ = ProcedurePhase::Completed;
        process_state_ = ProcessState::Completed;
    }
    else if (probe_calib_.isError())
    {
        abortProcess();
    }
}

void ProcedureController::handlePointApproach()
{
    if (motionSettled())
    {
        enterPhase(ProcedurePhase::PreClampXyCorrect);
    }
}

void ProcedureController::handlePreClamp()
{
    const float tol = machine_.machineConfig().force.xy_tolerance_N;
    const float fx = machine_.hostInputs().force.fx;
    const float fy = machine_.hostInputs().force.fy;
    if (std::fabs(fx) <= tol && std::fabs(fy) <= tol && motionSettled())
    {
        machine_.setPreClampXyEnabled(false);
        enterPhase(ProcedurePhase::Clamp);
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
    if (machine_.hostInputs().force.fz >= target || motionSettled() || zMinBlocked())
    {
        insertion_z_ = machine_.z();
        extraction_z_ = insertion_z_ + machine_.machineConfig().force.insertion_buffer;
        enterPhase(ProcedurePhase::OptimizeInsert);
    }
}

void ProcedureController::handleOptimizeInsert()
{
    if (!motionSettled())
    {
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
    if (motionSettled())
    {
        enterPhase(ProcedurePhase::Completed);
    }
}
