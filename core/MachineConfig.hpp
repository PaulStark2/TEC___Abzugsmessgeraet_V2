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
        float a_homing = 0.0f; // seek/retract only; endstop hit is hard stop

        // Clearance above max(highest plate-frame program Z, probe Z in plate frame) [mm].
        float safe_z_buffer = 30.0f;

        // Procedure phase extract (+Z) / insert (−Z) speeds and path profile.
        float phase1_v_extract = 1.0f;
        float phase1_v_insert = 1.0f;
        float phase1_a = 0.0f;
        float phase1_corner = 0.0f;
        float phase3_v_extract = 2.0f;
        float phase3_v_insert = 2.0f;
        float phase3_a = 0.0f;
        float phase3_corner = 0.0f;
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
        float insert_search_distance = 10.0f; // Phase-1 Z dive; independent of probe.search_distance
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
        // Default 0.1 mm matches the force correlation length at phase3_v ≈ 2 mm/s
        // (see docs/Measurement_Evaluation.md). Not yet on the M12 MachineConfig wire.
        float force_path_dz_mm = 0.1f;
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
        float measure_N = 30.0f;
        float measure_x_N = 30.0f;
        float measure_y_N = 30.0f;
    };

    struct Spreader
    {
        float extend_time_s = 0.5f;
        float retract_time_s = 0.5f;
    };

    struct Probe
    {
        float v_approach_probe = 10.0f; // raise / align / descend to search start
        float v_transfer_probe = 10.0f; // backoff, retreat, hub transfers
        float v_search = 2.0f; // coarse contact search
        float v_search_fine = 1.0f; // fine contact search
        float a_probe_transfer = 0.0f;
        float corner_probe_transfer = 0.0f;
        float a_probe_search = 0.0f; // trapezoid during search (no blend)
        float a_probe_brake = 500.0f; // emergency stop accel on trigger

        float search_distance = 10.0f; // ABB-style extra XY start clearance (not insert depth)
        float backoff = 1.0f; // backoff after trigger [units]
        float search_depth = 5.0f; // XY work height = measured center Z − search_depth; Z search end
        float approach_distance = 15.0f;
        float head_diameter = 0.0f; // reserved; unused by the current protocol
        float measuring_head_diameter = 8.0f;
        float measuring_head_thickness = 3.0f;
        float tool_diameter = 2.5f;
        float tool_cylindrical_height = 15.0f; // vertical tool cylinder from TCP in +Z
        float xy_search_margin = 2.0f; // extra inward XY travel past probe radius; not tool radius
        unsigned num_fine_searches = 1u;
        // Legacy field only (older builds used it for tool_offset math). Not the durable
        // probe position; not a motion/search target. Current M19 uses CalibrationSet.probe_center.
        TestPoint probe_offset_reference{40.0f, 40.0f, 10.0f};
    };

    struct Communication
    {
        float heartbeat_timeout_s = 0.3f; // host heartbeat timeout while auto process runs [s]
    };

    struct Calibration
    {
        TestPoint home_offset{0.0f, 0.0f, 0.0f}; // retract distance R from +max switch [mm] per axis;
                                                 // 0 → Machine uses kDefaultHomingRetract (2 mm).
                                                 // MCS origin is the +max switch (machine = 0). Home is (-R,-R,-R).
                                                 // Changing R updates CalibrationSet.home_offset; MCS is not redefined.
        TestPoint tool_offset{0.0f, 0.0f, 0.0f}; // TCP correction in machine coordinates
    };

    // Plate bore teach (KM / K1–K3). Independent of PreClamp force.* search params.
    struct PlateCalib
    {
        float v_plate_bore_search = 1.0f;
        float wall_force_N = 0.5f;
        float floor_force_N = 0.5f;
        float xy_search_limit = 5.0f;
        float z_search_limit = 10.0f;
        float z_lift_mm = 2.0f;
        float timeout_s = 60.0f;
        uint8_t refine_passes = 0;
    };

    Motion motion{};
    Force force{};
    Collision collision{};
    Spreader spreader{};
    Probe probe{};
    Calibration calibration{};
    Communication communication{};
    PlateCalib plate_calib{};

    // Z threshold for a collision context (raw, no clamp).
    float collisionThreshold(CollisionContext context) const;
    // Per-axis threshold for the same context. x/y/z are |Fx|/|Fy|/|Fz|.
    AxisForceLimit collisionLimit(CollisionContext context) const;
};

// Return Ok if every field is inside SafetyLimits; otherwise the first error found.
ConfigValidationError validateMachineConfig(const MachineConfig& config);

// Copy incoming into dest only when validateMachineConfig(incoming) == Ok.
// On rejection dest is unchanged.
bool tryApplyMachineConfig(MachineConfig& dest, const MachineConfig& incoming,
                           ConfigValidationError& error);
