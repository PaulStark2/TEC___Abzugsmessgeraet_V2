#pragma once

#include "AbortReason.hpp"
#include "BorePoint.hpp"
#include "BoreSearch.hpp"
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
    // Handmodus: tare → SeatToForce → TestOrAge iterations from current pose (no PreClamp/program).
    bool startTestOrAgeManual();
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

    // Last two-chord center. fresh until the bridge has copied it into MsgBoreCenter.
    struct BoreReport
    {
        bool fresh = false;
        bool anomaly = false;
        bool confirm_pending = false;
        float cx = 0.0f;
        float cy = 0.0f;
        float cz = 0.0f;
        float radius = 0.0f;
        float residual = 0.0f;
        float fx = 0.0f;
        float fy = 0.0f;
        float x[4] = {};
        float y[4] = {};
        std::uint8_t point_id = 0; // 0=PreClamp, 1=KM, 2=K1, 3=K2, 4=K3
    };

    const BoreReport& boreReport() const;
    void consumeBoreReport();

    // Standalone plate bore teach (z_first Einmess protocol). point_id: 1=KM … 4=K3.
    bool startPlateBoreSearch(std::uint8_t point_id);

    // Teach Anfahrt of a stored calib pose. point_id: 1=KM … 4=K3, 5=probe.
    bool startApproachCalibPoint(std::uint8_t point_id);

    // Procedure anomaly (CoordDrift / LateralForce / ZTravelLimit / TcpHang).
    struct AnomalyReport
    {
        bool fresh = false;
        bool confirm_pending = false;
        bool skip_applied = false;
        uint8_t type = 0; // 0=CoordDrift, 1=LateralForce, 2=ZTravelLimit, 3=TcpHang
        uint8_t point_index = 0;
        uint16_t iteration = 0;
        float measured = 0.0f;
        float limit = 0.0f;
        float z = 0.0f;
        float fx = 0.0f;
        float fy = 0.0f;
    };

    const AnomalyReport& anomalyReport() const;
    void consumeAnomalyReport();
    // action: 0=Continue, 1=NextPoint, 2=RepeatPoint
    void applyProcedureDecision(uint8_t action);

private:
    enum class UnclampMode : uint8_t
    {
        AfterSeat = 0,
        AfterTest = 1
    };

    enum class AnomalyExitAction : uint8_t
    {
        None = 0,
        NextPoint = 1,
        RepeatPoint = 2
    };

    bool motionSettled() const;
    bool zMinBlocked() const;
    bool atSafeZ() const;
    bool atCalibSafeZ() const;
    bool resolveCurrentMachinePoint();
    bool resolveSafeZMachine(float& out_safe_z_machine) const;
    bool resolveCalibApproachTarget(std::uint8_t point_id, TestPoint& out) const;
    bool programPointsBelowSafeZ() const;
    void enterPhase(ProcedurePhase phase);
    void commandRaiseSafeZ();
    void commandRaiseCalibSafeZ();
    void commandTraverseCalibSafeXY(float x, float y);
    void commandTraverseXY(float x, float y);
    void commandLowerToZ(float z);
    void abortProcess(AbortReason reason = AbortReason::Unspecified);
    void setAutomaticProcessGuards(bool active);
    void handleMoveSafePose();
    void handleProbeApproach();
    void handleCalibApproach();
    void handleProbeCalibrate(float dt);
    void handleProbeRemeasure(float dt);
    void handlePointApproach();
    void handlePreClamp(float dt);
    void handlePlateBoreSearch(float dt);
    void publishBoreReport(const BoreSearch::Result& result, bool confirm_pending);
    void handleClamp();
    void handleInsertInitial();
    void handleTestOrAge();
    void enterTestOrAgeAfterManualSeat();
    void completeTestOrAgeManual();
    void resetExtractSeek();
    // Extract leg: arm on Zug (Fz < -tol), then kraftfrei, then optional +Z offset.
    // Returns true when the extract leg is complete.
    bool tickExtractSeek(float fz, float free_tol);
    // Start insert after extract: optional fast return to force_free_z_ at
    // extract_after_free_v, then dive at phase3_v_insert.
    void beginTestOrAgeInsert();
    void startTestOrAgeInsertDive();
    void handleUnclamp();
    void handleNextPoint();
    void handleMoveSafeEnd();
    void startForceTare(ProcedurePhase next);
    void startForceTareBeforeFineApproach();
    void startForceTareBeforeProbeBore(ProbeCalibration::StorePolicy policy, bool remeasure);
    void startForceTareBeforePlateBore();
    void startProbeBoreAfterTare();
    void startPlateBoreAfterTare();
    bool awaitSpreaderRetracted();
    void commandFineApproach();
    void handleForceTare();

    void raiseAnomaly(uint8_t type, float measured, float limit);
    bool checkTestOrAgeAnomalies();
    void beginAnomalyExit(AnomalyExitAction action);
    void tickAnomalyExit();
    void finishAnomalyExit();

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
    uint8_t cycle_leg_; // phase-specific leg (InsertInitial; Test; Unclamp)
    uint8_t approach_leg_; // PointApproach / ProbeApproach / ProbeRemeasure / MoveSafeEnd
    TestPoint target_machine_;
    // JSON floor in machine frame for the current point. Fine approach and Clamp
    // must not command Z below this. Seat and TestOrAge may.
    float floor_machine_z_ = 0.0f;
    float end_x_;
    float end_y_;
    bool probe_calib_standalone_;
    bool probe_await_spreader_ = false; // open spreader before Install/OffsetOnly bore
    bool probe_await_spreader_retract_ = false; // retract after Einmessen before continuing
    bool preclamp_await_spreader_retracted_ = false; // close/deactivate before PreClamp wall search
    ProcedurePhase tare_next_;
    // Coarse Z has stopped. After ForceTare, resume PointApproach leg 3
    // instead of enterPhase(tare_next_), which would restart the approach.
    bool fine_approach_after_tare_;
    // Coarse Z / hub above Einmess-Bohrung. After ForceTare start z_first BoreSearch.
    bool probe_bore_after_tare_ = false;
    bool plate_bore_after_tare_ = false;
    ProbeCalibration::StorePolicy probe_bore_policy_ = ProbeCalibration::StorePolicy::Install;
    bool probe_bore_remeasure_ = false;

    BoreSearch bore_search_{};
    bool preclamp_await_confirm_ = false;
    bool plate_bore_standalone_ = false;
    bool plate_await_spreader_ = false;
    bool calib_approach_standalone_ = false;
    bool test_or_age_standalone_ = false;
    bool test_or_age_await_spreader_ = false;
    std::uint8_t plate_bore_point_id_ = 0;
    BoreReport bore_report_{};

    // 0=before seat (Clamp→Seat), 1=after seat (Clamp→Test), 2=after test
    uint8_t point_stage_ = 0;
    UnclampMode unclamp_mode_ = UnclampMode::AfterSeat;

    float ref_insert_x_ = 0.0f;
    float ref_insert_y_ = 0.0f;
    float ref_insert_z_ = 0.0f;
    float ref_extract_x_ = 0.0f;
    float ref_extract_y_ = 0.0f;
    float ref_extract_z_ = 0.0f;
    float last_insert_x_ = 0.0f;
    float last_insert_y_ = 0.0f;
    float last_insert_z_ = 0.0f;
    float last_extract_x_ = 0.0f;
    float last_extract_y_ = 0.0f;
    float last_extract_z_ = 0.0f;
    bool ref_insert_valid_ = false;
    bool ref_extract_valid_ = false;

    TestPoint tool_offset_before_remeasure_{};
    bool probe_remeasure_calib_active_ = false;

    AnomalyReport anomaly_report_{};
    bool anomaly_exit_pending_ = false;
    uint8_t anomaly_exit_leg_ = 0;
    AnomalyExitAction anomaly_exit_action_ = AnomalyExitAction::None;

    // Kraft-Weg distance gate (TestOrAge). Reset on leg/iteration change.
    bool force_path_have_z_ = false;
    float force_path_last_z_ = 0.0f;
    // Insert fine phase: slower v + finer dz near Sollkraft.
    bool insert_fine_active_ = false;

    // Extract seek (TestOrAge legs 0/2): ignore first 0 N (Druck→Zug), end after Zug→0.
    bool extract_seen_tensile_ = false;
    bool extract_free_latched_ = false;
    float extract_offset_target_z_ = 0.0f;
    // Insert: returning from after-free offset down to force_free_z_ (not yet measuring).
    bool insert_return_to_free_ = false;
};
