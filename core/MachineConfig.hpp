#pragma once

#include "SafetyLimits.hpp"
#include "TestPoint.hpp"
#include "Types.hpp"

// Writable machine parameters (GUI Parametereinstellungen / M12 CmdSetMachineConfig).
// Must be validated against SafetyLimits; invalid incoming values are rejected.
struct MachineConfig
{
    struct Motion
    {
        // Test-point / Safe-Z traverse (not used by M19).
        float v_approach_testpoints = 10.0f;
        float v_fine_testpoints = 2.0f; // fine Z approach + PreClamp XY
        float v_unclamp = 1.0f;
        float a_testpoints = 0.0f; // 0 = constant velocity
        float corner_testpoints = 0.0f; // 0 = full stop between axes

        float v_homing = 5.0f;
        float a_homing = 0.0f; // seek/retract only; endstop hit is hard stop

        // Clearance above max(highest plate-frame program Z, probe Z in plate frame) [mm].
        float safe_z_buffer = 30.0f;

        // Procedure phase extract (+Z) / insert (−Z) speeds and path profile.
        float phase1_v_extract = 1.0f;
        float phase1_v_insert = 1.0f;
        float phase1_a = 0.0f;
        float phase1_corner = 0.0f;
        float phase2_v_extract = 2.0f;
        float phase2_v_insert = 3.0f;
        float phase2_a = 0.0f;
        float phase2_corner = 0.0f;
        float phase3_v_extract = 2.0f;
        float phase3_v_insert = 2.0f;
        float phase3_a = 0.0f;
        float phase3_corner = 0.0f;
    };

    struct Force
    {
        float force_filter = 0.0f; // 0 = no filter, else (0,1]
        float xy_tolerance_N = 0.5f; // PreClamp |Fx|/|Fy| deadband [N]
        float xy_correction_step = 0.05f; // PreClamp XY step [units]
        float xy_max_correction = 2.0f; // max XY deviation from approach target [units]
        float insertion_buffer = 1.0f; // ExtractionPoint = InsertionPoint + buffer in +Z
        float insert_search_distance = 10.0f; // Phase-1 Z dive; independent of probe.search_distance
        float sample_timeout_s = 0.5f; // stale HostInputs.force → Fault [s]
        float xy_correct_timeout_s = 2.0f; // PreClamp abort if Fx/Fy not settled
        unsigned xy_max_attempts = 80u; // PreClamp abort after this many XY steps
    };

    struct Collision
    {
        float approach_N = 20.0f;
        float retract_N = 50.0f;
        float clamp_N = 20.0f;
        float unclamp_N = 20.0f;
        float contact_N = 50.0f;
        float measure_N = 50.0f;
    };

    struct Spreader
    {
        float extend_time_s = 0.5f;
        float retract_time_s = 0.5f;
    };

    struct Phase2
    {
        float force_tolerance_N = 0.5f; // |Fz - target| deadband
        float z_correction_gain = 0.1f; // dZ = gain * dF  [units/N]
        float z_correction_max = 1.0f; // |dZ| cap per attempt
        float minimum_correction = 0.01f; // ignore tinier steps
        unsigned max_optimization_iterations = 10u;
        unsigned max_sign_flips = 8u; // oscillation guard
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
        TestPoint home_offset{0.0f, 0.0f, 0.0f}; // retract distance from +max switch [mm] per axis;
                                                 // 0 → Machine uses kDefaultHomingRetract (2 mm).
                                                 // At switch: software axis := +R; after retract: axis := 0.
                                                 // Not the measured home pose (see CalibrationSet.home_offset).
        TestPoint tool_offset{0.0f, 0.0f, 0.0f}; // TCP correction in machine coordinates
    };

    Motion motion{};
    Force force{};
    Collision collision{};
    Spreader spreader{};
    Phase2 phase2{};
    Probe probe{};
    Calibration calibration{};
    Communication communication{};

    // Threshold for a collision context (raw, no clamp).
    float collisionThreshold(CollisionContext context) const;
};

// Return Ok if every field is inside SafetyLimits; otherwise the first error found.
ConfigValidationError validateMachineConfig(const MachineConfig& config);

// Copy incoming into dest only when validateMachineConfig(incoming) == Ok.
// On rejection dest is unchanged.
bool tryApplyMachineConfig(MachineConfig& dest, const MachineConfig& incoming,
                           ConfigValidationError& error);
