#pragma once

#include "SafetyLimits.hpp"
#include "TestPoint.hpp"
#include "Types.hpp"

// Per-axis force magnitude [N]. Compared with |Fx|, |Fy| and |Fz|.
struct AxisForceLimit
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// Writable machine parameters (GUI Parametereinstellungen / M12 CmdSetMachineConfig).
// Must be validated against SafetyLimits; invalid incoming values are rejected.
struct MachineConfig
{
    struct Motion
    {
        // Test-point / Safe-Z traverse (not used by M19).
        float v_approach_testpoints = 10.0f;
        float v_fine_testpoints = 2.0f; // fine Z approach only
        float v_bore_search = 1.0f; // PreClamp wall-to-wall and move to chord midpoints
        float v_unclamp = 1.0f;
        float a_testpoints = 0.0f; // 0 = constant velocity
        float corner_testpoints = 0.0f; // 0 = full stop between axes
        // Jog, hold, and calibration moves only. Must be > 0 so a direction
        // change can stop without a long coast. Automatic profiles stay on a_testpoints.
        float a_manual = 500.0f;

        float v_homing = 5.0f;
        // Targeted travel toward home standoff (Reference, session already Homed).
        float v_homing_fast = 18.0f;
        // Stop this far before derived home, then slow-seek to the switch [mm].
        float homing_approach_distance = 50.0f;
        float a_homing = 0.0f; // slow seek/retract/creep; endstop hit is hard stop
        // Acceleration for fast approach only [mm/s²]; 0 = constant velocity.
        float a_homing_fast = 500.0f;

        // Clearance above max(highest plate-frame approach Z, probe Z in plate frame) [mm].
        // Approach Z = program floor Z + point_z_clearance_mm.
        float safe_z_buffer = 30.0f;
        // Teach Anfahrt clearance above max machine-Z of stored calib poses (KM/K1–K3/probe) [mm].
        float safe_z_calib_buffer = 30.0f;
        // Approach height above the JSON floor Z [mm]. Floor stays the lower bound
        // during fine approach and the pneumatic Clamp phase.
        float point_z_clearance_mm = 2.0f;

        // Procedure phase extract (+Z) / insert (−Z) speeds and path profile.
        float phase1_v_extract = 1.0f;
        float phase1_v_insert = 1.0f;
        float phase1_a = 0.0f;
        float phase1_corner = 0.0f;
        float phase3_v_extract = 2.0f;
        float phase3_v_insert = 2.0f;
        // Insert speed once Fz reaches force_near_target_frac · target [mm/s].
        float phase3_v_insert_fine = 0.5f;
        float phase3_a = 0.0f;
        float phase3_corner = 0.0f;
        // +Z after extract kraftfrei (when extract_after_free_mm > 0) [mm/s].
        float extract_after_free_v = 1.0f;
    };

    struct Force
    {
        // Low-pass time constant [s] on Fx/Fy/Fz before the published sample.
        // 0 = passthrough. (0, 1] is tau in dy/dt = (u - y) / tau, not a per-sample alpha.
        float force_filter = 0.05f;
        float xy_tolerance_N = 0.5f; // PreClamp |Fx|/|Fy| deadband [N]
        float xy_correction_step = 0.05f; // PreClamp XY step [units]
        float xy_max_correction = 2.0f; // unused by the bore search; kept so the config frame order stays
        float insertion_buffer = 1.0f; // ExtractionPoint = InsertionPoint + buffer in +Z
        // AfterSeat fixed +Z retract from insertion_z_ before second PreClamp [mm].
        // 0 = no retract (tare / second PreClamp at seat pose).
        float after_seat_retract_mm = 2.0f;
        // After true kraftfrei on extract (+Z): continue this far and keep measuring [mm].
        // 0 = stop at kraftfrei (after tensile arming). Kraftfrei is the second 0 N
        // crossing (after Zug), not the Druck→Zug sign change.
        float extract_after_free_mm = 0.0f;
        float insert_search_distance = 10.0f; // Phase-1 Z dive; independent of probe.*
        float sample_timeout_s = 0.5f; // stale HostInputs.force → Fault [s]
        float xy_correct_timeout_s = 30.0f; // whole four-wall bore search [s]
        unsigned xy_max_attempts = 80u; // unused by the bore search; kept in the config frame
        // Measuring range magnitude for every axis [N]. Calibration points and
        // target_force_N must stay inside it. Collision thresholds may rise up to it.
        float range_N = 30.0f;
        // Watchdog hard-limit magnitude [N], per axis. Must be <= range_N.
        // hard_limit_N is Z. The name stays so older parameter files keep their Z limit.
        float hard_limit_N = 30.0f;
        float hard_limit_x_N = 30.0f;
        float hard_limit_y_N = 30.0f;
        // How far a wall search may leave the programmed point [mm].
        float xy_search_limit = 5.0f;
        // Pause for an operator confirm when the second-chord center is still loaded.
        // Off continues into Clamp.
        bool preclamp_confirm = true;
        // Extra full two-chord passes from the found center. 0 = one search only.
        uint8_t bore_refine_passes = 0;
        float force_tolerance_N = 0.5f; // |Fz - target| / force-free deadband
        float coord_tolerance_mm = 0.5f;
        float test_lateral_force_N = 5.0f;
        float z_limit_pos_mm = 10.0f;
        float z_limit_neg_mm = 15.0f;
        bool anomaly_skip = false;
        float tool_offset_change_tolerance_mm = 0.5f;
        // Minimum |Δz| between stored Kraft-Weg samples [mm]. 0 = every tick.
        // Default 0.01 mm; see docs/Measurement_Evaluation.md.
        float force_path_dz_mm = 0.01f;
        // Finer |Δz| gate during insert near Sollkraft (see force_near_target_frac).
        float force_path_dz_fine_mm = 0.005f;
        // Enter insert fine phase when Fz >= frac · target_force_N (0…1].
        float force_near_target_frac = 0.8f;
    };

    struct Collision
    {
        // Z limits keep the historical names. X and Y default to the same value.
        float approach_N = 20.0f;
        float approach_x_N = 20.0f;
        float approach_y_N = 20.0f;
        float retract_N = 30.0f;
        float retract_x_N = 30.0f;
        float retract_y_N = 30.0f;
        float clamp_N = 20.0f;
        float clamp_x_N = 20.0f;
        float clamp_y_N = 20.0f;
        float unclamp_N = 20.0f;
        float unclamp_x_N = 20.0f;
        float unclamp_y_N = 20.0f;
        float contact_N = 30.0f;
        float contact_x_N = 30.0f;
        float contact_y_N = 30.0f;
        // Measure / Phase-3 thresholds as percent of Prüfkraft (target_force_N).
        // Wire field names keep the historic *_N suffix; values are percent (e.g. 150).
        float measure_N = 150.0f;
        float measure_x_N = 150.0f;
        float measure_y_N = 150.0f;
    };

    // Restricted jog while MachineState::Fault (operator recovery).
    struct FaultManual
    {
        float v_max = 5.0f; // mm/s
        float a = 500.0f;   // mm/s^2; must be > 0
        // Elevated hard limits while fault-manual is active (phase collision off).
        float hard_limit_N = 50.0f;
        float hard_limit_x_N = 50.0f;
        float hard_limit_y_N = 50.0f;
    };

    struct Spreader
    {
        float extend_time_s = 0.5f;
        float retract_time_s = 0.5f;
    };

    // Einmess-Bohrung + plate bore teach (KM/K1–K3). Independent of PreClamp force.*.
    // BoreSearch z_first: floor → lift → XY two-chord. Durable pose in CalibrationSet.probe_center
    // (Install) or MsgBoreCenter report (plate teach).
    struct Probe
    {
        float v_approach = 10.0f; // ProbeApproach / CalibApproach XY / coarse Z
        float v_search = 1.0f; // bore wall / floor search
        float wall_force_N = 0.5f;
        float floor_force_N = 0.5f;
        float xy_search_limit = 5.0f;
        // Must exceed approach_distance so OffsetOnly / plate teach can reach the floor from standoff.
        float z_search_limit = 20.0f;
        float z_lift_mm = 2.0f;
        float timeout_s = 60.0f;
        uint8_t refine_passes = 0;
        float approach_distance = 15.0f; // coarse Z standoff above stored center before z_first
    };

    struct Communication
    {
        float heartbeat_timeout_s = 0.3f; // host heartbeat timeout while auto process runs [s]
    };

    struct Calibration
    {
        TestPoint home_offset{0.0f, 0.0f, 0.0f}; // retract distance R from homing switch [mm] per axis;
                                                 // 0 → Machine uses kDefaultHomingRetract (2 mm).
                                                 // X homes at Min (home +R); Y/Z at +Max (home −R).
                                                 // Changing R updates CalibrationSet.home_offset; MCS is not redefined.
        TestPoint tool_offset{0.0f, 0.0f, 0.0f}; // TCP correction in machine coordinates
    };

    Motion motion{};
    Force force{};
    Collision collision{};
    FaultManual fault_manual{};
    Spreader spreader{};
    Probe probe{};
    Calibration calibration{};
    Communication communication{};

    // Z threshold for a collision context (raw stored value, no clamp).
    // For Measure this is percent of Prüfkraft, not Newton.
    float collisionThreshold(CollisionContext context) const;
    // Per-axis threshold for the same context. Approach…Contact: |F| in N.
    // Measure: percent of target_force_N (scale at watchdog evaluation time).
    AxisForceLimit collisionLimit(CollisionContext context) const;
};

// Return Ok if every field is inside SafetyLimits; otherwise the first error found.
ConfigValidationError validateMachineConfig(const MachineConfig& config);

// Copy incoming into dest only when validateMachineConfig(incoming) == Ok.
// On rejection dest is unchanged.
bool tryApplyMachineConfig(MachineConfig& dest, const MachineConfig& incoming,
                           ConfigValidationError& error);
