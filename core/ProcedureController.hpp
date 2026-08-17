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
    ForcePathRecorder& forcePath();
    const ForcePathRecorder& forcePath() const;
    unsigned testIteration() const;

    ProbeCalibration& probeCalibration();

private:
    bool motionSettled() const;
    bool zMinBlocked() const;
    void enterPhase(ProcedurePhase phase);
    void commandSafePose();
    void commandCurrentPlatePoint();
    void abortProcess();
    void handleMoveSafePose();
    void handleProbeCalibrate();
    void handlePointApproach();
    void handlePreClamp();
    void handleClamp();
    void handleInsertInitial();
    void handleOptimizeInsert();
    void handleTestOrAge();
    void handleUnclamp();
    void handleNextPoint();
    void handleMoveSafeEnd();

    Machine& machine_;
    TestProgram program_;
    ProcessState process_state_;
    ProcedurePhase phase_;
    std::size_t current_index_;
    ProbeCalibration probe_calib_;
    ForcePathRecorder force_path_;
    float insertion_z_;
    float extraction_z_;
    float point_x_;
    float point_y_;
    unsigned optimize_attempts_;
    unsigned cycle_iteration_;
    uint8_t cycle_leg_; // 0 = at insertion, 1 = at extraction, 2 = extract then TestOrAge
};
