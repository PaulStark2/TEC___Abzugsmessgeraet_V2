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
        float v_approach = 10.0f; // approach speed [units/s]
        float v_slow = 2.0f; // slow approach / XY-correct [units/s]
        float v_measure = 1.0f; // measure / insert speed [units/s]
        float v_homing = 5.0f; // homing speed [units/s]
        float acceleration = 0.0f; // 0 = constant velocity (existing Core default)
        float safe_z = 10.0f; // XY traverse height; must be above plate-point machine Z
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
        float retract_N = 20.0f;
        float clamp_N = 20.0f;
        float unclamp_N = 20.0f;
        float contact_N = 40.0f;
        float measure_N = 40.0f;
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
        float search_speed = 2.0f; // fast search speed [units/s]
        float search_distance = 10.0f; // ABB-style extra XY start clearance (not insert depth)
        float backoff = 1.0f; // backoff after trigger [units]
        float search_depth = 5.0f; // XY work height = measured center Z − search_depth; Z search end
        float approach_distance = 10.0f; // start height above nominal Z
        float fine_search_speed = 1.0f;
        float head_diameter = 0.0f; // reserved; unused by the current protocol
        float measuring_head_diameter = 8.0f;
        float measuring_head_thickness = 3.0f;
        float tool_diameter = 2.5f;
        float tool_cylindrical_height = 15.0f; // vertical tool cylinder from TCP in +Z
        float xy_search_margin = 2.0f; // extra inward XY travel past probe radius; not tool radius
        unsigned num_fine_searches = 3u;
        TestPoint nominal_probe{40.0f, 40.0f, 10.0f}; // expected disc center, inside default 0..100 travel
    };

    struct Communication
    {
        float heartbeat_timeout_s = 0.3f; // host heartbeat timeout while auto process runs [s]
    };

    struct Calibration
    {
        TestPoint home_offset{0.0f, 0.0f, 0.0f}; // pose written after homing
        TestPoint tool_offset{0.0f, 0.0f, 0.0f}; // TCP correction: nominal_probe - measured_probe
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
