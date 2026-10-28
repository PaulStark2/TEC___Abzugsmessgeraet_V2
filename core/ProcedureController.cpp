#include "ProcedureController.hpp"

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
    probe_calib_standalone_ = false;
    plate_bore_standalone_ = false;
    plate_bore_point_id_ = 0;
    fine_approach_after_tare_ = false;
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
    plate_bore_standalone_ = false;
    phase_ = ProcedurePhase::ProbeCalibrate;
    return true;
}

bool ProcedureController::startPlateBoreSearch(std::uint8_t point_id)
{
    if (process_state_ == ProcessState::Running)
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
    const MachineConfig& cfg = machine_.machineConfig();
    const MachineConfig::PlateCalib& pc = cfg.plate_calib;
    BoreSearch::Params params{};
    params.wall_force_N = pc.wall_force_N;
    params.floor_force_N = pc.floor_force_N;
    params.xy_search_limit = pc.xy_search_limit;
    params.z_search_limit = pc.z_search_limit;
    params.z_lift_mm = pc.z_lift_mm;
    params.timeout_s = pc.timeout_s;
    params.refine_passes = pc.refine_passes;
    params.do_floor = true;
    machine_.setCollisionContext(CollisionContext::Contact);
    machine_.setActiveMotionProfile(pc.v_plate_bore_search, cfg.motion.a_testpoints,
                                    cfg.motion.corner_testpoints);
    machine_.setPreClampXyEnabled(false);
    if (!bore_search_.start(machine_, params))
    {
        return false;
    }
    process_state_ = ProcessState::Running;
    setAutomaticProcessGuards(true);
    probe_calib_standalone_ = false;
    plate_bore_standalone_ = true;
    plate_bore_point_id_ = point_id;
    preclamp_await_confirm_ = false;
    phase_ = ProcedurePhase::PlateBoreSearch;
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
        point_stage_ = 0;
        approach_leg_ = 0;
        commandRaiseSafeZ();
        break;
    case ProcedurePhase::PreClampXyCorrect:
    {
        machine_.setCollisionContext(CollisionContext::Contact);
        machine_.setActiveMotionProfile(motion.v_bore_search, motion.a_testpoints,
                                        motion.corner_testpoints);
        machine_.setPreClampXyEnabled(false);
        preclamp_await_confirm_ = false;
        const MachineConfig::Force& force_cfg = machine_.machineConfig().force;
        BoreSearch::Params params{};
        params.wall_force_N = force_cfg.xy_tolerance_N;
        params.xy_search_limit = force_cfg.xy_search_limit;
        params.timeout_s = force_cfg.xy_correct_timeout_s;
        params.refine_passes = force_cfg.bore_refine_passes;
        params.do_floor = false;
        if (!bore_search_.start(machine_, params))
        {
            abortProcess("preclamp bore search start failed");
            return;
        }
        machine_.moveTo(machine_.x(), machine_.y(), machine_.z());
        break;
    }
    case ProcedurePhase::Clamp:
        machine_.setPreClampXyEnabled(false);
        machine_.setCollisionContext(CollisionContext::Clamp);
        machine_.setSpreaderExtend(true);
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
                         "[TestOrAge] start extract +Z looking for kraftfrei from z=%.3f search=%.3f\n",
                         machine_.z(), search);
            machine_.moveTo(point_x_, point_y_, machine_.z() + search);
        }
        break;
    case ProcedurePhase::Unclamp:
        machine_.setPreClampXyEnabled(false);
        machine_.setCollisionContext(CollisionContext::Unclamp);
        cycle_leg_ = 0;
        point_x_ = machine_.x();
        point_y_ = machine_.y();
        if (unclamp_mode_ == UnclampMode::AfterSeat)
        {
            point_stage_ = 1;
            machine_.setActiveMotionProfile(motion.v_unclamp, motion.a_testpoints,
                                            motion.corner_testpoints);
            std::fprintf(stderr, "[Unclamp] AfterSeat at z=%.3f\n", machine_.z());
        }
        else
        {
            machine_.setActiveMotionProfile(motion.phase1_v_insert, motion.phase1_a,
                                            motion.phase1_corner);
            machine_.setCollisionContext(CollisionContext::Contact);
            const float search = machine_.machineConfig().force.insert_search_distance;
            std::fprintf(stderr, "[Unclamp] AfterTest re-seat dive from z=%.3f\n", machine_.z());
            machine_.moveTo(point_x_, point_y_, machine_.z() - search);
        }
        break;
    case ProcedurePhase::ProbeRemeasure:
    {
        const CalibrationSet& calib = machine_.calibrationSet();
        const MachineConfig::Probe& probe = machine_.machineConfig().probe;
        tool_offset_before_remeasure_ = calib.tool_offset;
        probe_remeasure_calib_active_ = false;
        approach_leg_ = 0;
        machine_.setCollisionContext(CollisionContext::Approach);
        machine_.setActiveMotionProfile(probe.v_approach_probe, probe.a_probe_transfer,
                                        probe.corner_probe_transfer);
        std::fprintf(stderr,
                     "[ProbeRemeasure] enter at Safe-Z pose=(%.3f,%.3f,%.3f) prev_offset=(%.3f,%.3f,%.3f)\n",
                     machine_.x(), machine_.y(), machine_.z(), calib.tool_offset.x, calib.tool_offset.y,
                     calib.tool_offset.z);
        break;
    }
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
    // Formula 2: last tip-on-disc TCP pose (OffsetOnly measured).
    // target = probe_center - tool_offset (= measured); not toMachinePoint(+offset).
    const TestPoint target{calib.probe_center.x - calib.tool_offset.x,
                           calib.probe_center.y - calib.tool_offset.y,
                           calib.probe_center.z - calib.tool_offset.z};
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
                         "target_hub_z=%.3f probe_center.z=%.3f tool_offset.z=%.3f — abort\n",
                         contact.x, contact.y, contact.z, coarse_z, calib.probe_center.z,
                         calib.tool_offset.z);
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
                     "target=(%.3f,%.3f,%.3f) tool_offset=(%.3f,%.3f,%.3f) v_search=%.3f\n",
                     machine_.x(), machine_.y(), machine_.z(), target.x, target.y, target.z,
                     calib.tool_offset.x, calib.tool_offset.y, calib.tool_offset.z,
                     probe.v_search);
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
                     "[ProbeApproach] XY v_approach_probe=%.3f → (%.3f,%.3f) @ Safe-Z "
                     "(probe_center-tool_offset)\n",
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
            abortProcess("ProbeApproach: XY not at probe center - tool_offset before Z descent");
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
                     "[ProbeApproach] COARSE z=%.3f (target.z=%.3f + standoff=%.3f) "
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
    bore_search_.tick(dt, machine_);
    if (bore_search_.isError())
    {
        switch (bore_search_.errorReason())
        {
        case BoreSearch::ErrorReason::Timeout:
            abortProcess("plate bore search timeout");
            break;
        case BoreSearch::ErrorReason::WallNotFound:
            abortProcess("plate bore wall not found");
            break;
        case BoreSearch::ErrorReason::FloorNotFound:
            abortProcess("plate bore floor not found");
            break;
        default:
            abortProcess("plate bore search failed");
            break;
        }
        return;
    }
    if (!bore_search_.isComplete())
    {
        return;
    }
    publishBoreReport(bore_search_.result(), false);
    machine_.stop();
    setAutomaticProcessGuards(false);
    phase_ = ProcedurePhase::Completed;
    process_state_ = ProcessState::Completed;
    plate_bore_standalone_ = false;
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

    bore_search_.tick(dt, machine_);

    if (bore_search_.isError())
    {
        switch (bore_search_.errorReason())
        {
        case BoreSearch::ErrorReason::Timeout:
            abortProcess("preclamp bore search timeout");
            break;
        case BoreSearch::ErrorReason::WallNotFound:
            abortProcess("preclamp wall not found");
            break;
        default:
            abortProcess("preclamp bore search failed");
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
            startForceTare(ProcedurePhase::TestOrAge);
        }
        else
        {
            abortProcess("Clamp after test stage");
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
        unclamp_mode_ = UnclampMode::AfterSeat;
        std::fprintf(stderr,
                     "[InsertInitial] seat ok fz=%.3f target=%.3f insertion_z=%.3f → Unclamp AfterSeat\n",
                     fz, target, insertion_z_);
        startForceTare(ProcedurePhase::Unclamp);
        return;
    }
    if (motionSettled() || zMinBlocked())
    {
        std::fprintf(stderr,
                     "[InsertInitial] ABORT settled/zMin fz=%.3f target=%.3f pose.z=%.3f "
                     "search=%.3f\n",
                     fz, target, machine_.z(), search);
        abortProcess("InsertInitial: no seat force");
    }
}

void ProcedureController::handleTestOrAge()
{
    if (anomaly_report_.confirm_pending)
    {
        return;
    }

    const bool record = machine_.testConfiguration().mode == TestMode::Test;
    // Leg 0 = opening extract to kraftfrei; not part of the evaluation curves.
    if (record && cycle_leg_ != 0)
    {
        const float z = machine_.z();
        const float dz_gate = machine_.machineConfig().force.force_path_dz_mm;
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
    const float target = machine_.testConfiguration().target_force_N;
    const float free_tol = force_cfg.force_tolerance_N;
    const float fz = machine_.hostInputs().force.fz;
    const float search = force_cfg.insert_search_distance;

    if (cycle_leg_ == 0)
    {
        if (std::fabs(fz) <= free_tol)
        {
            force_free_z_ = machine_.z();
            extraction_z_ = force_free_z_;
            last_extract_x_ = machine_.x();
            last_extract_y_ = machine_.y();
            last_extract_z_ = machine_.z();
            if (!ref_extract_valid_)
            {
                ref_extract_x_ = last_extract_x_;
                ref_extract_y_ = last_extract_y_;
                ref_extract_z_ = last_extract_z_;
                ref_extract_valid_ = true;
            }
            std::fprintf(stderr,
                         "[TestOrAge] kraftfrei z=%.3f → insert dive iter %u\n", force_free_z_,
                         cycle_iteration_);
            cycle_leg_ = 1;
            force_path_.setLeg(ForcePathRecorder::kLegInsert);
            force_path_have_z_ = false;
            machine_.setActiveMotionProfile(motion.phase3_v_insert, motion.phase3_a,
                                            motion.phase3_corner);
            machine_.setCollisionContext(CollisionContext::Contact);
            machine_.moveTo(point_x_, point_y_, machine_.z() - search);
            return;
        }
        if (motionSettled() || zMinBlocked())
        {
            abortProcess("TestOrAge: no kraftfrei on extract");
        }
        return;
    }

    if (cycle_leg_ == 1)
    {
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
            cycle_leg_ = 2;
            force_path_.setLeg(ForcePathRecorder::kLegExtract);
            force_path_have_z_ = false;
            machine_.setActiveMotionProfile(motion.phase3_v_extract, motion.phase3_a,
                                            motion.phase3_corner);
            machine_.setCollisionContext(CollisionContext::Retract);
            machine_.moveTo(point_x_, point_y_, machine_.z() + search);
            return;
        }
        if (motionSettled() || zMinBlocked())
        {
            abortProcess("TestOrAge: insert without target force");
        }
        return;
    }

    if (cycle_leg_ == 2)
    {
        if (std::fabs(fz) <= free_tol)
        {
            last_extract_x_ = machine_.x();
            last_extract_y_ = machine_.y();
            last_extract_z_ = machine_.z();
            extraction_z_ = last_extract_z_;
            force_free_z_ = last_extract_z_;
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
                unclamp_mode_ = UnclampMode::AfterTest;
                machine_.moveTo(point_x_, point_y_, machine_.z()); // hold kraftfrei
                startForceTare(ProcedurePhase::Unclamp);
                return;
            }
            force_path_.setIteration(static_cast<std::uint16_t>(cycle_iteration_));
            force_path_.setLeg(ForcePathRecorder::kLegInsert);
            force_path_have_z_ = false;
            cycle_leg_ = 1;
            machine_.setActiveMotionProfile(motion.phase3_v_insert, motion.phase3_a,
                                            motion.phase3_corner);
            machine_.setCollisionContext(CollisionContext::Contact);
            machine_.moveTo(point_x_, point_y_, machine_.z() - search);
            return;
        }
        if (motionSettled())
        {
            abortProcess("TestOrAge: extract without kraftfrei");
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
            machine_.setActiveMotionProfile(motion.v_unclamp, motion.a_testpoints,
                                            motion.corner_testpoints);
            machine_.setCollisionContext(CollisionContext::Retract);
            commandLowerToZ(target_machine_.z + standoff);
            cycle_leg_ = 2;
            return;
        }
        if (cycle_leg_ == 2)
        {
            if (!motionSettled())
            {
                return;
            }
            machine_.setActiveMotionProfile(motion.v_fine_testpoints, motion.a_testpoints,
                                            motion.corner_testpoints);
            machine_.setCollisionContext(CollisionContext::Contact);
            commandLowerToZ(target_machine_.z);
            cycle_leg_ = 3;
            return;
        }
        if (cycle_leg_ == 3)
        {
            if (!motionSettled())
            {
                return;
            }
            point_stage_ = 1;
            enterPhase(ProcedurePhase::PreClampXyCorrect);
            return;
        }
        return;
    }

    if (cycle_leg_ == 0)
    {
        if (fz >= target)
        {
            insertion_z_ = machine_.z();
            machine_.moveTo(point_x_, point_y_, insertion_z_);
            cycle_leg_ = 1;
            return;
        }
        if (motionSettled() || zMinBlocked())
        {
            abortProcess("Unclamp AfterTest: re-seat failed");
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
            abortProcess("Unclamp AfterTest: Safe-Z not reached");
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
    const MachineConfig::Probe& probe = machine_.machineConfig().probe;
    const TestPoint target{calib.probe_center.x - calib.tool_offset.x,
                           calib.probe_center.y - calib.tool_offset.y,
                           calib.probe_center.z - calib.tool_offset.z};
    const float standoff = probe.approach_distance;
    const float coarse_z = target.z + standoff;

    if (probe_remeasure_calib_active_)
    {
        probe_calib_.tick(dt, machine_);
        if (probe_calib_.isComplete())
        {
            probe_remeasure_calib_active_ = false;
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
        if (probe_calib_.isError())
        {
            probe_remeasure_calib_active_ = false;
            abortProcess("ProbeRemeasure OffsetOnly error");
        }
        return;
    }

    if (approach_leg_ == 2)
    {
        TestPoint contact{};
        if (machine_.consumeProbeTrigger(contact))
        {
            machine_.brakeToStop(probe.a_probe_brake);
            machine_.latchFaultReason(FaultReason::ProbeApproachContact);
            abortProcess("ProbeRemeasure: probe contact during coarse Z");
            return;
        }
        if (!motionSettled())
        {
            return;
        }
        machine_.consumeProbeTrigger();
        const TestPoint hub{machine_.x(), machine_.y(), machine_.z()};
        if (!probe_calib_.start(machine_, hub, ProbeCalibration::StorePolicy::OffsetOnly))
        {
            abortProcess("ProbeRemeasure: OffsetOnly start failed");
            return;
        }
        probe_remeasure_calib_active_ = true;
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
            abortProcess("ProbeRemeasure: not at Safe-Z");
            return;
        }
        machine_.setActiveMotionProfile(probe.v_approach_probe, probe.a_probe_transfer,
                                        probe.corner_probe_transfer);
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
            abortProcess("ProbeRemeasure: XY miss");
            return;
        }
        machine_.consumeProbeTrigger();
        machine_.setActiveMotionProfile(probe.v_approach_probe, probe.a_probe_transfer,
                                        probe.corner_probe_transfer);
        commandLowerToZ(coarse_z);
        approach_leg_ = 2;
        return;
    }
}
