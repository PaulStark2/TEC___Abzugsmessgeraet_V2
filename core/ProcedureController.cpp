#include "ProcedureController.hpp"
#include "AbortReason.hpp"

#include "SafeZ.hpp"

#include <cstdio>
#include <algorithm>
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
    , probe_calib_standalone_(false)
    , tare_next_(ProcedurePhase::Idle)
    , fine_approach_after_tare_(false)
    , plate_bore_standalone_(false)
    , plate_bore_point_id_(0)
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
    case ProcedurePhase::ProbeRemeasure:
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
    machine_.clearAbortReason();
    probe_calib_standalone_ = false;
    plate_bore_standalone_ = false;
    calib_approach_standalone_ = false;
    test_or_age_standalone_ = false;
    test_or_age_await_spreader_ = false;
    plate_bore_point_id_ = 0;
    fine_approach_after_tare_ = false;
    probe_bore_after_tare_ = false;
    plate_bore_after_tare_ = false;
    plate_await_spreader_ = false;
    probe_await_spreader_retract_ = false;
    preclamp_await_spreader_retracted_ = false;
    point_stage_ = 0;
    anomaly_exit_pending_ = false;
    anomaly_report_ = AnomalyReport{};
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
    if (machine_.machineState() == MachineState::Fault)
    {
        return false;
    }
    // Need HomingState::Homed; TCP is jogged above the bore (not isHomed() pose).
    if (machine_.homingState() != HomingState::Homed)
    {
        return false;
    }
    // Soft-STOP latch after a prior teach abort: recover to Idle via no-op moveTo.
    if (machine_.machineState() == MachineState::Stopped ||
        machine_.machineState() == MachineState::Paused)
    {
        machine_.moveTo(machine_.x(), machine_.y(), machine_.z());
    }
    // Spreizer must be open before / during Einmessen (avoid tool damage).
    machine_.setSpreaderExtend(true);
    process_state_ = ProcessState::Running;
    setAutomaticProcessGuards(true);
    probe_calib_standalone_ = true;
    plate_bore_standalone_ = false;
    calib_approach_standalone_ = false;
    test_or_age_standalone_ = false;
    test_or_age_await_spreader_ = false;
    probe_await_spreader_ = true;
    phase_ = ProcedurePhase::ProbeCalibrate;
    return true;
}

bool ProcedureController::startPlateBoreSearch(std::uint8_t point_id)
{
    if (process_state_ == ProcessState::Running)
    {
        return false;
    }
    if (machine_.machineState() == MachineState::Fault)
    {
        return false;
    }
    if (machine_.homingState() != HomingState::Homed)
    {
        return false;
    }
    if (point_id < 1 || point_id > 4)
    {
        return false;
    }
    // Soft-STOP latch after a prior teach/abort: recover to Idle via no-op moveTo.
    if (machine_.machineState() == MachineState::Stopped ||
        machine_.machineState() == MachineState::Paused)
    {
        machine_.moveTo(machine_.x(), machine_.y(), machine_.z());
    }
    process_state_ = ProcessState::Running;
    setAutomaticProcessGuards(true);
    probe_calib_standalone_ = false;
    calib_approach_standalone_ = false;
    test_or_age_standalone_ = false;
    test_or_age_await_spreader_ = false;
    plate_bore_standalone_ = true;
    plate_bore_point_id_ = point_id;
    preclamp_await_confirm_ = false;
    plate_bore_after_tare_ = false;
    plate_await_spreader_ = true;
    machine_.setSpreaderExtend(true);
    machine_.setCollisionContext(CollisionContext::Approach);
    phase_ = ProcedurePhase::PlateBoreSearch;
    return true;
}

bool ProcedureController::resolveCalibApproachTarget(std::uint8_t point_id, TestPoint& out) const
{
    const CalibrationSet& calib = machine_.calibrationSet();
    switch (point_id)
    {
    case 1:
        if (!calib.plate_origin_valid)
        {
            return false;
        }
        out = calib.plate_origin;
        return true;
    case 2:
        if (!calib.rotation_points_valid)
        {
            return false;
        }
        out = calib.k1;
        return true;
    case 3:
        if (!calib.rotation_points_valid)
        {
            return false;
        }
        out = calib.k2;
        return true;
    case 4:
        if (!calib.rotation_points_valid)
        {
            return false;
        }
        out = calib.k3;
        return true;
    case 5:
        if (!calib.probe_center_valid)
        {
            return false;
        }
        out = calib.probe_center;
        return true;
    default:
        return false;
    }
}

bool ProcedureController::startApproachCalibPoint(std::uint8_t point_id)
{
    if (process_state_ == ProcessState::Running)
    {
        return false;
    }
    if (machine_.machineState() == MachineState::Fault)
    {
        return false;
    }
    if (machine_.homingState() != HomingState::Homed)
    {
        return false;
    }
    TestPoint target{};
    if (!resolveCalibApproachTarget(point_id, target))
    {
        return false;
    }
    float safe_z = 0.0f;
    if (!resolveCalibSafeZMachine(machine_.calibrationSet(),
                                  machine_.machineConfig().motion.safe_z_calib_buffer, safe_z))
    {
        return false;
    }
    (void)safe_z;
    if (machine_.machineState() == MachineState::Stopped ||
        machine_.machineState() == MachineState::Paused)
    {
        machine_.moveTo(machine_.x(), machine_.y(), machine_.z());
    }
    target_machine_ = target;
    process_state_ = ProcessState::Running;
    setAutomaticProcessGuards(true);
    probe_calib_standalone_ = false;
    plate_bore_standalone_ = false;
    calib_approach_standalone_ = true;
    test_or_age_standalone_ = false;
    test_or_age_await_spreader_ = false;
    preclamp_await_confirm_ = false;
    enterPhase(ProcedurePhase::CalibApproach);
    return process_state_ == ProcessState::Running;
}

bool ProcedureController::startTestOrAgeManual()
{
    if (process_state_ == ProcessState::Running)
    {
        return false;
    }
    if (machine_.machineState() == MachineState::Fault)
    {
        return false;
    }
    if (machine_.homingState() != HomingState::Homed)
    {
        return false;
    }
    const TestConfiguration& tcfg = machine_.testConfiguration();
    if (!(tcfg.target_force_N > 0.0f) || tcfg.iterations == 0u)
    {
        std::fprintf(stderr,
                     "[TestOrAgeManual] REJECTED force=%.3f iterations=%u\n", tcfg.target_force_N,
                     tcfg.iterations);
        return false;
    }
    if (machine_.machineState() == MachineState::Stopped ||
        machine_.machineState() == MachineState::Paused)
    {
        machine_.moveTo(machine_.x(), machine_.y(), machine_.z());
    }
    machine_.setSpreaderExtend(true);
    process_state_ = ProcessState::Running;
    setAutomaticProcessGuards(true);
    machine_.clearAbortReason();
    probe_calib_standalone_ = false;
    plate_bore_standalone_ = false;
    calib_approach_standalone_ = false;
    test_or_age_standalone_ = true;
    test_or_age_await_spreader_ = true;
    fine_approach_after_tare_ = false;
    probe_bore_after_tare_ = false;
    plate_bore_after_tare_ = false;
    point_stage_ = 1;
    anomaly_exit_pending_ = false;
    anomaly_report_ = AnomalyReport{};
    tare_next_ = ProcedurePhase::InsertInitial;
    // Wait for spreader, then beginForceTare in handleForceTare (do not enterPhase yet).
    phase_ = ProcedurePhase::ForceTare;
    std::fprintf(stderr,
                 "[TestOrAgeManual] start pose=(%.3f,%.3f,%.3f) target=%.3f iterations=%u mode=%u\n",
                 machine_.x(), machine_.y(), machine_.z(), tcfg.target_force_N, tcfg.iterations,
                 static_cast<unsigned>(tcfg.mode));
    return true;
}

void ProcedureController::tick(float dt)
{
    if (process_state_ != ProcessState::Running)
    {
        return;
    }
    if (machine_.machineState() == MachineState::Stopped)
    {
        abortProcess(AbortReason::SoftStop);
        return;
    }
    if (machine_.machineState() == MachineState::Fault)
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

    if (machine_.machineState() == MachineState::Stopped)
    {
        abortProcess(AbortReason::SoftStop);
        return;
    }
    if (machine_.machineState() == MachineState::Fault)
    {
        abortProcess();
        return;
    }
    if (machine_.machineState() == MachineState::Paused)
    {
        return;
    }

    if (anomaly_exit_pending_)
    {
        tickAnomalyExit();
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
    case ProcedurePhase::TestOrAge:
        handleTestOrAge();
        break;
    case ProcedurePhase::ProbeCalibrate:
        handleProbeCalibrate(dt);
        break;
    case ProcedurePhase::ProbeRemeasure:
        handleProbeRemeasure(dt);
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
    case ProcedurePhase::PlateBoreSearch:
        handlePlateBoreSearch(dt);
        break;
    case ProcedurePhase::CalibApproach:
        handleCalibApproach();
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
    abortProcess(AbortReason::SoftStop);
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
        machine_.setSpreaderExtend(true);
        machine_.setActiveMotionProfile(probe.v_approach, motion.a_testpoints, motion.corner_testpoints);
        approach_leg_ = 0;
        commandRaiseSafeZ();
        break;
    case ProcedurePhase::CalibApproach:
        machine_.setCollisionContext(CollisionContext::Approach);
        machine_.setActiveMotionProfile(probe.v_approach, motion.a_testpoints, motion.corner_testpoints);
        approach_leg_ = 0;
        commandRaiseCalibSafeZ();
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
        point_stage_ = 0;
        approach_leg_ = 0;
        commandRaiseSafeZ();
        break;
    case ProcedurePhase::PreClampXyCorrect:
        // Spreizer must be closed (deactivated) during PreClamp wall search; Clamp opens it.
        machine_.setCollisionContext(CollisionContext::Contact);
        machine_.setActiveMotionProfile(motion.v_bore_search, motion.a_testpoints,
                                        motion.corner_testpoints);
        machine_.setPreClampXyEnabled(false);
        preclamp_await_confirm_ = false;
        preclamp_await_spreader_retracted_ = true;
        machine_.setSpreaderExtend(false);
        break;
    case ProcedurePhase::Clamp:
        // Keep Contact limits until ForceTare finishes. Switching to Clamp (lower
        // thresholds) while residual PreClamp / fine-approach force is still live
        // trips ForceCollision before tare can clear the sample.
        machine_.setPreClampXyEnabled(false);
        machine_.setCollisionContext(CollisionContext::Contact);
        machine_.setSpreaderExtend(true);
        std::fprintf(stderr,
                     "[Clamp] enter Contact context fx=%.3f fy=%.3f fz=%.3f "
                     "contact_N=%.3f clamp_N=%.3f\n",
                     machine_.hostInputs().force.fx, machine_.hostInputs().force.fy,
                     machine_.hostInputs().force.fz, machine_.machineConfig().collision.contact_N,
                     machine_.machineConfig().collision.clamp_N);
        break;
    case ProcedurePhase::InsertInitial:
        machine_.setCollisionContext(CollisionContext::Contact);
        machine_.setActiveMotionProfile(motion.phase1_v_insert, motion.phase1_a,
                                        motion.phase1_corner);
        point_x_ = machine_.x();
        point_y_ = machine_.y();
        insertion_z_ = machine_.z();
        extraction_z_ = machine_.z();
        force_free_z_ = machine_.z();
        cycle_leg_ = 0;
        {
            const float search = machine_.machineConfig().force.insert_search_distance;
            const float dive_z = machine_.z() - search;
            std::fprintf(stderr,
                         "[InsertInitial] SeatToForce IN -Z from z=%.3f to %.3f fz=%.3f target=%.3f "
                         "v_insert=%.3f\n",
                         machine_.z(), dive_z, machine_.hostInputs().force.fz,
                         machine_.testConfiguration().target_force_N, motion.phase1_v_insert);
            machine_.moveTo(point_x_, point_y_, dive_z);
        }
        break;
    case ProcedurePhase::TestOrAge:
        cycle_iteration_ = 0;
        cycle_leg_ = 0;
        force_path_.reset();
        force_path_.setIteration(0);
        force_path_.setLeg(ForcePathRecorder::kLegExtract);
        force_path_have_z_ = false;
        force_path_last_z_ = 0.0f;
        resetExtractSeek();
        ref_insert_valid_ = false;
        ref_extract_valid_ = false;
        point_x_ = machine_.x();
        point_y_ = machine_.y();
        machine_.setCollisionContext(CollisionContext::Measure);
        machine_.setActiveMotionProfile(motion.phase3_v_extract, motion.phase3_a,
                                        motion.phase3_corner);
        {
            const float search = machine_.machineConfig().force.insert_search_distance;
            std::fprintf(stderr,
                         "[TestOrAge] start extract +Z looking for kraftfrei (after Zug) from z=%.3f "
                         "search=%.3f\n",
                         machine_.z(), search);
            machine_.moveTo(point_x_, point_y_, machine_.z() + search);
        }
        break;
    case ProcedurePhase::Unclamp:
        machine_.setPreClampXyEnabled(false);
        cycle_leg_ = 0;
        point_x_ = machine_.x();
        point_y_ = machine_.y();
        if (unclamp_mode_ == UnclampMode::AfterSeat)
        {
            // Still seated under load — keep Contact (not Unclamp 20 N) until the
            // fixed retract lift finishes and ForceTare runs load-free.
            point_stage_ = 1;
            machine_.setCollisionContext(CollisionContext::Contact);
            insert_return_to_free_ = false;
            machine_.setActiveMotionProfile(motion.v_unclamp, motion.a_testpoints,
                                            motion.corner_testpoints);
            std::fprintf(stderr,
                         "[Unclamp] AfterSeat at z=%.3f insertion_z=%.3f retract_mm=%.3f\n",
                         machine_.z(), insertion_z_,
                         machine_.machineConfig().force.after_seat_retract_mm);
        }
        else
        {
            // After last extract we may still sit above kraftfrei by extract_after_free_mm.
            // Return that offset at extract_after_free_v before the slow re-seat dive.
            machine_.setCollisionContext(CollisionContext::Contact);
            const float offset_mm = machine_.machineConfig().force.extract_after_free_mm;
            if (offset_mm > 0.0f && machine_.z() > force_free_z_ + 1.0e-3f)
            {
                insert_return_to_free_ = true;
                machine_.setActiveMotionProfile(motion.extract_after_free_v, motion.phase3_a,
                                                motion.phase3_corner);
                std::fprintf(stderr,
                             "[Unclamp] AfterTest return to kraftfrei z=%.3f at v=%.3f "
                             "(then re-seat)\n",
                             force_free_z_, motion.extract_after_free_v);
                machine_.moveTo(point_x_, point_y_, force_free_z_);
            }
            else
            {
                insert_return_to_free_ = false;
                machine_.setActiveMotionProfile(motion.phase1_v_insert, motion.phase1_a,
                                                motion.phase1_corner);
                const float search = machine_.machineConfig().force.insert_search_distance;
                std::fprintf(stderr, "[Unclamp] AfterTest re-seat dive from z=%.3f\n",
                             machine_.z());
                machine_.moveTo(point_x_, point_y_, machine_.z() - search);
            }
        }
        break;
    case ProcedurePhase::ProbeRemeasure:
    {
        const CalibrationSet& calib = machine_.calibrationSet();
        const MachineConfig& cfg = machine_.machineConfig();
        const MachineConfig::Probe& probe = cfg.probe;
        tool_offset_before_remeasure_ = calib.tool_offset;
        probe_remeasure_calib_active_ = false;
        approach_leg_ = 0;
        machine_.setSpreaderExtend(true);
        machine_.setCollisionContext(CollisionContext::Approach);
        machine_.setActiveMotionProfile(probe.v_approach, cfg.motion.a_testpoints,
                                        cfg.motion.corner_testpoints);
        std::fprintf(stderr,
                     "[ProbeRemeasure] enter at Safe-Z pose=(%.3f,%.3f,%.3f) prev_offset=(%.3f,%.3f,%.3f)\n",
                     machine_.x(), machine_.y(), machine_.z(), calib.tool_offset.x, calib.tool_offset.y,
                     calib.tool_offset.z);
        break;
    }
    case ProcedurePhase::ForceTare:
        if (!machine_.beginForceTare())
        {
            abortProcess(AbortReason::ForceTareBusy);
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
    if (phase_ == ProcedurePhase::InsertInitial || phase_ == ProcedurePhase::ProbeRemeasure ||
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

bool ProcedureController::atCalibSafeZ() const
{
    float safe_z = 0.0f;
    if (!resolveCalibSafeZMachine(machine_.calibrationSet(),
                                  machine_.machineConfig().motion.safe_z_calib_buffer, safe_z))
    {
        return false;
    }
    return std::fabs(machine_.z() - safe_z) <= 1.0e-3f;
}

bool ProcedureController::resolveCurrentMachinePoint()
{
    TestPoint plate{};
    if (!program_.pointAt(current_index_, plate))
    {
        return false;
    }
    TestPoint machine_point{};
    if (!toMachinePoint(machine_.calibrationSet(), plate, machine_point))
    {
        return false;
    }
    const float clearance = machine_.machineConfig().motion.point_z_clearance_mm;
    if (!std::isfinite(clearance) || clearance < 0.0f)
    {
        return false;
    }
    floor_machine_z_ = machine_point.z;
    target_machine_ = machine_point;
    target_machine_.z = machine_point.z + clearance;
    return std::isfinite(target_machine_.z) && std::isfinite(floor_machine_z_);
}

bool ProcedureController::resolveSafeZMachine(float& out_safe_z_machine) const
{
    return resolveProgramSafeZMachine(program_, machine_.machineConfig().motion.safe_z_buffer,
                                      machine_.calibrationSet(), out_safe_z_machine,
                                      machine_.machineConfig().motion.point_z_clearance_mm);
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
        const float clearance = machine_.machineConfig().motion.point_z_clearance_mm;
        if (!(machine_point.z + clearance < safe_z_machine))
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

void ProcedureController::commandRaiseCalibSafeZ()
{
    float safe_z = 0.0f;
    if (!resolveCalibSafeZMachine(machine_.calibrationSet(),
                                  machine_.machineConfig().motion.safe_z_calib_buffer, safe_z))
    {
        return;
    }
    machine_.moveTo(machine_.x(), machine_.y(), safe_z);
}

void ProcedureController::commandTraverseCalibSafeXY(float x, float y)
{
    float safe_z = 0.0f;
    if (!resolveCalibSafeZMachine(machine_.calibrationSet(),
                                  machine_.machineConfig().motion.safe_z_calib_buffer, safe_z))
    {
        return;
    }
    machine_.moveTo(x, y, safe_z);
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
    // Fine approach (leg 3) and pneumatic Clamp stay at or above the JSON floor.
    // SeatToForce and TestOrAge call moveTo directly and may go deeper.
    const bool hold_floor = phase_ == ProcedurePhase::Clamp ||
                            (phase_ == ProcedurePhase::PointApproach && approach_leg_ >= 3);
    if (hold_floor && z < floor_machine_z_)
    {
        z = floor_machine_z_;
    }
    machine_.moveTo(machine_.x(), machine_.y(), z);
}

void ProcedureController::setAutomaticProcessGuards(bool active)
{
    machine_.setCommunicationWatchdogEnabled(active);
    machine_.setRuntimeConfigLocked(active);
}

void ProcedureController::abortProcess(AbortReason reason)
{
    machine_.latchAbortReason(reason);

    const bool teach_standalone = probe_calib_standalone_ || plate_bore_standalone_ ||
                                  calib_approach_standalone_ || test_or_age_standalone_;
    probe_calib_.abort(machine_);
    bore_search_.abort();
    machine_.setPreClampXyEnabled(false);
    fine_approach_after_tare_ = false;
    probe_bore_after_tare_ = false;
    plate_bore_after_tare_ = false;
    plate_await_spreader_ = false;
    probe_await_spreader_retract_ = false;
    preclamp_await_spreader_retracted_ = false;
    test_or_age_await_spreader_ = false;
    setAutomaticProcessGuards(false);
    machine_.setCollisionContext(CollisionContext::Approach);

    if (teach_standalone)
    {
        // Teach / hand TestOrAge: keep HomingState::Homed and leave Idle so
        // commands can retry without re-home.
        if (machine_.machineState() != MachineState::Fault)
        {
            machine_.moveTo(machine_.x(), machine_.y(), machine_.z());
        }
        phase_ = ProcedurePhase::Aborted;
        process_state_ = ProcessState::Aborted;
        probe_calib_standalone_ = false;
        plate_bore_standalone_ = false;
        calib_approach_standalone_ = false;
        test_or_age_standalone_ = false;
        std::fprintf(stderr,
                     "[ProcedureAbort] reason=%s | teach standalone (Homed kept, Idle) fault=%u "
                     "pose=(%.3f,%.3f,%.3f)\n",
                     abortReasonMessage(reason),
                     static_cast<unsigned>(machine_.lastFaultReason()), machine_.x(), machine_.y(),
                     machine_.z());
        return;
    }

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
                 abortReasonMessage(reason),
                 static_cast<unsigned>(machine_.lastFaultReason()),
                 static_cast<unsigned>(machine_.lastForceWatchdogTrip()),
                 machine_.hostInputs().force.fz, machine_.x(), machine_.y(), machine_.z(),
                 machine_.rawX(), machine_.rawY(), machine_.rawZ());
}

void ProcedureController::startForceTare(ProcedurePhase next)
{
    fine_approach_after_tare_ = false;
    probe_bore_after_tare_ = false;
    tare_next_ = next;
    enterPhase(ProcedurePhase::ForceTare);
}

void ProcedureController::startForceTareBeforeFineApproach()
{
    fine_approach_after_tare_ = true;
    probe_bore_after_tare_ = false;
    tare_next_ = ProcedurePhase::Idle;
    enterPhase(ProcedurePhase::ForceTare);
}

void ProcedureController::startForceTareBeforeProbeBore(ProbeCalibration::StorePolicy policy,
                                                       bool remeasure)
{
    fine_approach_after_tare_ = false;
    probe_bore_after_tare_ = true;
    plate_bore_after_tare_ = false;
    probe_bore_policy_ = policy;
    probe_bore_remeasure_ = remeasure;
    tare_next_ = ProcedurePhase::Idle;
    enterPhase(ProcedurePhase::ForceTare);
}

void ProcedureController::startForceTareBeforePlateBore()
{
    fine_approach_after_tare_ = false;
    probe_bore_after_tare_ = false;
    plate_bore_after_tare_ = true;
    tare_next_ = ProcedurePhase::Idle;
    enterPhase(ProcedurePhase::ForceTare);
}

void ProcedureController::startProbeBoreAfterTare()
{
    const TestPoint hub{machine_.x(), machine_.y(), machine_.z()};
    std::fprintf(stderr,
                 "[ProbeBore] after Ruheprobe start policy=%u rem=%d pose=(%.3f,%.3f,%.3f)\n",
                 static_cast<unsigned>(probe_bore_policy_), probe_bore_remeasure_ ? 1 : 0,
                 hub.x, hub.y, hub.z);
    if (!probe_calib_.start(machine_, hub, probe_bore_policy_))
    {
        abortProcess(probe_bore_remeasure_ ? AbortReason::ProbeRemeasureOffsetOnlyStart
                                           : AbortReason::ProbeBoreSearchFailed);
        return;
    }
    if (probe_bore_remeasure_)
    {
        probe_remeasure_calib_active_ = true;
        phase_ = ProcedurePhase::ProbeRemeasure;
    }
    else
    {
        phase_ = ProcedurePhase::ProbeCalibrate;
    }
}

void ProcedureController::startPlateBoreAfterTare()
{
    const MachineConfig& cfg = machine_.machineConfig();
    const MachineConfig::Probe& probe = cfg.probe;
    BoreSearch::Params params{};
    params.wall_force_N = probe.wall_force_N;
    params.floor_force_N = probe.floor_force_N;
    params.xy_search_limit = probe.xy_search_limit;
    params.z_search_limit = probe.z_search_limit;
    params.z_lift_mm = probe.z_lift_mm;
    params.timeout_s = probe.timeout_s;
    params.refine_passes = probe.refine_passes;
    params.do_floor = true;
    params.z_first = true;
    machine_.setCollisionContext(CollisionContext::Contact);
    machine_.setActiveMotionProfile(probe.v_search, cfg.motion.a_testpoints,
                                    cfg.motion.corner_testpoints);
    machine_.setPreClampXyEnabled(false);
    std::fprintf(stderr,
                 "[PlateBore] after Ruheprobe z_first point_id=%u pose=(%.3f,%.3f,%.3f)\n",
                 static_cast<unsigned>(plate_bore_point_id_), machine_.x(), machine_.y(),
                 machine_.z());
    if (!bore_search_.start(machine_, params))
    {
        abortProcess(AbortReason::PlateBoreStartFailed);
        return;
    }
    phase_ = ProcedurePhase::PlateBoreSearch;
}

bool ProcedureController::awaitSpreaderRetracted()
{
    if (!machine_.spreaderBusy() && !machine_.hostOutputs().spreaderExtended)
    {
        return true;
    }
    if (!machine_.spreaderBusy() && machine_.hostOutputs().spreaderExtended)
    {
        machine_.setSpreaderExtend(false);
    }
    return false;
}

void ProcedureController::commandFineApproach()
{
    const MachineConfig::Motion& motion = machine_.machineConfig().motion;
    phase_ = ProcedurePhase::PointApproach;
    machine_.setActiveMotionProfile(motion.v_fine_testpoints, motion.a_testpoints,
                                    motion.corner_testpoints);
    machine_.setCollisionContext(CollisionContext::Contact);
    std::fprintf(stderr,
                 "[PointApproach] FINE approach=%.3f floor=%.3f v_fine_testpoints=%.3f pose=(%.3f,%.3f,%.3f) "
                 "fz=%.3f\n",
                 target_machine_.z, floor_machine_z_, motion.v_fine_testpoints, machine_.x(), machine_.y(),
                 machine_.z(), machine_.hostInputs().force.fz);
    commandLowerToZ(target_machine_.z);
    approach_leg_ = 3;
}

void ProcedureController::handleForceTare()
{
    if (test_or_age_await_spreader_)
    {
        if (machine_.spreaderBusy())
        {
            return;
        }
        if (!machine_.hostOutputs().spreaderExtended)
        {
            machine_.setSpreaderExtend(true);
            return;
        }
        test_or_age_await_spreader_ = false;
        if (!machine_.beginForceTare())
        {
            abortProcess(AbortReason::ForceTareBusy);
        }
        return;
    }
    if (machine_.forceTareActive())
    {
        return;
    }
    if (probe_bore_after_tare_)
    {
        probe_bore_after_tare_ = false;
        startProbeBoreAfterTare();
        return;
    }
    if (plate_bore_after_tare_)
    {
        plate_bore_after_tare_ = false;
        startPlateBoreAfterTare();
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
            abortProcess(AbortReason::PointApproachXyMiss);
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
    const MachineConfig& cfg = machine_.machineConfig();
    const MachineConfig::Probe& probe = cfg.probe;
    const MachineConfig::Motion& motion = cfg.motion;
    // Formula 2: target = probe_center - tool_offset (last measured TCP in bore).
    const TestPoint target{calib.probe_center.x - calib.tool_offset.x,
                           calib.probe_center.y - calib.tool_offset.y,
                           calib.probe_center.z - calib.tool_offset.z};
    const float standoff = probe.approach_distance;
    const float coarse_z = target.z + standoff;

    if (!machine_.hostOutputs().spreaderExtended && !machine_.spreaderBusy())
    {
        machine_.setSpreaderExtend(true);
    }

    // Leg 2: coarse Z above bore → Ruheprobe → OffsetOnly z_first BoreSearch.
    if (approach_leg_ == 2)
    {
        if (!motionSettled())
        {
            return;
        }
        if (machine_.spreaderBusy() || !machine_.hostOutputs().spreaderExtended)
        {
            return;
        }
        std::fprintf(stderr,
                     "[ProbeApproach] coarse done → Ruheprobe then OffsetOnly "
                     "pose=(%.3f,%.3f,%.3f) target=(%.3f,%.3f,%.3f) "
                     "tool_offset=(%.3f,%.3f,%.3f) v_search=%.3f\n",
                     machine_.x(), machine_.y(), machine_.z(), target.x, target.y, target.z,
                     calib.tool_offset.x, calib.tool_offset.y, calib.tool_offset.z,
                     probe.v_search);
        probe_await_spreader_ = false;
        startForceTareBeforeProbeBore(ProbeCalibration::StorePolicy::OffsetOnly, false);
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
            abortProcess(AbortReason::ProbeApproachSafeZLeg0);
            return;
        }
        machine_.setActiveMotionProfile(probe.v_approach, motion.a_testpoints, motion.corner_testpoints);
        machine_.setCollisionContext(CollisionContext::Approach);
        std::fprintf(stderr,
                     "[ProbeApproach] XY v_approach=%.3f → (%.3f,%.3f) @ Safe-Z "
                     "(probe_center-tool_offset)\n",
                     probe.v_approach, target.x, target.y);
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
            abortProcess(AbortReason::ProbeApproachXyMiss);
            return;
        }
        if (!atSafeZ())
        {
            abortProcess(AbortReason::ProbeApproachSafeZLeg1);
            return;
        }
        machine_.setActiveMotionProfile(probe.v_approach, motion.a_testpoints, motion.corner_testpoints);
        machine_.setCollisionContext(CollisionContext::Approach);
        std::fprintf(stderr,
                     "[ProbeApproach] COARSE z=%.3f (target.z=%.3f + standoff=%.3f) "
                     "v_approach=%.3f pose=(%.3f,%.3f,%.3f)\n",
                     coarse_z, target.z, standoff, probe.v_approach, machine_.x(), machine_.y(),
                     machine_.z());
        commandLowerToZ(coarse_z);
        approach_leg_ = 2;
        return;
    }
}

void ProcedureController::handleProbeCalibrate(float dt)
{
    if (probe_await_spreader_)
    {
        if (machine_.spreaderBusy())
        {
            return;
        }
        if (!machine_.hostOutputs().spreaderExtended)
        {
            machine_.setSpreaderExtend(true);
            return;
        }
        // Standalone Install: Spreizer open at hub → Ruheprobe → z_first BoreSearch.
        probe_await_spreader_ = false;
        startForceTareBeforeProbeBore(ProbeCalibration::StorePolicy::Install, false);
        return;
    }

    if (probe_await_spreader_retract_)
    {
        if (!awaitSpreaderRetracted())
        {
            return;
        }
        probe_await_spreader_retract_ = false;
        if (probe_calib_standalone_)
        {
            machine_.setCollisionContext(CollisionContext::Approach);
            setAutomaticProcessGuards(false);
            if (machine_.machineState() != MachineState::Fault)
            {
                machine_.moveTo(machine_.x(), machine_.y(), machine_.z());
            }
            phase_ = ProcedurePhase::Completed;
            process_state_ = ProcessState::Completed;
            probe_calib_standalone_ = false;
        }
        else
        {
            enterPhase(ProcedurePhase::MoveSafePose);
            if (motionSettled())
            {
                handleMoveSafePose();
            }
        }
        return;
    }

    probe_calib_.tick(dt, machine_);
    if (probe_calib_.isComplete())
    {
        // After Einmessen: Spreizer einfahren before Messpunkt Feinanfahrt / Completed.
        probe_await_spreader_retract_ = true;
        machine_.setSpreaderExtend(false);
        return;
    }
    if (probe_calib_.isError())
    {
        const BoreSearch::ErrorReason reason = probe_calib_.searchErrorReason();
        if (reason == BoreSearch::ErrorReason::Timeout ||
            reason == BoreSearch::ErrorReason::WallNotFound ||
            reason == BoreSearch::ErrorReason::FloorNotFound)
        {
            machine_.latchFaultReason(FaultReason::ProbeSearchNoContact);
            abortProcess(AbortReason::ProbeBoreSearchFailed);
        }
        else
        {
            abortProcess(AbortReason::ProbeCalibrationError);
        }
    }
}

const ProcedureController::BoreReport& ProcedureController::boreReport() const
{
    return bore_report_;
}

void ProcedureController::consumeBoreReport()
{
    bore_report_.fresh = false;
}

void ProcedureController::publishBoreReport(const BoreSearch::Result& result, bool confirm_pending)
{
    bore_report_ = BoreReport{};
    bore_report_.fresh = true;
    bore_report_.anomaly = result.anomaly;
    bore_report_.confirm_pending = confirm_pending;
    bore_report_.cx = result.cx;
    bore_report_.cy = result.cy;
    bore_report_.cz = result.cz;
    bore_report_.radius = result.radius;
    bore_report_.residual = 0.0f;
    bore_report_.fx = result.fx;
    bore_report_.fy = result.fy;
    bore_report_.point_id = plate_bore_standalone_ ? plate_bore_point_id_ : 0;
    for (int i = 0; i < 4; ++i)
    {
        bore_report_.x[i] = result.points[i].x;
        bore_report_.y[i] = result.points[i].y;
    }
    std::fprintf(stderr,
                 "[BoreSearch] id=%u center=(%.3f,%.3f,%.3f) r=%.3f fx=%.3f fy=%.3f anomaly=%d confirm=%d\n",
                 static_cast<unsigned>(bore_report_.point_id), static_cast<double>(result.cx),
                 static_cast<double>(result.cy), static_cast<double>(result.cz),
                 static_cast<double>(result.radius), static_cast<double>(result.fx),
                 static_cast<double>(result.fy), result.anomaly ? 1 : 0, confirm_pending ? 1 : 0);
}

void ProcedureController::handlePlateBoreSearch(float dt)
{
    if (plate_await_spreader_)
    {
        if (machine_.spreaderBusy())
        {
            return;
        }
        if (!machine_.hostOutputs().spreaderExtended)
        {
            machine_.setSpreaderExtend(true);
            return;
        }
        plate_await_spreader_ = false;
        startForceTareBeforePlateBore();
        return;
    }

    bore_search_.tick(dt, machine_);
    if (bore_search_.isError())
    {
        switch (bore_search_.errorReason())
        {
        case BoreSearch::ErrorReason::Timeout:
            abortProcess(AbortReason::PlateBoreTimeout);
            break;
        case BoreSearch::ErrorReason::WallNotFound:
            abortProcess(AbortReason::PlateBoreWallNotFound);
            break;
        case BoreSearch::ErrorReason::FloorNotFound:
            abortProcess(AbortReason::PlateBoreFloorNotFound);
            break;
        default:
            abortProcess(AbortReason::PlateBoreSearchFailed);
            break;
        }
        return;
    }
    if (!bore_search_.isComplete())
    {
        return;
    }
    publishBoreReport(bore_search_.result(), false);
    machine_.setPreClampXyEnabled(false);
    // Teach end must not soft-STOP: Wiederholen / erneute Suche brauchen Idle.
    // Leave Contact context so residual force cannot trip the watchdog after the search.
    machine_.setCollisionContext(CollisionContext::Approach);
    setAutomaticProcessGuards(false);
    if (machine_.machineState() != MachineState::Fault)
    {
        machine_.moveTo(machine_.x(), machine_.y(), machine_.z());
    }
    phase_ = ProcedurePhase::Completed;
    process_state_ = ProcessState::Completed;
    plate_bore_standalone_ = false;
}

void ProcedureController::handleCalibApproach()
{
    const MachineConfig::Probe& probe = machine_.machineConfig().probe;
    const MachineConfig::Motion& motion = machine_.machineConfig().motion;
    const float standoff = probe.approach_distance;
    const float coarse_z = target_machine_.z + standoff;

    if (!motionSettled())
    {
        return;
    }
    if (approach_leg_ == 0)
    {
        if (!atCalibSafeZ())
        {
            abortProcess(AbortReason::CalibApproachSafeZLeg0);
            return;
        }
        machine_.setActiveMotionProfile(probe.v_approach, motion.a_testpoints, motion.corner_testpoints);
        machine_.setCollisionContext(CollisionContext::Approach);
        std::fprintf(stderr,
                     "[CalibApproach] XY v_approach=%.3f → (%.3f,%.3f) @ Safe-Z-Calib\n",
                     probe.v_approach, target_machine_.x, target_machine_.y);
        commandTraverseCalibSafeXY(target_machine_.x, target_machine_.y);
        approach_leg_ = 1;
        return;
    }
    if (approach_leg_ == 1)
    {
        const float xy_tol = 0.05f;
        if (std::fabs(machine_.x() - target_machine_.x) > xy_tol ||
            std::fabs(machine_.y() - target_machine_.y) > xy_tol)
        {
            abortProcess(AbortReason::CalibApproachXyMiss);
            return;
        }
        if (!atCalibSafeZ())
        {
            abortProcess(AbortReason::CalibApproachSafeZLeg1);
            return;
        }
        machine_.setActiveMotionProfile(probe.v_approach, motion.a_testpoints, motion.corner_testpoints);
        machine_.setCollisionContext(CollisionContext::Approach);
        std::fprintf(stderr,
                     "[CalibApproach] COARSE z=%.3f (target.z=%.3f + standoff=%.3f)\n",
                     coarse_z, target_machine_.z, standoff);
        commandLowerToZ(coarse_z);
        approach_leg_ = 2;
        return;
    }
    if (approach_leg_ == 2)
    {
        setAutomaticProcessGuards(false);
        if (machine_.machineState() != MachineState::Fault)
        {
            machine_.moveTo(machine_.x(), machine_.y(), machine_.z());
        }
        phase_ = ProcedurePhase::Completed;
        process_state_ = ProcessState::Completed;
        calib_approach_standalone_ = false;
        std::fprintf(stderr,
                     "[CalibApproach] done pose=(%.3f,%.3f,%.3f)\n", machine_.x(), machine_.y(),
                     machine_.z());
    }
}

void ProcedureController::handlePreClamp(float dt)
{
    if (preclamp_await_confirm_)
    {
        if (machine_.machineState() == MachineState::Paused)
        {
            return;
        }
        preclamp_await_confirm_ = false;
        bore_report_.confirm_pending = false;
        bore_report_.fresh = true;
        machine_.setPreClampXyEnabled(false);
        enterPhase(ProcedurePhase::Clamp);
        return;
    }

    if (preclamp_await_spreader_retracted_)
    {
        if (!awaitSpreaderRetracted())
        {
            return;
        }
        preclamp_await_spreader_retracted_ = false;
        const MachineConfig::Force& force_cfg = machine_.machineConfig().force;
        BoreSearch::Params params{};
        params.wall_force_N = force_cfg.xy_tolerance_N;
        params.xy_search_limit = force_cfg.xy_search_limit;
        params.timeout_s = force_cfg.xy_correct_timeout_s;
        params.refine_passes = force_cfg.bore_refine_passes;
        params.do_floor = false;
        if (!bore_search_.start(machine_, params))
        {
            abortProcess(AbortReason::PreclampStartFailed);
            return;
        }
        machine_.moveTo(machine_.x(), machine_.y(), machine_.z());
        return;
    }

    bore_search_.tick(dt, machine_);

    if (bore_search_.isError())
    {
        switch (bore_search_.errorReason())
        {
        case BoreSearch::ErrorReason::Timeout:
            abortProcess(AbortReason::PreclampTimeout);
            break;
        case BoreSearch::ErrorReason::WallNotFound:
            abortProcess(AbortReason::PreclampWallNotFound);
            break;
        default:
            abortProcess(AbortReason::PreclampSearchFailed);
            break;
        }
        return;
    }

    if (!bore_search_.isComplete())
    {
        return;
    }

    const BoreSearch::Result& result = bore_search_.result();
    const MachineConfig::Force& force_cfg = machine_.machineConfig().force;
    if (result.anomaly && force_cfg.preclamp_confirm)
    {
        publishBoreReport(result, true);
        preclamp_await_confirm_ = true;
        machine_.pauseHolding();
        return;
    }

    publishBoreReport(result, false);
    machine_.setPreClampXyEnabled(false);
    enterPhase(ProcedurePhase::Clamp);
}

void ProcedureController::handleClamp()
{
    if (anomaly_report_.confirm_pending)
    {
        return;
    }
    if (!machine_.spreaderBusy() && machine_.hostOutputs().spreaderExtended)
    {
        if (point_stage_ == 0)
        {
            startForceTare(ProcedurePhase::InsertInitial);
        }
        else if (point_stage_ == 1)
        {
            // Ruheprobe already ran after AfterSeat retract, before this PreClamp/Clamp.
            // Taring again under spreader load zeros residual Gegenkraft and skews extract.
            std::fprintf(stderr,
                         "[Clamp] stage1 spreader ready → TestOrAge (no post-Clamp tare)\n");
            enterPhase(ProcedurePhase::TestOrAge);
        }
        else
        {
            abortProcess(AbortReason::ClampAfterTestStage);
        }
    }
}

void ProcedureController::handleInsertInitial()
{
    const float target = machine_.testConfiguration().target_force_N;
    const float fz = machine_.hostInputs().force.fz;
    const float search = machine_.machineConfig().force.insert_search_distance;

    if (fz >= target)
    {
        insertion_z_ = machine_.z();
        machine_.moveTo(point_x_, point_y_, insertion_z_); // hold seat pose
        if (test_or_age_standalone_)
        {
            std::fprintf(stderr,
                         "[InsertInitial] seat ok fz=%.3f target=%.3f insertion_z=%.3f → "
                         "TestOrAge manual (extract first)\n",
                         fz, target, insertion_z_);
            enterTestOrAgeAfterManualSeat();
            return;
        }
        unclamp_mode_ = UnclampMode::AfterSeat;
        std::fprintf(stderr,
                     "[InsertInitial] seat ok fz=%.3f target=%.3f insertion_z=%.3f → Unclamp AfterSeat "
                     "(tare after retract)\n",
                     fz, target, insertion_z_);
        // Do not tare under seat load — AfterSeat lifts first, then ForceTare.
        enterPhase(ProcedurePhase::Unclamp);
        return;
    }
    if (motionSettled() || zMinBlocked())
    {
        std::fprintf(stderr,
                     "[InsertInitial] ABORT settled/zMin fz=%.3f target=%.3f pose.z=%.3f "
                     "search=%.3f\n",
                     fz, target, machine_.z(), search);
        abortProcess(AbortReason::InsertInitialNoSeatForce);
    }
}

void ProcedureController::enterTestOrAgeAfterManualSeat()
{
    const MachineConfig::Motion& motion = machine_.machineConfig().motion;
    const float search = machine_.machineConfig().force.insert_search_distance;
    cycle_iteration_ = 0;
    cycle_leg_ = 2; // after seat: first counted motion is extract to kraftfrei
    force_path_.reset();
    force_path_.setIteration(0);
    force_path_.setLeg(ForcePathRecorder::kLegExtract);
    force_path_have_z_ = false;
    force_path_last_z_ = 0.0f;
    resetExtractSeek();
    ref_insert_valid_ = false;
    ref_extract_valid_ = false;
    point_x_ = machine_.x();
    point_y_ = machine_.y();
    machine_.setCollisionContext(CollisionContext::Measure);
    machine_.setActiveMotionProfile(motion.phase3_v_extract, motion.phase3_a, motion.phase3_corner);
    phase_ = ProcedurePhase::TestOrAge;
    std::fprintf(stderr,
                 "[TestOrAgeManual] after seat → extract +Z search=%.3f from z=%.3f\n", search,
                 machine_.z());
    machine_.moveTo(point_x_, point_y_, machine_.z() + search);
}

void ProcedureController::resetExtractSeek()
{
    extract_seen_tensile_ = false;
    extract_free_latched_ = false;
    extract_offset_target_z_ = 0.0f;
    insert_return_to_free_ = false;
}

void ProcedureController::startTestOrAgeInsertDive()
{
    const MachineConfig::Motion& motion = machine_.machineConfig().motion;
    const float search = machine_.machineConfig().force.insert_search_distance;
    insert_return_to_free_ = false;
    insert_fine_active_ = false;
    force_path_.setLeg(ForcePathRecorder::kLegInsert);
    force_path_have_z_ = false;
    machine_.setActiveMotionProfile(motion.phase3_v_insert, motion.phase3_a, motion.phase3_corner);
    machine_.setCollisionContext(CollisionContext::Measure);
    std::fprintf(stderr,
                 "[TestOrAge] insert dive −Z from z=%.3f search=%.3f at v=%.3f\n", machine_.z(),
                 search, motion.phase3_v_insert);
    machine_.moveTo(point_x_, point_y_, machine_.z() - search);
}

void ProcedureController::beginTestOrAgeInsert()
{
    const MachineConfig::Motion& motion = machine_.machineConfig().motion;
    const float offset_mm = machine_.machineConfig().force.extract_after_free_mm;
    cycle_leg_ = 1;
    machine_.setCollisionContext(CollisionContext::Measure);

    // Fast return over the after-free distance back to the Ausgliederpunkt.
    if (offset_mm > 0.0f && machine_.z() > force_free_z_ + 1.0e-3f)
    {
        insert_return_to_free_ = true;
        machine_.setActiveMotionProfile(motion.extract_after_free_v, motion.phase3_a,
                                        motion.phase3_corner);
        std::fprintf(stderr,
                     "[TestOrAge] return to kraftfrei z=%.3f at v=%.3f (then insert dive)\n",
                     force_free_z_, motion.extract_after_free_v);
        machine_.moveTo(point_x_, point_y_, force_free_z_);
        return;
    }

    startTestOrAgeInsertDive();
}

bool ProcedureController::tickExtractSeek(float fz, float free_tol)
{
    const float offset_mm = machine_.machineConfig().force.extract_after_free_mm;

    if (extract_free_latched_)
    {
        if (offset_mm <= 0.0f)
        {
            return true;
        }
        if (machine_.z() + 1.0e-3f >= extract_offset_target_z_ || motionSettled())
        {
            return true;
        }
        return false;
    }

    if (!extract_seen_tensile_)
    {
        if (fz < -free_tol)
        {
            extract_seen_tensile_ = true;
            std::fprintf(stderr, "[TestOrAge] extract Zug armed fz=%.3f z=%.3f\n", fz, machine_.z());
        }
        return false;
    }

    if (std::fabs(fz) > free_tol)
    {
        return false;
    }

    last_extract_x_ = machine_.x();
    last_extract_y_ = machine_.y();
    last_extract_z_ = machine_.z();
    extraction_z_ = last_extract_z_;
    force_free_z_ = last_extract_z_;
    extract_free_latched_ = true;
    std::fprintf(stderr, "[TestOrAge] kraftfrei (after Zug) z=%.3f offset_mm=%.3f\n", force_free_z_,
                 offset_mm);

    if (offset_mm <= 0.0f)
    {
        return true;
    }

    extract_offset_target_z_ = force_free_z_ + offset_mm;
    const MachineConfig::Motion& motion = machine_.machineConfig().motion;
    machine_.setActiveMotionProfile(motion.extract_after_free_v, motion.phase3_a,
                                    motion.phase3_corner);
    std::fprintf(stderr,
                 "[TestOrAge] after-free +Z to z=%.3f at v=%.3f (phase3 extract ended at kraftfrei)\n",
                 extract_offset_target_z_, motion.extract_after_free_v);
    machine_.moveTo(point_x_, point_y_, extract_offset_target_z_);
    return false;
}

void ProcedureController::completeTestOrAgeManual()
{
    machine_.moveTo(point_x_, point_y_, machine_.z());
    setAutomaticProcessGuards(false);
    machine_.setCollisionContext(CollisionContext::Approach);
    phase_ = ProcedurePhase::Completed;
    process_state_ = ProcessState::Completed;
    test_or_age_standalone_ = false;
    test_or_age_await_spreader_ = false;
    std::fprintf(stderr,
                 "[TestOrAgeManual] Completed at z=%.3f iterations done Homed kept\n", machine_.z());
}

void ProcedureController::handleTestOrAge()
{
    if (anomaly_report_.confirm_pending)
    {
        return;
    }

    const bool record = machine_.testConfiguration().mode == TestMode::Test;
    const float target_force = machine_.testConfiguration().target_force_N;
    // Leg 0 = opening extract to kraftfrei; not part of the evaluation curves.
    // Tag from cycle_leg_ at sample time: 1 = Eingliedern (−Z / Druck), 2 = Ausgliedern (+Z / Zug).
    // Skip recording while returning from after-free offset to kraftfrei.
    if (record && !insert_return_to_free_ && (cycle_leg_ == 1 || cycle_leg_ == 2))
    {
        force_path_.setLeg(cycle_leg_ == 1 ? ForcePathRecorder::kLegInsert
                                           : ForcePathRecorder::kLegExtract);
        const float z = machine_.z();
        const MachineConfig::Force& fcfg = machine_.machineConfig().force;
        float dz_gate = fcfg.force_path_dz_mm;
        if (cycle_leg_ == 1 && insert_fine_active_)
        {
            dz_gate = fcfg.force_path_dz_fine_mm;
        }
        const bool take = !force_path_have_z_ || dz_gate <= 0.0f ||
                          std::fabs(z - force_path_last_z_) + 1.0e-6f >= dz_gate;
        if (take)
        {
            force_path_.push(z, machine_.hostInputs().force.fz);
            force_path_have_z_ = true;
            force_path_last_z_ = z;
        }
    }
    if (checkTestOrAgeAnomalies())
    {
        return;
    }

    const MachineConfig::Motion& motion = machine_.machineConfig().motion;
    const MachineConfig::Force& force_cfg = machine_.machineConfig().force;
    const float target = target_force;
    const float free_tol = force_cfg.force_tolerance_N;
    const float fz = machine_.hostInputs().force.fz;
    const float search = force_cfg.insert_search_distance;

    if (cycle_leg_ == 0)
    {
        if (tickExtractSeek(fz, free_tol))
        {
            if (!ref_extract_valid_)
            {
                ref_extract_x_ = last_extract_x_;
                ref_extract_y_ = last_extract_y_;
                ref_extract_z_ = last_extract_z_;
                ref_extract_valid_ = true;
            }
            std::fprintf(stderr,
                         "[TestOrAge] kraftfrei z=%.3f → insert iter %u\n", force_free_z_,
                         cycle_iteration_);
            beginTestOrAgeInsert();
            return;
        }
        if (!extract_free_latched_ && (motionSettled() || zMinBlocked()))
        {
            abortProcess(extract_seen_tensile_
                             ? AbortReason::TestOrAgeExtractAfterZug
                             : AbortReason::TestOrAgeNoZugOnExtract);
        }
        return;
    }

    if (cycle_leg_ == 1)
    {
        if (insert_return_to_free_)
        {
            if (!motionSettled() && machine_.z() > force_free_z_ + 1.0e-3f)
            {
                return;
            }
            startTestOrAgeInsertDive();
            return;
        }
        // Fine phase: denser Kraft-Weg gate + slower insert near Sollkraft.
        const float near_frac = force_cfg.force_near_target_frac;
        if (!insert_fine_active_ && near_frac > 0.0f && near_frac <= 1.0f &&
            fz >= near_frac * target)
        {
            insert_fine_active_ = true;
            // Planner picks up new v/a on the remaining insert dive.
            machine_.setActiveMotionProfile(motion.phase3_v_insert_fine, motion.phase3_a,
                                            motion.phase3_corner);
            std::fprintf(stderr,
                         "[TestOrAge] insert fine phase fz=%.3f ≥ %.0f%% target → v=%.3f dz_fine=%.4f\n",
                         fz, near_frac * 100.0f, motion.phase3_v_insert_fine,
                         force_cfg.force_path_dz_fine_mm);
        }
        if (fz >= target)
        {
            last_insert_x_ = machine_.x();
            last_insert_y_ = machine_.y();
            last_insert_z_ = machine_.z();
            insertion_z_ = last_insert_z_;
            if (!ref_insert_valid_)
            {
                ref_insert_x_ = last_insert_x_;
                ref_insert_y_ = last_insert_y_;
                ref_insert_z_ = last_insert_z_;
                ref_insert_valid_ = true;
            }
            else
            {
                const float tol = force_cfg.coord_tolerance_mm;
                const float dz = std::fabs(last_insert_z_ - ref_insert_z_);
                if (dz > tol)
                {
                    raiseAnomaly(0, dz, tol);
                    return;
                }
            }
            if (checkTestOrAgeAnomalies())
            {
                return;
            }
            std::fprintf(stderr, "[TestOrAge] insert contact z=%.3f → extract\n", insertion_z_);
            // Shared seat sample: close insert and open extract at the same (z, Fz)
            // before +Z motion starts (avoids dz-gate / timing gap at the reverse point).
            if (record)
            {
                force_path_.setLeg(ForcePathRecorder::kLegInsert);
                force_path_.push(insertion_z_, fz);
                force_path_.setLeg(ForcePathRecorder::kLegExtract);
                force_path_.push(insertion_z_, fz);
                force_path_have_z_ = true;
                force_path_last_z_ = insertion_z_;
            }
            else
            {
                force_path_.setLeg(ForcePathRecorder::kLegExtract);
                force_path_have_z_ = false;
            }
            cycle_leg_ = 2;
            insert_fine_active_ = false;
            resetExtractSeek();
            machine_.setActiveMotionProfile(motion.phase3_v_extract, motion.phase3_a,
                                            motion.phase3_corner);
            machine_.setCollisionContext(CollisionContext::Measure);
            machine_.moveTo(point_x_, point_y_, machine_.z() + search);
            return;
        }
        if (motionSettled() || zMinBlocked())
        {
            abortProcess(AbortReason::TestOrAgeInsertNoTarget);
        }
        return;
    }

    if (cycle_leg_ == 2)
    {
        if (tickExtractSeek(fz, free_tol))
        {
            if (!ref_extract_valid_)
            {
                ref_extract_x_ = last_extract_x_;
                ref_extract_y_ = last_extract_y_;
                ref_extract_z_ = last_extract_z_;
                ref_extract_valid_ = true;
            }
            else if (cycle_iteration_ > 0)
            {
                const float tol = force_cfg.coord_tolerance_mm;
                const float dz = std::fabs(last_extract_z_ - ref_extract_z_);
                if (dz > tol)
                {
                    raiseAnomaly(0, dz, tol);
                    return;
                }
            }
            if (checkTestOrAgeAnomalies())
            {
                return;
            }
            ++cycle_iteration_;
            std::fprintf(stderr, "[TestOrAge] cycle %u/%u done extract\n", cycle_iteration_,
                         machine_.testConfiguration().iterations);
            if (cycle_iteration_ >= machine_.testConfiguration().iterations)
            {
                if (test_or_age_standalone_)
                {
                    completeTestOrAgeManual();
                    return;
                }
                unclamp_mode_ = UnclampMode::AfterTest;
                machine_.moveTo(point_x_, point_y_, machine_.z()); // hold end of extract
                startForceTare(ProcedurePhase::Unclamp);
                return;
            }
            force_path_.setIteration(static_cast<std::uint16_t>(cycle_iteration_));
            beginTestOrAgeInsert();
            return;
        }
        if (!extract_free_latched_ && motionSettled())
        {
            abortProcess(extract_seen_tensile_
                             ? AbortReason::TestOrAgeExtractAfterZug
                             : AbortReason::TestOrAgeNoZugOnExtract);
        }
        return;
    }
}

void ProcedureController::handleUnclamp()
{
    const MachineConfig::Motion& motion = machine_.machineConfig().motion;
    const float standoff = machine_.machineConfig().probe.approach_distance;
    const float buffer = machine_.machineConfig().force.insertion_buffer;
    const float target = machine_.testConfiguration().target_force_N;
    const float fz = machine_.hostInputs().force.fz;

    if (unclamp_mode_ == UnclampMode::AfterSeat)
    {
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
            const float retract_mm = machine_.machineConfig().force.after_seat_retract_mm;
            if (retract_mm > 0.0f)
            {
                const float retract_z = insertion_z_ + retract_mm;
                machine_.setActiveMotionProfile(motion.v_unclamp, motion.a_testpoints,
                                                motion.corner_testpoints);
                machine_.setCollisionContext(CollisionContext::Retract);
                std::fprintf(stderr,
                             "[Unclamp] AfterSeat lift insertion_z=%.3f + retract_mm=%.3f → z=%.3f "
                             "(not program+standoff)\n",
                             insertion_z_, retract_mm, retract_z);
                commandLowerToZ(retract_z);
            }
            else
            {
                std::fprintf(stderr,
                             "[Unclamp] AfterSeat retract_mm=0 → no lift, tare at insertion_z=%.3f\n",
                             insertion_z_);
            }
            cycle_leg_ = 2;
            return;
        }
        if (cycle_leg_ == 2)
        {
            if (!motionSettled())
            {
                return;
            }
            // Load-free tare at the fixed retract height, then second PreClamp here.
            point_stage_ = 1;
            startForceTare(ProcedurePhase::PreClampXyCorrect);
            return;
        }
        return;
    }

    if (cycle_leg_ == 0)
    {
        if (insert_return_to_free_)
        {
            if (!motionSettled() && machine_.z() > force_free_z_ + 1.0e-3f)
            {
                return;
            }
            insert_return_to_free_ = false;
            machine_.setActiveMotionProfile(motion.phase1_v_insert, motion.phase1_a,
                                            motion.phase1_corner);
            machine_.setCollisionContext(CollisionContext::Contact);
            const float search = machine_.machineConfig().force.insert_search_distance;
            std::fprintf(stderr,
                         "[Unclamp] AfterTest re-seat dive from kraftfrei z=%.3f at v=%.3f\n",
                         machine_.z(), motion.phase1_v_insert);
            machine_.moveTo(point_x_, point_y_, machine_.z() - search);
            return;
        }
        if (fz >= target)
        {
            insertion_z_ = machine_.z();
            machine_.moveTo(point_x_, point_y_, insertion_z_);
            cycle_leg_ = 1;
            return;
        }
        if (motionSettled() || zMinBlocked())
        {
            abortProcess(AbortReason::UnclampAfterTestReseatFailed);
        }
        return;
    }
    if (cycle_leg_ == 1)
    {
        machine_.setSpreaderExtend(false);
        cycle_leg_ = 2;
        return;
    }
    if (cycle_leg_ == 2)
    {
        if (machine_.spreaderBusy() || machine_.hostOutputs().spreaderExtended)
        {
            return;
        }
        const float fine_z = std::min(insertion_z_ + buffer, target_machine_.z + standoff);
        machine_.setActiveMotionProfile(motion.v_fine_testpoints, motion.a_testpoints,
                                        motion.corner_testpoints);
        machine_.setCollisionContext(CollisionContext::Retract);
        commandLowerToZ(fine_z);
        cycle_leg_ = 3;
        return;
    }
    if (cycle_leg_ == 3)
    {
        if (!motionSettled())
        {
            return;
        }
        machine_.setActiveMotionProfile(motion.v_approach_testpoints, motion.a_testpoints,
                                        motion.corner_testpoints);
        machine_.setCollisionContext(CollisionContext::Approach);
        commandRaiseSafeZ();
        cycle_leg_ = 4;
        return;
    }
    if (cycle_leg_ == 4)
    {
        if (!motionSettled())
        {
            return;
        }
        if (!atSafeZ())
        {
            abortProcess(AbortReason::UnclampAfterTestSafeZ);
            return;
        }
        point_stage_ = 2;
        enterPhase(ProcedurePhase::ProbeRemeasure);
        return;
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

const ProcedureController::AnomalyReport& ProcedureController::anomalyReport() const
{
    return anomaly_report_;
}

void ProcedureController::consumeAnomalyReport()
{
    anomaly_report_.fresh = false;
}

void ProcedureController::raiseAnomaly(uint8_t type, float measured, float limit)
{
    const HostInputs& in = machine_.hostInputs();
    anomaly_report_ = AnomalyReport{};
    anomaly_report_.fresh = true;
    anomaly_report_.type = type;
    anomaly_report_.point_index = static_cast<uint8_t>(current_index_);
    anomaly_report_.iteration = static_cast<uint16_t>(cycle_iteration_);
    anomaly_report_.measured = measured;
    anomaly_report_.limit = limit;
    anomaly_report_.z = machine_.z();
    anomaly_report_.fx = in.force.fx;
    anomaly_report_.fy = in.force.fy;

    std::fprintf(stderr,
                 "[Anomaly] type=%u measured=%.3f limit=%.3f z=%.3f fx=%.3f fy=%.3f skip=%d\n",
                 static_cast<unsigned>(type), measured, limit, anomaly_report_.z, anomaly_report_.fx,
                 anomaly_report_.fy, machine_.machineConfig().force.anomaly_skip ? 1 : 0);

    if (machine_.machineConfig().force.anomaly_skip)
    {
        anomaly_report_.skip_applied = true;
        anomaly_report_.confirm_pending = false;
        beginAnomalyExit(AnomalyExitAction::NextPoint);
        return;
    }
    anomaly_report_.confirm_pending = true;
    anomaly_report_.skip_applied = false;
    machine_.pauseHolding();
}

bool ProcedureController::checkTestOrAgeAnomalies()
{
    if (anomaly_report_.confirm_pending)
    {
        return true;
    }
    const MachineConfig::Force& force_cfg = machine_.machineConfig().force;
    const HostInputs& in = machine_.hostInputs();
    const float lat = force_cfg.test_lateral_force_N;
    if (std::fabs(in.force.fx) > lat)
    {
        raiseAnomaly(1, std::fabs(in.force.fx), lat);
        return true;
    }
    if (std::fabs(in.force.fy) > lat)
    {
        raiseAnomaly(1, std::fabs(in.force.fy), lat);
        return true;
    }
    const float z_lo = target_machine_.z - force_cfg.z_limit_neg_mm;
    const float z_hi = target_machine_.z + force_cfg.z_limit_pos_mm;
    if (machine_.z() < z_lo)
    {
        raiseAnomaly(2, machine_.z() - target_machine_.z, force_cfg.z_limit_neg_mm);
        return true;
    }
    if (machine_.z() > z_hi)
    {
        raiseAnomaly(2, machine_.z() - target_machine_.z, force_cfg.z_limit_pos_mm);
        return true;
    }
    return false;
}

void ProcedureController::beginAnomalyExit(AnomalyExitAction action)
{
    anomaly_exit_pending_ = true;
    anomaly_exit_action_ = action;
    anomaly_exit_leg_ = 0;
    if (machine_.machineState() == MachineState::Paused)
    {
        machine_.resume();
    }
    // Stop current trajectory without Process abort / NeedsHoming.
    machine_.pauseHolding();
    if (machine_.machineState() == MachineState::Paused)
    {
        machine_.resume();
    }
}

void ProcedureController::applyProcedureDecision(uint8_t action)
{
    if (!anomaly_report_.confirm_pending)
    {
        return;
    }

    const uint8_t type = anomaly_report_.type;
    anomaly_report_.confirm_pending = false;
    anomaly_report_.fresh = true;

    if (machine_.machineState() == MachineState::Paused)
    {
        machine_.resume();
    }

    if (action == 0)
    {
        if (type == 3)
        {
            beginAnomalyExit(AnomalyExitAction::NextPoint);
            return;
        }
        return;
    }
    if (action == 1)
    {
        beginAnomalyExit(AnomalyExitAction::NextPoint);
        return;
    }
    if (action == 2)
    {
        beginAnomalyExit(AnomalyExitAction::RepeatPoint);
        return;
    }
}

void ProcedureController::tickAnomalyExit()
{
    const MachineConfig::Motion& motion = machine_.machineConfig().motion;

    if (machine_.machineState() == MachineState::Stopped ||
        machine_.machineState() == MachineState::Fault)
    {
        anomaly_exit_pending_ = false;
        return;
    }

    if (anomaly_exit_leg_ == 0)
    {
        if (machine_.hostOutputs().spreaderExtended || machine_.spreaderBusy())
        {
            machine_.setSpreaderExtend(false);
            anomaly_exit_leg_ = 1;
            return;
        }
        anomaly_exit_leg_ = 2;
    }
    if (anomaly_exit_leg_ == 1)
    {
        if (machine_.spreaderBusy() || machine_.hostOutputs().spreaderExtended)
        {
            return;
        }
        anomaly_exit_leg_ = 2;
    }
    if (anomaly_exit_leg_ == 2)
    {
        machine_.setActiveMotionProfile(motion.v_approach_testpoints, motion.a_testpoints,
                                        motion.corner_testpoints);
        machine_.setCollisionContext(CollisionContext::Approach);
        commandRaiseSafeZ();
        anomaly_exit_leg_ = 3;
        return;
    }
    if (anomaly_exit_leg_ == 3)
    {
        if (!motionSettled())
        {
            return;
        }
        finishAnomalyExit();
    }
}

void ProcedureController::finishAnomalyExit()
{
    anomaly_exit_pending_ = false;
    anomaly_exit_leg_ = 0;
    const AnomalyExitAction action = anomaly_exit_action_;
    anomaly_exit_action_ = AnomalyExitAction::None;
    anomaly_report_.confirm_pending = false;

    if (action == AnomalyExitAction::RepeatPoint)
    {
        point_stage_ = 0;
        enterPhase(ProcedurePhase::PointApproach);
        return;
    }
    handleNextPoint();
}

void ProcedureController::handleProbeRemeasure(float dt)
{
    if (anomaly_report_.confirm_pending)
    {
        return;
    }

    const CalibrationSet& calib = machine_.calibrationSet();
    const MachineConfig& cfg = machine_.machineConfig();
    const MachineConfig::Probe& probe = cfg.probe;
    const MachineConfig::Motion& motion = cfg.motion;
    const TestPoint target{calib.probe_center.x - calib.tool_offset.x,
                           calib.probe_center.y - calib.tool_offset.y,
                           calib.probe_center.z - calib.tool_offset.z};
    const float standoff = probe.approach_distance;
    const float coarse_z = target.z + standoff;

    if (probe_await_spreader_retract_)
    {
        if (!awaitSpreaderRetracted())
        {
            return;
        }
        probe_await_spreader_retract_ = false;
        const TestPoint& now = machine_.calibrationSet().tool_offset;
        const float dx = now.x - tool_offset_before_remeasure_.x;
        const float dy = now.y - tool_offset_before_remeasure_.y;
        const float dz = now.z - tool_offset_before_remeasure_.z;
        const float delta = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float tol = machine_.machineConfig().force.tool_offset_change_tolerance_mm;
        std::fprintf(stderr, "[ProbeRemeasure] OffsetOnly done delta=%.3f tol=%.3f\n", delta, tol);
        if (delta > tol)
        {
            raiseAnomaly(3, delta, tol);
            return;
        }
        handleNextPoint();
        return;
    }

    if (probe_remeasure_calib_active_)
    {
        probe_calib_.tick(dt, machine_);
        if (probe_calib_.isComplete())
        {
            probe_remeasure_calib_active_ = false;
            probe_await_spreader_retract_ = true;
            machine_.setSpreaderExtend(false);
            return;
        }
        if (probe_calib_.isError())
        {
            probe_remeasure_calib_active_ = false;
            abortProcess(AbortReason::ProbeRemeasureOffsetOnlyError);
        }
        return;
    }

    if (approach_leg_ == 2)
    {
        if (!motionSettled())
        {
            return;
        }
        if (machine_.spreaderBusy() || !machine_.hostOutputs().spreaderExtended)
        {
            machine_.setSpreaderExtend(true);
            return;
        }
        startForceTareBeforeProbeBore(ProbeCalibration::StorePolicy::OffsetOnly, true);
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
            abortProcess(AbortReason::ProbeRemeasureNotAtSafeZ);
            return;
        }
        machine_.setActiveMotionProfile(probe.v_approach, motion.a_testpoints, motion.corner_testpoints);
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
            abortProcess(AbortReason::ProbeRemeasureXyMiss);
            return;
        }
        machine_.setActiveMotionProfile(probe.v_approach, motion.a_testpoints, motion.corner_testpoints);
        commandLowerToZ(coarse_z);
        approach_leg_ = 2;
        return;
    }
}
