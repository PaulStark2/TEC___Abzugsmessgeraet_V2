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
    };

    struct Force
    {
        float force_filter = 0.0f; // 0 = no filter, else (0,1]
        float xy_tolerance_N = 0.5f; // PreClamp |Fx|/|Fy| deadband [N]
        float xy_correction_step = 0.05f; // PreClamp XY step [units]
        float xy_max_correction = 2.0f; // max XY deviation from approach target [units]
        float insertion_buffer = 1.0f; // ExtractionPoint = InsertionPoint + buffer in +Z
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
    };

    struct Probe
    {
        float search_speed = 2.0f; // placeholder for later real probe protocol
        float search_distance = 10.0f;
        float backoff = 1.0f;
    };

    struct Calibration
    {
        TestPoint home_offset{0.0f, 0.0f, 0.0f}; // pose written after homing
        TestPoint tool_offset{0.0f, 0.0f, 0.0f}; // probe tip → spreader TCP
    };

    Motion motion{};
    Force force{};
    Collision collision{};
    Spreader spreader{};
    Phase2 phase2{};
    Probe probe{};
    Calibration calibration{};

    // Threshold for a collision context (raw, no clamp).
    float collisionThreshold(CollisionContext context) const;
};

// Return Ok if every field is inside SafetyLimits; otherwise the first error found.
ConfigValidationError validateMachineConfig(const MachineConfig& config);

// Copy incoming into dest only when validateMachineConfig(incoming) == Ok.
// On rejection dest is unchanged.
bool tryApplyMachineConfig(MachineConfig& dest, const MachineConfig& incoming,
                           ConfigValidationError& error);
