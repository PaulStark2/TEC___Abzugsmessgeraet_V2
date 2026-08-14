#pragma once

#include "MotionPlanner.hpp" // compose motion planner
#include "HostInputs.hpp" // host input container
#include "HostOutputs.hpp" // host output container
#include "Types.hpp" // MachineState and MotionState

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

    // Bitmask of directions currently blocked by soft limits (6 bits, kLimitDir*).
    uint8_t blockedDirections() const;

    // True when any limit switch is active or any direction is blocked.
    bool limitWarningActive() const;

    // Provide host I/O snapshot (no evaluation in M5). // set inputs
    void setHostInputs(const HostInputs& inputs); // assign

    // Return the last host inputs snapshot (M9: read force without evaluation). // get inputs
    const HostInputs& hostInputs() const; // getter

    // Provide current host outputs snapshot (placeholders in M5). // query outputs
    HostOutputs hostOutputs() const; // getter

    // Start a new move to a target pose. // command move
    void moveTo(float x, float y, float z); // delegate

    // Start Core homing sequence Z+ → X+ → Y+ (M16).
    bool home();

    // Non-blocking progress update. // step
    void tick(float dt); // no blocking

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
    float x() const; // getter
    float y() const; // getter
    float z() const; // getter

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
    static constexpr float kHomingVelocity = 5.0f;
    static constexpr float kHomingOvershoot = 1000.0f;

    // Host I/O containers (no evaluation). // placeholders
    HostInputs host_inputs_; // last inputs
    HostOutputs host_outputs_; // produced outputs

    // M11 safety helpers.
    void enterSafetyFault(); // stopMotion + Fault only (no pneumatic changes)
    void updatePneumaticStub(); // mirror Extend -> Extended for both actuators

    // M16 limit helpers: derive blocked directions and effective planner targets without
    // mutating stored_target_* (so a later command away from the limit still works).
    uint8_t computeBlockedDirections(float target_x, float target_y, float target_z) const;
    void applyEffectiveTargets(float target_x, float target_y, float target_z);
    uint8_t activeLimitSwitchMask() const;

    // M16 homing tick and phase transitions.
    void tickHoming(float dt);
    void beginHomingPhase(HomingState phase);
    bool homingPhaseLimitReached(HomingState phase) const;
    void finishHomingSuccess();
    void finishHomingError();

    void tickActiveMotion(float dt);
};
