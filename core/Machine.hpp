#pragma once

#include "CalibrationSet.hpp"
#include "ForceWatchdog.hpp"
#include "HostInputs.hpp" // host input container
#include "HostOutputs.hpp" // host output container
#include "MachineConfig.hpp"
#include "MotionPlanner.hpp" // compose motion planner
#include "SpreaderTimer.hpp"
#include "TestConfiguration.hpp"
#include "Types.hpp" // MachineState and MotionState

#include <functional>

#include <cstdint>

// Machine provides the overgeordneten Betriebszustandsautomaten über MotionPlanner. // state machine
class Machine
{
public:
    // Deterministic parameters for M10 force control.
    // Default values are chosen so that M10 stays effectively disabled unless tests/hosts override parameters.
    struct ForceControlParams
    {
        // XY hysteresis thresholds [N] (force magnitude).
        float fx_threshold_on_N = 100000.0f;
        float fx_threshold_off_N = 50000.0f;
        float fy_threshold_on_N = 100000.0f;
        float fy_threshold_off_N = 50000.0f;

        // Deterministic target step sizes for XY corrections [machine position units].
        float dx_step_units = 0.05f;
        float dy_step_units = 0.05f;

        // Maximum deviation of corrected X/Y targets from the commanded moveTo() targets [machine position units].
        // X and Y are clamped independently around the original commanded target.
        float max_x_correction_units = 1000000.0f;
        float max_y_correction_units = 1000000.0f;

        // Distance-to-Z-target threshold (absolute) for enabling slow Z approach / XY control.
        float slow_z_distance_threshold_units = 1e-3f;

        // Safety threshold for Fz [N].
        // M10 uses abs(Fz) >= fz_safety_threshold_N for Safety-Fault.
        float fz_safety_threshold_N = 1000000.0f;
    };

    // Construct machine with deterministic default motion parameters. // init defaults
    Machine(); // create

    // Construct machine with deterministic M10 ForceControl parameters. // init defaults
    explicit Machine(const ForceControlParams& params);

    // Return current machine state. // query state
    MachineState machineState() const; // getter

    // Return current motion state from the owned MotionPlanner. // thin getter
    MotionState motionState() const; // for TestExecutor AtTarget checks

    // M16 homing sub-state and flags.
    HomingState homingState() const;
    bool isHomed() const;

    // After Soft-STOP of an automatic process: reference is no longer trusted for a
    // new procedure start (operator must Home again). Does not clear measuredHome.
    void invalidateHoming();

    // Bitmask of directions currently blocked by soft limits (6 bits, kLimitDir*).
    uint8_t blockedDirections() const;

    // True when any limit switch is active or any direction is blocked.
    bool limitWarningActive() const;

    // Provide host I/O snapshot (no evaluation in M5). // set inputs
    void setHostInputs(const HostInputs& inputs); // assign

    // Return the last host inputs snapshot (M9: read force without evaluation). // get inputs
    const HostInputs& hostInputs() const; // getter

    // Optional simulation hook:
    // A caller may register a callback that recomputes/refreshes hostInputs
    // after the machine advanced motion for this tick.
    // Core logic calls this at a deterministic point in ProcedureController::tick.
    using HostInputUpdateCallback = std::function<void(Machine&)>;
    void setHostInputUpdateCallback(HostInputUpdateCallback cb);
    void invokeHostInputUpdateCallback();

    // Provide current host outputs snapshot (placeholders in M5). // query outputs
    HostOutputs hostOutputs() const; // getter

    // Start a new move to a target pose. // command move
    void moveTo(float x, float y, float z); // delegate

    // Reference-only homing (Z+ → X+ → Y+). Requires a valid MCS; never defines MCS.
    // Re-applies the stored frame at the switches (step-loss correction) without
    // touching home_offset / probe / plate / tool_offset.
    bool home();

    // Explicit MCS definition (same motion SM as home). Origin at +max switches (machine = 0);
    // latches derived home (-R,-R,-R), sets machine_coordinate_system_valid, clears plate/probe.
    bool defineMachineCoordinateSystem();

    // Host restore after restart: latch MCS without motion and without invalidating geometry.
    // home_position is ignored (wire compatibility); home is derived from current retract R.
    bool restoreMachineCoordinateSystem(const TestPoint& home_position);

    bool machineCoordinateSystemValid() const;

    // If MCS is valid and the TCP is already at CalibrationSet.home_offset, latch
    // isHomed without running the limit-seek SM (used after jog-back-to-home).
    bool confirmHomedIfAtHome(float tolerance_mm = 0.5f);

    // Test helper: open-loop step loss without clearing MCS offsets.
    void shiftRawPose(float dx, float dy, float dz);

    // Non-blocking progress update. // step
    void tick(float dt); // no blocking

    // Split tick used by ProcedureController so Pose(t) and Force(t) share a sample:
    //   advanceKinematics → host-input refresh → evaluateHostSafety → phase handler
    void advanceKinematics(float dt);
    void evaluateHostSafety(float dt);

    // Pause motion if Active, resume only from Paused. // pause
    void pause(); // command
    void resume(); // command

    // Stop motion from any non-terminal state. // stop
    void stop(); // command

    // Clear latched Fault after e_stop is released (M11). // clear fault
    void clearFault(); // command

    // Pneumatic commands (independent actuators, M11 stub). // pneumatics
    void setSpreaderExtend(bool extend); // Spreizer aktiv/inaktiv
    void setFixtureExtend(bool extend); // Bauplattenhalter offen/geschlossen

    // Pose getters (delegated from MotionPlanner). // read pose
    float x() const; // machine-frame
    float y() const;
    float z() const;
    float zVelocity() const;

    // Continuous commanded/raw pose (unchanged by machine re-reference).
    float rawX() const;
    float rawY() const;
    float rawZ() const;

    // machine = raw - referenceOffset
    float referenceOffsetX() const;
    float referenceOffsetY() const;
    float referenceOffsetZ() const;

    // Snap MotionPlanner/Axis pose (open-loop re-reference or test injection).
    // Does not run Homing. Clears isHomed when leaving the latched home pose.
    // Keeps HomingState::Homed (coordinate reference still established).
    void setCurrentPose(float x, float y, float z);

    // M17 RAM copies of writable config / taught calibration (not SafetyLimits).
    bool trySetMachineConfig(const MachineConfig& incoming, ConfigValidationError& error);
    const MachineConfig& machineConfig() const;
    bool trySetTestConfiguration(const TestConfiguration& incoming, ConfigValidationError& error);
    const TestConfiguration& testConfiguration() const;
    void setPlateOrigin(const TestPoint& machine_pose);
    void setProbeCenter(const TestPoint& machine_pose);
    void setToolOffset(const TestPoint& offset);
    const CalibrationSet& calibrationSet() const;

    void setActiveVelocity(float velocity);
    // Apply velocity / acceleration / corner blend for the next motion segment.
    void setActiveMotionProfile(float velocity, float acceleration, float corner_radius);
    // Decelerate to a hold at the current pose (probe trigger); does not Process-STOP.
    void brakeToStop(float acceleration);
    void setPreClampXyEnabled(bool enabled);
    void loadPreClampForceParams();
    bool preClampXyEnabled() const;
    unsigned xyCorrectionSteps() const;

    // Collision watchdog context (ProcedureController sets this per phase).
    void setCollisionContext(CollisionContext context);
    CollisionContext collisionContext() const;
    ForceWatchdogTrip lastForceWatchdogTrip() const;
    FaultReason lastFaultReason() const;
    // Latch a process abort reason without entering MachineState::Fault.
    void latchFaultReason(FaultReason reason);
    float forceSampleAge() const;

    bool spreaderBusy() const;
    SpreaderState spreaderState() const;

    void setCommunicationWatchdogEnabled(bool enabled);
    bool communicationWatchdogEnabled() const;
    float hostHeartbeatAge() const;

    void setRuntimeConfigLocked(bool locked);
    bool runtimeConfigLocked() const;
    // Consume one latched trigger event. Pose is the host-latched trigger pose.
    bool consumeProbeTrigger();
    bool consumeProbeTrigger(TestPoint& pose_out);

private:
    enum class ForceControlPhase
    {
        NormalZApproach,
        SlowZApproach
    };

    MotionPlanner motion_planner_; // direct composition
    MachineState machine_state_; // state

    // Default motion parameters used when (re)starting motion after stopMotion(). // stored defaults
    float default_max_velocity_; // max velocity
    float default_acceleration_; // acceleration
    float default_corner_radius_; // two-axis blend radius (0 = full stop)

    // Commanded destination (never overwritten by soft-limit blocking).
    float stored_target_x_; // stored X
    float stored_target_y_; // stored Y
    float stored_target_z_; // stored Z

    // Original X/Y targets from moveTo(); XY correction is clamped relative to these. // commanded baseline
    float commanded_target_x_; // baseline X
    float commanded_target_y_; // baseline Y

    // M10 force control parameters and internal phase.
    ForceControlParams force_params_; // deterministic params
    ForceControlPhase force_phase_; // current phase
    bool x_force_correction_active_; // hysteresis state for X
    bool y_force_correction_active_; // hysteresis state for Y

    // M16 soft-limit blocking (recomputed each tick from limits + stored targets).
    uint8_t blocked_directions_;

    // M16 homing.
    HomingState homing_state_;
    bool is_homed_;
    bool homing_retracting_; // true while executing Retract-R after switch latch
    static constexpr float kHomingVelocity = 5.0f;
    static constexpr float kHomingOvershoot = 1000.0f;
    static constexpr float kDefaultHomingRetract = 2.0f; // mm from +max when config retract is 0
    static constexpr float kHomingCreepSpeedFactor = 0.1f; // fraction of v_homing
    static constexpr float kHomingCreepMinSpeed = 0.2f;    // mm/s floor
    static constexpr float kHomingCreepBackTravel = 5.0f;  // mm per creep-back command
    static constexpr float kHomingCreepBackMaxTravel = 50.0f; // safety: give up if still on limit

    // Per-axis seek sub-state (not on the wire; HomingState stays Z/X/Y).
    enum class HomingAxisLeg : uint8_t
    {
        FastSeek = 0,     // +max until limit true
        CreepBack = 1,    // DefineMcs: −axis until limit false
        CreepForward = 2, // DefineMcs: +axis until limit true (rising edge)
        RetractR = 3      // retract by R from latched switch
    };

    MachineConfig machine_config_;
    TestConfiguration test_configuration_;
    CalibrationSet calibration_set_;
    CollisionContext collision_context_;
    ForceWatchdogTrip last_force_watchdog_trip_;
    SpreaderTimer spreader_timer_;
    bool pre_clamp_xy_enabled_;
    unsigned xy_correction_steps_;
    FaultReason last_fault_reason_;
    float force_sample_age_s_;
    std::uint32_t last_force_sample_sequence_;
    bool have_force_sample_;
    bool force_sample_fresh_;
    bool comm_watchdog_enabled_;
    bool heartbeat_fresh_;
    float heartbeat_age_s_;
    std::uint32_t last_heartbeat_sequence_;
    bool have_heartbeat_;
    bool runtime_config_locked_;
    bool probe_trigger_latched_;
    bool prev_probe_triggered_;
    TestPoint probe_trigger_pose_{};
    std::uint32_t last_probe_trigger_sequence_;

    // Host I/O containers (no evaluation). // placeholders
    HostInputs host_inputs_; // last inputs
    HostOutputs host_outputs_; // produced outputs

    HostInputUpdateCallback host_input_update_callback_{};

    // M11 safety helpers.
    void enterSafetyFault(FaultReason reason); // stopMotion + latched Fault
    void applySpreaderSafeState(); // valve off; do not invent a retracted pose
    void updatePneumaticStub(); // mirror Extend -> Extended for both actuators

    // M16 limit helpers: derive blocked directions and effective planner targets without
    // mutating stored_target_* (so a later command away from the limit still works).
    uint8_t computeBlockedDirections(float target_x, float target_y, float target_z) const;
    void applyEffectiveTargets(float target_x, float target_y, float target_z);
    uint8_t activeLimitSwitchMask() const;

    // M16 homing tick and phase transitions.
    enum class HomingMode : uint8_t
    {
        DefineMcs = 0, // establish MCS at +max switches (machine = 0); latch derived home
        Reference = 1  // re-apply stored MCS (switch = 0); keep probe/plate
    };

    void tickHoming(float dt);
    bool startHomingSequence(HomingMode mode);
    void beginHomingPhase(HomingState phase);
    void beginCreepBack();
    void beginCreepForward();
    // Snap the active axis to MCS origin (0) at the +max switch; retract to -R.
    void beginHomingRetract(HomingState phase);
    TestPoint derivedMachineHomePose() const;
    void applyDerivedHomeFromRetractR();
    float homingRetractDistance(float configured) const;
    float homingCreepVelocity() const;
    void setHomingMotionSpeed(float velocity);
    void commandActiveAxisToward(float signed_delta);
    bool homingPhaseLimitReached(HomingState phase) const;
    void finishHomingSuccess();
    void finishHomingError();

    HomingMode homing_mode_;
    HomingAxisLeg homing_axis_leg_;
    float homing_creep_back_traveled_;

    void tickActiveMotion(float dt);
};
