#pragma once

#include "CoordinateTransform.hpp"
#include "ForcePathRecorder.hpp"
#include "Machine.hpp"
#include "ProbeCalibration.hpp"
#include "TestPoint.hpp"
#include "TestProgram.hpp"
#include "Types.hpp"

#include <cstddef>
#include <cstdint>

// Single process state machine for the automated test/aging cycle.
class ProcedureController
{
public:
    explicit ProcedureController(Machine& machine);

    void setProgram(const TestProgram& program);
    bool start();
    bool startProbeCalibration();
    void tick(float dt);
    bool pause();
    void resume();
    void stop();

    ProcessState processState() const;
    ProcedurePhase phase() const;
    bool pauseAllowed() const;
    std::size_t currentIndex() const;
    bool currentPoint(TestPoint& out) const;
    float insertionZ() const;
    float extractionZ() const;
    float forceFreeZ() const;
    unsigned cycleLeg() const;
    ForcePathRecorder& forcePath();
    const ForcePathRecorder& forcePath() const;
    unsigned testIteration() const;

    ProbeCalibration& probeCalibration();

private:
    bool motionSettled() const;
    bool zMinBlocked() const;
    bool atSafeZ() const;
    bool resolveCurrentMachinePoint();
    bool resolveSafeZMachine(float& out_safe_z_machine) const;
    bool programPointsBelowSafeZ() const;
    void enterPhase(ProcedurePhase phase);
    void commandRaiseSafeZ();
    void commandTraverseXY(float x, float y);
    void commandLowerToZ(float z);
    void abortProcess(const char* reason = "unspecified");
    void setAutomaticProcessGuards(bool active);
    void handleMoveSafePose();
    void handleProbeApproach();
    void handleProbeCalibrate(float dt);
    void handlePointApproach();
    void handlePreClamp(float dt);
    void handleClamp();
    void handleInsertInitial();
    void handleOptimizeInsert();
    void handleTestOrAge();
    void handleUnclamp();
    void handleNextPoint();
    void handleMoveSafeEnd();
    void startForceTare(ProcedurePhase next);
    void startForceTareBeforeFineApproach();
    void commandFineApproach();
    void handleForceTare();

    Machine& machine_;
    TestProgram program_;
    ProcessState process_state_;
    ProcedurePhase phase_;
    std::size_t current_index_;
    ProbeCalibration probe_calib_;
    ForcePathRecorder force_path_;
    float insertion_z_;
    float extraction_z_;
    float force_free_z_;
    float point_x_;
    float point_y_;
    unsigned optimize_attempts_;
    unsigned cycle_iteration_;
    uint8_t cycle_leg_; // phase-specific leg (InsertInitial unload/insert; Optimize/Test; Unclamp)
    uint8_t approach_leg_; // PointApproach: 0 raise, 1 XY, 2 coarse Z, 3 fine Z; MoveSafeEnd: 0 raise, 2 homing
    TestPoint target_machine_;
    float end_x_;
    float end_y_;
    float preclamp_elapsed_s_;
    int8_t phase2_last_error_sign_;
    unsigned phase2_sign_flips_;
    float phase2_measured_fz_; // Fz latched at insertion_z after insert leg
    float phase2_motion_fz_; // last Fz observed while inserting
    bool probe_calib_standalone_;
    ProcedurePhase tare_next_;
    // Coarse Z has stopped. After ForceTare, resume PointApproach leg 3
    // instead of enterPhase(tare_next_), which would restart the approach.
    bool fine_approach_after_tare_;
};
