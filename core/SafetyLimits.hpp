#pragma once

// Hard safety limits. Not writable via GUI or M12 MachineConfig.
// MachineConfig values that exceed these must be rejected (not clamped).
struct SafetyLimits
{
    // Absolute force magnitude the Core will ever accept as a configured threshold [N].
    // Matches the sensor range (±50 N). Firmware ADC scaling is outside Core.
    static constexpr float kMaxFxAbs_N = 50.0f;
    static constexpr float kMaxFyAbs_N = 50.0f;
    static constexpr float kMaxFzAbs_N = 50.0f;

    // Absolute motion limits in machine position units / s and / s^2.
    static constexpr float kMaxVelocity = 50.0f;
    static constexpr float kMaxAcceleration = 500.0f;
    // Max corner blend radius [machine units] for two-axis path blending.
    static constexpr float kMaxCornerRadius = 50.0f;

    // Spreader valve timing bounds [s].
    static constexpr float kMinSpreaderTime_s = 0.01f;
    static constexpr float kMaxSpreaderTime_s = 10.0f;

    // Insertion / extraction buffer along Z [machine units].
    static constexpr float kMaxInsertionBuffer = 50.0f;

    // Phase-2 Z correction magnitude [machine units].
    static constexpr float kMaxZCorrection = 20.0f;

    // PreClamp max XY correction from the approach target [machine units].
    static constexpr float kMaxXyCorrection = 50.0f;

    // Phase-2 / phase-3 iteration cap.
    static constexpr unsigned kMaxIterations = 10000u;

    // Force-filter coefficient: 0 = passthrough, (0,1] = exponential smoothing.
    static constexpr float kMaxForceFilter = 1.0f;

    // Max allowed safe_z_buffer [mm] (clearance above highest plate-frame program Z).
    static constexpr float kMaxAbsSafeZ = 500.0f;

    // Force sample freshness window [s]. Stale HostInputs.force → Fault.
    static constexpr float kMinForceSampleTimeout_s = 0.05f;
    static constexpr float kMaxForceSampleTimeout_s = 5.0f;

    // Host communication heartbeat timeout while an automatic process runs [s].
    static constexpr float kMinHeartbeatTimeout_s = 0.05f;
    static constexpr float kMaxHeartbeatTimeout_s = 2.0f;

    static constexpr float kMinPreClampTimeout_s = 0.05f;
    static constexpr float kMaxPreClampTimeout_s = 30.0f;
    static constexpr unsigned kMaxPreClampAttempts = 10000u;

    // Probe disc / tool geometry and search envelope [machine units].
    static constexpr float kMaxProbeDiameter = 50.0f;
    static constexpr float kMaxProbeThickness = 20.0f;
    static constexpr float kMaxSearchDepth = 50.0f;
    static constexpr float kMaxApproachDistance = 100.0f;
    static constexpr float kMaxNominalAbs = 500.0f;
    static constexpr float kMaxToolCylindricalHeight = 100.0f;
    static constexpr float kMaxXySearchMargin = 50.0f;
};
