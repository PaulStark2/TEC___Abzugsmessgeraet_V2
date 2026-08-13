#pragma once

// 3D force vector from the host force sensor (physical Newton values in Core).
struct ForceVector
{
    // Force component along machine +X axis [N].
    float fx;
    // Force component along machine +Y axis [N].
    float fy;
    // Force component along machine +Z axis [N].
    float fz;
};

// HostInputs is a pure data container for later Host/Hardware integration.
struct HostInputs
{
    // 3D force in Newton [N]: fx/fy/fz along machine +X/+Y/+Z.
    // Positive values follow positive machine axis direction; negatives are valid.
    // {0,0,0} means zero force vector.
    ForceVector force;

    // Limit switch flags (M11: directional evaluation in Machine::tick()).
    bool limitXMin; // X minimum limit
    bool limitXMax; // X maximum limit
    bool limitYMin; // Y minimum limit
    bool limitYMax; // Y maximum limit
    bool limitZMin; // Z minimum limit
    bool limitZMax; // Z maximum limit

    // Emergency stop flag (M11: highest-priority safety input in Machine::tick()).
    bool e_stop; // hardware e-stop active

    // Probe/touch sensor flag (M15: digital input, short pulse while triggered).
    // Core only receives the signal; no calibration or teach-in logic exists yet.
    bool probeTriggered; // measuring probe contact
};
