#include "ProcedureController.hpp"

#include "SafeZ.hpp"

#include <cstdio>
#include <cmath>

ProcedureController::ProcedureController(Machine& machine)
    : machine_(machine)
    , program_()
    , process_state_(ProcessState::Idle)
    , phase_(ProcedurePhase::Idle)
    , current_index_(0)
    , insertion_z_(0.0f)
    , extraction_z_(0.0f)
    , force_free_z_(0.0f)
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
    , phase2_measured_fz_(0.0f)
    , phase2_motion_fz_(0.0f)
    , probe_calib_standalone_(false)
    , tare_next_(ProcedurePhase::Idle)
    , fine_approach_after_tare_(false)
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
    case ProcedurePhase::ForceTare:
        return false;
    default:
        return true;
    }
}

bool ProcedureController::start()
{
    const CalibrationSet& calib = machine_.calibrationSet();
    float safe_z_machine = 0.0f;
    const bool safe_z_ok = resolveSafeZMachine(safe_z_machine);

    std::fprintf(stderr,
                 "[ProcedureStart] machineState=%u homingState=%u isHomed=%d "
                 "rawPose=(%.3f,%.3f,%.3f) machinePose=(%.3f,%.3f,%.3f) "
                 "referenceOffset=(%.3f,%.3f,%.3f) selection=%u "
                 "testConfiguration(mode=%u,F=%.3f,iters=%u) procedureState=%u "
                 "plate_valid=%d probe_valid=%d program_points=%zu safe_z_machine=%.3f\n",
                 static_cast<unsigned>(machine_.machineState()),
                 static_cast<unsigned>(machine_.homingState()), machine_.isHomed() ? 1 : 0,
                 machine_.rawX(), machine_.rawY(), machine_.rawZ(), machine_.x(), machine_.y(),
                 machine_.z(), machine_.referenceOffsetX(), machine_.referenceOffsetY(),
                 machine_.referenceOffsetZ(),
                 static_cast<unsigned>(machine_.testConfiguration().active_point_index),
                 static_cast<unsigned>(machine_.testConfiguration().mode),
                 machine_.testConfiguration().target_force_N,
                 machine_.testConfiguration().iterations, static_cast<unsigned>(process_state_),
                 calib.plate_origin_valid ? 1 : 0, calib.probe_center_valid ? 1 : 0, program_.size(),
                 safe_z_ok ? safe_z_machine : 0.0f);

    if (process_state_ == ProcessState::Running)
    {
        std::fprintf(stderr, "[ProcedureStart] REJECTED: process already Running\n");
        return false;
    }
    if (program_.size() == 0)
    {
        std::fprintf(stderr, "[ProcedureStart] REJECTED: program empty\n");
        return false;
    }
    if (!calib.plate_origin_valid)
    {
        std::fprintf(stderr, "[ProcedureStart] REJECTED: plate_origin_valid=false\n");
        return false;
    }
    if (!calib.probe_center_valid)
    {
        std::fprintf(stderr, "[ProcedureStart] REJECTED: probe_center_valid=false\n");
        return false;
    }
    if (!machine_.isHomed())
    {
        std::fprintf(stderr, "[ProcedureStart] REJECTED: isHomed=false (at-home required)\n");
        return false;
    }
    if (!programPointsBelowSafeZ())
    {
        float max_z = 0.0f;
        float safe_z_plate = 0.0f;
        float safe_z = 0.0f;
        const float buffer = machine_.machineConfig().motion.safe_z_buffer;
        const bool plate_ok = computeSafeZPlate(program_, buffer, max_z, safe_z_plate);
        const bool machine_ok =
            plate_ok && computeSafeZMachine(machine_.calibrationSet(), safe_z_plate, safe_z);
        std::fprintf(stderr,
                     "[ProcedureStart] REJECTED: programPointsBelowSafeZ | "
                     "program_points=%zu safe_z_buffer=%.3f max_program_z=%.3f safe_z_plate=%.3f "
                     "safe_z_machine=%.3f plate_ok=%d machine_ok=%d plate_origin.z=%.3f "
                     "tool_offset.z=%.3f pose.z=%.3f\n",
                     program_.size(), buffer, max_z, safe_z_plate, safe_z, plate_ok ? 1 : 0,
                     machine_ok ? 1 : 0, calib.plate_origin.z, calib.tool_offset.z, machine_.z());
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
    fine_approach_after_tare_ = false;
    enterPhase(ProcedurePhase::ProbeApproach);
    if (phase_ == ProcedurePhase::ProbeApproach && motionSettled())
    {
        handleProbeApproach();
    }
    if (process_state_ == ProcessState::Aborted)
    {
        std::fprintf(stderr, "[ProcedureStart] REJECTED: aborted during enterPhase/ProbeApproach\n");
        return false;
    }
    std::fprintf(stderr, "[ProcedureStart] ACCEPTED phase=%u\n", static_cast<unsigned>(phase_));
    return true;
}

bool ProcedureController::startProbeCalibration()
{
    if (process_state_ == ProcessState::Running)
    {
        return false;
    }
    // M19: need a valid home reference (HomingState::Homed), but the TCP is
    // normally jogged near the probe first — so isHomed() (at home pose) is
    // intentionally not required here. Procedure start still requires isHomed().
    if (machine_.homingState() != HomingState::Homed)
    {
        return false;
    }
    // Manual start: operator jogs TCP to the hub above the disc. That pose is the
    // only location reference; no assumed disc center is derived from it.
    const TestPoint hub{machine_.x(), machine_.y(), machine_.z()};
    // CmdStartProbeCalib / FullCalib / Geometry → Install (set probeCenter, toolOffset=0).
    if (!probe_calib_.start(machine_, hub, ProbeCalibration::StorePolicy::Install))
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

    // Pose(t) then Force(t) then watchdog, then phase logic on the same sample.
    machine_.advanceKinematics(dt);
    machine_.invokeHostInputUpdateCallback();
    machine_.evaluateHostSafety(dt);

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
    case ProcedurePhase::ForceTare:
        handleForceTare();
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

float ProcedureController::forceFreeZ() const
{
    return force_free_z_;
}

unsigned ProcedureController::cycleLeg() const
{
    return cycle_leg_;
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
    const MachineConfig::Motion& motion = machine_.machineConfig().motion;
    const MachineConfig::Probe& probe = machine_.machineConfig().probe;
    switch (phase)
    {
    case ProcedurePhase::MoveSafePose:
        machine_.setCollisionContext(CollisionContext::Approach);
        machine_.setActiveMotionProfile(motion.v_approach_testpoints, motion.a_testpoints,
                                        motion.corner_testpoints);
        approach_leg_ = 0;
        commandRaiseSafeZ();
        break;
    case ProcedurePhase::ProbeApproach:
        machine_.setCollisionContext(CollisionContext::Approach);
        machine_.setActiveMotionProfile(probe.v_approach_probe, probe.a_probe_transfer,
                                        probe.corner_probe_transfer);
        approach_leg_ = 0;
        commandRaiseSafeZ();
        break;
    case ProcedurePhase::MoveSafeEnd:
        machine_.setCollisionContext(CollisionContext::Approach);
        machine_.setActiveMotionProfile(motion.v_approach_testpoints, motion.a_testpoints,
                                        motion.corner_testpoints);
        approach_leg_ = 0;
        std::fprintf(stderr,
                     "[MoveSafeEnd] raise Safe-Z then Machine::home() (not park measured home)\n");
        commandRaiseSafeZ();
        break;
    case ProcedurePhase::PointApproach:
        machine_.setPreClampXyEnabled(false);
        machine_.setCollisionContext(CollisionContext::Approach);
        machine_.setActiveMotionProfile(motion.v_approach_testpoints, motion.a_testpoints,
                                        motion.corner_testpoints);
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
        // Fine XY corrections; corner applies to any multi-axis blend.
        machine_.setActiveMotionProfile(motion.v_fine_testpoints, motion.a_testpoints,
                                        motion.corner_testpoints);
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
        machine_.setActiveMotionProfile(motion.phase1_v_extract, motion.phase1_a,
                                        motion.phase1_corner);
        point_x_ = machine_.x();
        point_y_ = machine_.y();
        // Phase 1 must start with extraction (+Z) immediately after spreader open.
        insertion_z_ = machine_.z();
        extraction_z_ = machine_.z();
        force_free_z_ = machine_.z();
        cycle_leg_ = 0;
        {
            const float search = machine_.machineConfig().force.insert_search_distance;
            std::fprintf(stderr,
                         "[InsertInitial] start OUT +Z from z=%.3f fz=%.3f target=%.3f search=%.3f "
                         "v_extract=%.3f\n",
                         machine_.z(), machine_.hostInputs().force.fz,
                         machine_.testConfiguration().target_force_N, search,
                         motion.phase1_v_extract);
            machine_.moveTo(point_x_, point_y_, machine_.z() + search);
        }
        break;
    case ProcedurePhase::OptimizeInsert:
        optimize_attempts_ = 0;
        cycle_leg_ = 0;
        phase2_last_error_sign_ = 0;
        phase2_sign_flips_ = 0;
        phase2_measured_fz_ = 0.0f;
        phase2_motion_fz_ = 0.0f;
        machine_.setCollisionContext(CollisionContext::Contact);
        // Phase 1 ends at extraction_z; Phase 2 starts there and inserts to insertion_z.
        machine_.setActiveMotionProfile(motion.phase2_v_insert, motion.phase2_a,
                                        motion.phase2_corner);
        std::fprintf(stderr,
                     "[OptimizeInsert] Phase2 start at extraction_z=%.3f → insert to insertion_z=%.3f "
                     "v_insert=%.3f\n",
                     extraction_z_, insertion_z_, motion.phase2_v_insert);
        machine_.moveTo(point_x_, point_y_, insertion_z_);
        break;
    case ProcedurePhase::TestOrAge:
        cycle_iteration_ = 0;
        cycle_leg_ = 0;
        force_path_.reset();
        force_path_.setIteration(0);
        machine_.setCollisionContext(CollisionContext::Measure);
        machine_.setActiveMotionProfile(motion.phase3_v_insert, motion.phase3_a,
                                        motion.phase3_corner);
        machine_.moveTo(point_x_, point_y_, insertion_z_);
        break;
    case ProcedurePhase::Unclamp:
        machine_.setPreClampXyEnabled(false);
        machine_.setCollisionContext(CollisionContext::Unclamp);
        cycle_leg_ = 0;
        machine_.setActiveMotionProfile(motion.v_unclamp, motion.a_testpoints,
                                        motion.corner_testpoints);
        machine_.moveTo(point_x_, point_y_, insertion_z_);
        break;
    case ProcedurePhase::ForceTare:
        if (!machine_.beginForceTare())
        {
            abortProcess("force tare busy");
        }
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
    if (phase_ == ProcedurePhase::InsertInitial || phase_ == ProcedurePhase::OptimizeInsert ||
        phase_ == ProcedurePhase::TestOrAge)
    {
        machine_.invokeHostInputUpdateCallback();
    }
    std::fprintf(stderr, "[enterPhase] %u cycle_leg=%u z=%.3f fz=%.3f\n",
                 static_cast<unsigned>(phase_), static_cast<unsigned>(cycle_leg_),
                 machine_.z(), machine_.hostInputs().force.fz);
}

bool ProcedureController::atSafeZ() const
{
    float safe_z_machine = 0.0f;
    if (!resolveSafeZMachine(safe_z_machine))
    {
        return false;
    }
    return std::fabs(machine_.z() - safe_z_machine) <= 1.0e-3f;
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

bool ProcedureController::resolveSafeZMachine(float& out_safe_z_machine) const
{
    return resolveProgramSafeZMachine(program_, machine_.machineConfig().motion.safe_z_buffer,
                                      machine_.calibrationSet(), out_safe_z_machine);
}

bool ProcedureController::programPointsBelowSafeZ() const
{
    float safe_z_machine = 0.0f;
    if (!resolveSafeZMachine(safe_z_machine))
    {
        return false;
    }
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
        if (!(machine_point.z < safe_z_machine))
        {
            return false;
        }
    }
    return program_.size() > 0;
}

void ProcedureController::commandRaiseSafeZ()
{
    float safe_z_machine = 0.0f;
    if (!resolveSafeZMachine(safe_z_machine))
    {
        return;
    }
    machine_.moveTo(machine_.x(), machine_.y(), safe_z_machine);
}

void ProcedureController::commandTraverseXY(float x, float y)
{
    float safe_z_machine = 0.0f;
    if (!resolveSafeZMachine(safe_z_machine))
    {
        return;
    }
    machine_.moveTo(x, y, safe_z_machine);
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

void ProcedureController::abortProcess(const char* reason)
{
    probe_calib_.abort();
    machine_.setPreClampXyEnabled(false);
    fine_approach_after_tare_ = false;
    setAutomaticProcessGuards(false);
    if (machine_.machineState() != MachineState::Fault)
    {
        machine_.stop();
    }
    // Soft-STOP / abort of an automatic run requires Homing before the next start.
    machine_.invalidateHoming();
    phase_ = ProcedurePhase::Aborted;
    process_state_ = ProcessState::Aborted;
    std::fprintf(stderr,
                 "[ProcedureAbort] reason=%s | isHomed cleared (NeedsHoming) fault=%u "
                 "watchdogTrip=%u lastFz=%.3f pose=(%.3f,%.3f,%.3f) raw=(%.3f,%.3f,%.3f)\n",
                 reason != nullptr ? reason : "unspecified",
                 static_cast<unsigned>(machine_.lastFaultReason()),
                 static_cast<unsigned>(machine_.lastForceWatchdogTrip()),
                 machine_.hostInputs().force.fz, machine_.x(), machine_.y(), machine_.z(),
                 machine_.rawX(), machine_.rawY(), machine_.rawZ());
}

void ProcedureController::startForceTare(ProcedurePhase next)
{
    fine_approach_after_tare_ = false;
    tare_next_ = next;
    enterPhase(ProcedurePhase::ForceTare);
}

void ProcedureController::startForceTareBeforeFineApproach()
{
    fine_approach_after_tare_ = true;
    tare_next_ = ProcedurePhase::Idle;
    enterPhase(ProcedurePhase::ForceTare);
}

void ProcedureController::commandFineApproach()
{
    const MachineConfig::Motion& motion = machine_.machineConfig().motion;
    phase_ = ProcedurePhase::PointApproach;
    machine_.setActiveMotionProfile(motion.v_fine_testpoints, motion.a_testpoints,
                                    motion.corner_testpoints);
    machine_.setCollisionContext(CollisionContext::Contact);
    std::fprintf(stderr,
                 "[PointApproach] FINE z=%.3f v_fine_testpoints=%.3f pose=(%.3f,%.3f,%.3f) "
                 "fz=%.3f\n",
                 target_machine_.z, motion.v_fine_testpoints, machine_.x(), machine_.y(),
                 machine_.z(), machine_.hostInputs().force.fz);
    commandLowerToZ(target_machine_.z);
    approach_leg_ = 3;
}

void ProcedureController::handleForceTare()
{
    if (machine_.forceTareActive())
    {
        return;
    }
    if (fine_approach_after_tare_)
    {
        fine_approach_after_tare_ = false;
        commandFineApproach();
        return;
    }
    enterPhase(tare_next_);
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
    const MachineConfig::Motion& motion = machine_.machineConfig().motion;
    const float standoff = machine_.machineConfig().probe.approach_distance;
    const float coarse_z = target_machine_.z + standoff;

    // Fine approach may end early on contact force (real HW); sim usually reaches Z.
    // The rest tare already ran at the coarse standoff, so PreClamp sees this force.
    if (approach_leg_ == 3)
    {
        const float fz = machine_.hostInputs().force.fz;
        const float target = machine_.testConfiguration().target_force_N;
        if (fz >= target)
        {
            enterPhase(ProcedurePhase::PreClampXyCorrect);
            return;
        }
    }

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
        machine_.setActiveMotionProfile(motion.v_approach_testpoints, motion.a_testpoints,
                                        motion.corner_testpoints);
        machine_.setCollisionContext(CollisionContext::Approach);
        std::fprintf(stderr,
                     "[PointApproach] XY v_approach_testpoints=%.3f → (%.3f,%.3f) @ Safe-Z\n",
                     motion.v_approach_testpoints, target_machine_.x, target_machine_.y);
        commandTraverseXY(target_machine_.x, target_machine_.y);
        approach_leg_ = 1;
        return;
    }
    if (approach_leg_ == 1)
    {
        // Do not descend if XY did not reach the programmed target (soft-limit hold).
        const float xy_tol = 0.05f;
        if (std::fabs(machine_.x() - target_machine_.x) > xy_tol ||
            std::fabs(machine_.y() - target_machine_.y) > xy_tol)
        {
            abortProcess("PointApproach: XY not at programmed target before Z descent");
            return;
        }
        if (!atSafeZ())
        {
            abortProcess();
            return;
        }
        machine_.setActiveMotionProfile(motion.v_approach_testpoints, motion.a_testpoints,
                                        motion.corner_testpoints);
        machine_.setCollisionContext(CollisionContext::Approach);
        std::fprintf(stderr,
                     "[PointApproach] COARSE z=%.3f (target=%.3f + standoff=%.3f) "
                     "v_approach_testpoints=%.3f pose=(%.3f,%.3f,%.3f)\n",
                     coarse_z, target_machine_.z, standoff, motion.v_approach_testpoints,
                     machine_.x(), machine_.y(), machine_.z());
        commandLowerToZ(coarse_z);
        approach_leg_ = 2;
        return;
    }
    if (approach_leg_ == 2)
    {
        // Standstill above the point. Tare here so the fine descent and the
        // following XY correction both see force relative to this rest value.
        startForceTareBeforeFineApproach();
        return;
    }
    enterPhase(ProcedurePhase::PreClampXyCorrect);
}

void ProcedureController::handleProbeApproach()
{
    const CalibrationSet& calib = machine_.calibrationSet();
    const MachineConfig::Probe& probe = machine_.machineConfig().probe;
    const TestPoint target = calib.probe_center;
    const float standoff = probe.approach_distance;
    const float coarse_z = target.z + standoff;

    // Leg 2: blind coarse Z toward hub. Abort immediately on Messtaster contact —
    // typically stored probe_center.z too low vs. the real disc.
    if (approach_leg_ == 2)
    {
        TestPoint contact{};
        if (machine_.consumeProbeTrigger(contact))
        {
            std::fprintf(stderr,
                         "[ProbeApproach] CONTACT during coarse Z at (%.3f,%.3f,%.3f) "
                         "target_hub_z=%.3f probe_center.z=%.3f — abort\n",
                         contact.x, contact.y, contact.z, coarse_z, target.z);
            machine_.brakeToStop(probe.a_probe_brake);
            machine_.latchFaultReason(FaultReason::ProbeApproachContact);
            abortProcess("ProbeApproach: probe contact during coarse Z approach");
            return;
        }
        if (!motionSettled())
        {
            return;
        }
        // Hub reached without early contact — start M19 OffsetOnly from current TCP.
        machine_.consumeProbeTrigger();
        std::fprintf(stderr,
                     "[ProbeApproach] start M19 at hub pose=(%.3f,%.3f,%.3f) "
                     "v_search=%.3f\n",
                     machine_.x(), machine_.y(), machine_.z(), probe.v_search);
        const TestPoint hub{machine_.x(), machine_.y(), machine_.z()};
        if (!probe_calib_.start(machine_, hub, ProbeCalibration::StorePolicy::OffsetOnly))
        {
            abortProcess();
            return;
        }
        phase_ = ProcedurePhase::ProbeCalibrate;
        return;
    }

    if (!motionSettled())
    {
        return;
    }
    if (approach_leg_ == 0)
    {
        if (!atSafeZ())
        {
            abortProcess("ProbeApproach leg0: Raise Safe-Z did not reach (soft-limit or blocked)");
            return;
        }
        machine_.setActiveMotionProfile(probe.v_approach_probe, probe.a_probe_transfer,
                                        probe.corner_probe_transfer);
        machine_.setCollisionContext(CollisionContext::Approach);
        std::fprintf(stderr,
                     "[ProbeApproach] XY v_approach_probe=%.3f → (%.3f,%.3f) @ Safe-Z\n",
                     probe.v_approach_probe, target.x, target.y);
        commandTraverseXY(target.x, target.y);
        approach_leg_ = 1;
        return;
    }
    if (approach_leg_ == 1)
    {
        const float xy_tol = 0.05f;
        if (std::fabs(machine_.x() - target.x) > xy_tol ||
            std::fabs(machine_.y() - target.y) > xy_tol)
        {
            abortProcess("ProbeApproach: XY not at probe center before Z descent");
            return;
        }
        if (!atSafeZ())
        {
            abortProcess("ProbeApproach leg1: left Safe-Z before coarse Z");
            return;
        }
        // Discard stale latch from jog / prior search before the open-loop descent.
        machine_.consumeProbeTrigger();
        machine_.setActiveMotionProfile(probe.v_approach_probe, probe.a_probe_transfer,
                                        probe.corner_probe_transfer);
        machine_.setCollisionContext(CollisionContext::Approach);
        std::fprintf(stderr,
                     "[ProbeApproach] COARSE z=%.3f (probe.z=%.3f + standoff=%.3f) "
                     "v_approach_probe=%.3f pose=(%.3f,%.3f,%.3f)\n",
                     coarse_z, target.z, standoff, probe.v_approach_probe, machine_.x(),
                     machine_.y(), machine_.z());
        commandLowerToZ(coarse_z);
        approach_leg_ = 2;
        return;
    }
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
        if (probe_calib_.searchErrorReason() == ProbeSearch::ErrorReason::NoContact)
        {
            machine_.latchFaultReason(FaultReason::ProbeSearchNoContact);
            abortProcess("probe search: no contact before timeout");
        }
        else
        {
            abortProcess("probe calibration error");
        }
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
        startForceTare(ProcedurePhase::InsertInitial);
    }
}

void ProcedureController::handleInsertInitial()
{
    const MachineConfig::Motion& motion = machine_.machineConfig().motion;
    const float target = machine_.testConfiguration().target_force_N;
    const float fz = machine_.hostInputs().force.fz;
    const float free_tol = machine_.machineConfig().phase2.force_tolerance_N;
    const float search = machine_.machineConfig().force.insert_search_distance;
    const float buffer = machine_.machineConfig().force.insertion_buffer;

    // Leg 3: return to extraction_z, then Phase 2.
    if (cycle_leg_ == 3)
    {
        if (!motionSettled())
        {
            return;
        }
        std::fprintf(stderr,
                     "[InsertInitial] Phase1 done insertion_z=%.3f extraction_z=%.3f force_free_z=%.3f → OptimizeInsert\n",
                     insertion_z_, extraction_z_, force_free_z_);
        startForceTare(ProcedurePhase::OptimizeInsert);
        return;
    }

    // Leg 1: travel remaining +Z reserve to extraction_z.
    if (cycle_leg_ == 1)
    {
        if (!motionSettled())
        {
            return;
        }
        std::fprintf(stderr,
                     "[InsertInitial] at extraction_z=%.3f → IN dive to force_free-search=%.3f\n",
                     extraction_z_, force_free_z_ - search);
        cycle_leg_ = 2;
        machine_.setActiveMotionProfile(motion.phase1_v_insert, motion.phase1_a,
                                        motion.phase1_corner);
        machine_.setCollisionContext(CollisionContext::Contact);
        machine_.moveTo(point_x_, point_y_, force_free_z_ - search);
        return;
    }

    // Leg 0: extract +Z until force-free, then command reserve to extraction_z.
    if (cycle_leg_ == 0)
    {
        if (std::fabs(fz) <= free_tol)
        {
            force_free_z_ = machine_.z();
            extraction_z_ = force_free_z_ + buffer;
            cycle_leg_ = 1;
            machine_.setActiveMotionProfile(motion.phase1_v_extract, motion.phase1_a,
                                            motion.phase1_corner);
            std::fprintf(stderr,
                         "[InsertInitial] force-free z=%.3f fz=%.3f → reserve to extraction_z=%.3f\n",
                         force_free_z_, fz, extraction_z_);
            machine_.moveTo(point_x_, point_y_, extraction_z_);
            return;
        }
        if (motionSettled())
        {
            abortProcess();
        }
        return;
    }

    // Leg 2: insert -Z until target (compression, fz >= target).
    if (fz >= target)
    {
        insertion_z_ = machine_.z();
        if (!(extraction_z_ > insertion_z_))
        {
            extraction_z_ = insertion_z_ + buffer;
        }
        std::fprintf(stderr,
                     "[InsertInitial] contact ok fz=%.3f target=%.3f insertion_z=%.3f → OUT to extraction_z=%.3f\n",
                     fz, target, insertion_z_, extraction_z_);
        cycle_leg_ = 3;
        machine_.setActiveMotionProfile(motion.phase1_v_extract, motion.phase1_a,
                                        motion.phase1_corner);
        machine_.moveTo(point_x_, point_y_, extraction_z_);
        return;
    }
    if (motionSettled() || zMinBlocked())
    {
        std::fprintf(stderr,
                     "[InsertInitial] ABORT settled/zMin fz=%.3f target=%.3f pose.z=%.3f "
                     "search=%.3f\n",
                     fz, target, machine_.z(), search);
        abortProcess();
    }
}

void ProcedureController::handleOptimizeInsert()
{
    const float fz = machine_.hostInputs().force.fz;
    const float target = machine_.testConfiguration().target_force_N;
    const MachineConfig::Phase2& p2 = machine_.machineConfig().phase2;
    const MachineConfig::Motion& motion = machine_.machineConfig().motion;

    // Leg 0: inserting extraction_z → insertion_z (measure while moving + at end).
    if (cycle_leg_ == 0)
    {
        phase2_motion_fz_ = fz;
        if (!motionSettled())
        {
            return;
        }
        if (zMinBlocked())
        {
            abortProcess();
            return;
        }
        phase2_measured_fz_ = fz;
        std::fprintf(stderr,
                     "[OptimizeInsert] iter %u at insertion_z=%.3f fz_motion=%.3f fz_end=%.3f "
                     "target=%.3f → extract to %.3f v_extract=%.3f\n",
                     optimize_attempts_ + 1u, machine_.z(), phase2_motion_fz_, phase2_measured_fz_,
                     target, extraction_z_, motion.phase2_v_extract);
        machine_.setActiveMotionProfile(motion.phase2_v_extract, motion.phase2_a,
                                        motion.phase2_corner);
        machine_.setCollisionContext(CollisionContext::Retract);
        machine_.moveTo(point_x_, point_y_, extraction_z_);
        cycle_leg_ = 1;
        return;
    }

    // Leg 1: at extraction_z — compare force, correct or finish.
    if (!motionSettled())
    {
        return;
    }
    const float error = phase2_measured_fz_ - target;
    std::fprintf(stderr,
                 "[OptimizeInsert] iter %u at extraction_z=%.3f fz_measured=%.3f target=%.3f "
                 "err=%.3f tol=%.3f\n",
                 optimize_attempts_ + 1u, machine_.z(), phase2_measured_fz_, target, error,
                 p2.force_tolerance_N);
    if (std::fabs(error) <= p2.force_tolerance_N)
    {
        std::fprintf(stderr, "[OptimizeInsert] force in tolerance → TestOrAge\n");
        startForceTare(ProcedurePhase::TestOrAge);
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

    // F > target (too much compression) → less insert → +Z. F < target → more insert → −Z.
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
    std::fprintf(stderr,
                 "[OptimizeInsert] correction dz=%.3f → new insertion_z=%.3f\n", dz, insertion_z_);

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

    machine_.setActiveMotionProfile(motion.phase2_v_insert, motion.phase2_a, motion.phase2_corner);
    machine_.setCollisionContext(CollisionContext::Contact);
    std::fprintf(stderr,
                 "[OptimizeInsert] next iter insert to %.3f v_insert=%.3f\n", insertion_z_,
                 motion.phase2_v_insert);
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
    const MachineConfig::Motion& motion = machine_.machineConfig().motion;
    if (cycle_leg_ == 0)
    {
        machine_.setActiveMotionProfile(motion.phase3_v_extract, motion.phase3_a,
                                        motion.phase3_corner);
        machine_.setCollisionContext(CollisionContext::Retract);
        machine_.moveTo(point_x_, point_y_, extraction_z_);
        cycle_leg_ = 1;
        return;
    }
    ++cycle_iteration_;
    std::fprintf(stderr,
                 "[TestOrAge] cycle %u/%u mode=%s\n", cycle_iteration_,
                 machine_.testConfiguration().iterations,
                 machine_.testConfiguration().mode == TestMode::Test ? "Test" : "Aging");
    if (cycle_iteration_ >= machine_.testConfiguration().iterations)
    {
        startForceTare(ProcedurePhase::Unclamp);
        return;
    }
    force_path_.setIteration(static_cast<std::uint16_t>(cycle_iteration_));
    machine_.setActiveMotionProfile(motion.phase3_v_insert, motion.phase3_a, motion.phase3_corner);
    machine_.setCollisionContext(CollisionContext::Contact);
    machine_.moveTo(point_x_, point_y_, insertion_z_);
    cycle_leg_ = 0;
}

void ProcedureController::handleUnclamp()
{
    const float standoff = machine_.machineConfig().probe.approach_distance;

    if (cycle_leg_ == 0)
    {
        if (!motionSettled())
        {
            return;
        }
        machine_.setSpreaderExtend(false);
        cycle_leg_ = 1;
        return;
    }
    if (cycle_leg_ == 1)
    {
        if (machine_.spreaderBusy() || machine_.hostOutputs().spreaderExtended)
        {
            return;
        }
        const MachineConfig::Motion& motion = machine_.machineConfig().motion;
        machine_.setActiveMotionProfile(motion.v_unclamp, motion.a_testpoints,
                                        motion.corner_testpoints);
        machine_.setCollisionContext(CollisionContext::Retract);
        commandLowerToZ(target_machine_.z + standoff);
        cycle_leg_ = 2;
        return;
    }
    if (!motionSettled())
    {
        return;
    }
    enterPhase(ProcedurePhase::NextPoint);
    handleNextPoint();
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
    // approach_leg_ 2: Machine::home() already started — wait for Homed / HomingError.
    if (approach_leg_ == 2)
    {
        if (machine_.homingState() == HomingState::HomingError)
        {
            std::fprintf(stderr, "[MoveSafeEnd] HomingError → abort\n");
            abortProcess();
            return;
        }
        if (machine_.machineState() == MachineState::Homing)
        {
            return;
        }
        if (machine_.isHomed() && machine_.homingState() == HomingState::Homed)
        {
            std::fprintf(stderr,
                         "[MoveSafeEnd] Machine::home() done pose=(%.3f,%.3f,%.3f) → Completed\n",
                         machine_.x(), machine_.y(), machine_.z());
            enterPhase(ProcedurePhase::Completed);
        }
        return;
    }

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
        std::fprintf(stderr, "[MoveSafeEnd] Safe-Z ok → start Machine::home()\n");
        if (!machine_.home())
        {
            abortProcess();
            return;
        }
        approach_leg_ = 2;
        return;
    }
}
