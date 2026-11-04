#pragma once

// Hard safety limits. Not writable via GUI or M12 MachineConfig.
// MachineConfig values that exceed these must be rejected (not clamped).
struct SafetyLimits
{
    // Typo brake for configured force thresholds [N]. Not the sensor range.
    // The measuring range and the watchdog hard limit live in MachineConfig
    // and default to ±30 N. Firmware ADC scaling is outside Core.
    static constexpr float kMaxFxAbs_N = 500.0f;
    static constexpr float kMaxFyAbs_N = 500.0f;
    static constexpr float kMaxFzAbs_N = 500.0f;

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

    // PreClamp max XY correction from the approach target [machine units].
    static constexpr float kMaxXyCorrection = 50.0f;

    // Coordinate / Z-envelope / tool-offset anomaly tolerances [machine units].
    static constexpr float kMaxCoordTolerance = 50.0f;
    static constexpr float kMaxZLimitMm = 100.0f;
    static constexpr float kMaxToolOffsetChangeTolerance = 50.0f;

    // Test / aging iteration cap (and other iteration-style bounds).
    static constexpr unsigned kMaxIterations = 10000u;

    // Force-filter time constant [s]: 0 = passthrough, (0, 1] = tau of the low-pass.
    static constexpr float kMaxForceFilter = 1.0f;

    // Max allowed safe_z_buffer [mm] (clearance above highest program or probe Z).
    static constexpr float kMaxAbsSafeZ = 500.0f;

    // Force sample freshness window [s]. Stale HostInputs.force → Fault.
    static constexpr float kMinForceSampleTimeout_s = 0.05f;
    static constexpr float kMaxForceSampleTimeout_s = 5.0f;

    // Host communication heartbeat timeout while an automatic process runs [s].
    static constexpr float kMinHeartbeatTimeout_s = 0.05f;
    static constexpr float kMaxHeartbeatTimeout_s = 2.0f;

    static constexpr float kMinPreClampTimeout_s = 0.05f;
    // Whole PreClamp chord search including optional refine passes [s].
    static constexpr float kMaxPreClampTimeout_s = 600.0f;
    static constexpr unsigned kMaxPreClampAttempts = 10000u;
    // Extra full two-chord passes after the first PreClamp search.
    static constexpr unsigned kMaxBoreRefinePasses = 5u;

    // Plate bore floor search envelope [machine units].
    static constexpr float kMaxPlateZSearch = 50.0f;
    static constexpr float kMaxPlateZLift = 20.0f;

    // Probe disc / tool geometry and search envelope [machine units].
    static constexpr float kMaxProbeDiameter = 50.0f;
    static constexpr float kMaxProbeThickness = 20.0f;
    static constexpr float kMaxSearchDepth = 50.0f;
    static constexpr float kMaxApproachDistance = 100.0f;
    static constexpr float kMaxNominalAbs = 500.0f;
    static constexpr float kMaxToolCylindricalHeight = 100.0f;
    static constexpr float kMaxXySearchMargin = 50.0f;

    // Measure-phase collision as percent of Prüfkraft (must exceed 100%).
    static constexpr float kMinMeasureCollisionPercent = 100.0f; // exclusive lower bound
    static constexpr float kMaxMeasureCollisionPercent = 500.0f;

    // Max single jog step while fault-manual is active [mm] (GUI/server clamp).
    static constexpr float kMaxFaultManualStep_mm = 10.0f;
};
