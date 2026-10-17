#pragma once

#include <cstdint>

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

    // Host (STM32 ADC / SimPlant) marks whether this snapshot is a real conversion.
    // false → Core must Fault; never treat a missing conversion as 0 N.
    bool forceSampleValid = true;

    // Incremented by the host on each new conversion. Unchanged sequence ages in Machine::tick().
    std::uint32_t forceSampleSequence = 0;

    // Host (Pi) heartbeat. STM32/Core ages this independently of the Pi thread.
    // A missing increment while the communication watchdog is enabled → Fault.
    bool hostHeartbeatValid = true;
    std::uint32_t hostHeartbeatSequence = 0;

    // Limit switch flags (M11: directional evaluation in Machine::tick()).
    bool limitXMin; // X minimum limit
    bool limitXMax; // X maximum limit
    bool limitYMin; // Y minimum limit
    bool limitYMax; // Y maximum limit
    bool limitZMin; // Z minimum limit
    bool limitZMax; // Z maximum limit

    // Emergency stop flag (M11: highest-priority safety input in Machine::tick()).
    bool e_stop; // hardware e-stop active

    // Probe/touch sensor (authoritative host: STM32 or SimPlant).
    // CRITICAL: Core latches only when poseValid + finite XYZ + new sequence.
    // Level (probeTriggered) alone is ignored — see Machine::setHostInputs and
    // docs/M19_ProbeCalibration.md. Host must stamp TCP at the contact edge.
    // probeTriggerX/Y/Z are machine-frame TCP (same as Machine::x/y/z). SimPlant
    // latches raw plant TCP; SimProtocolBridge converts with referenceOffset.
    // FirmwareHost latches machine_.x/y/z() directly (already machine frame).
    bool probeTriggered; // measuring probe contact (level)
    bool probeTriggerPoseValid = false; // host provided XYZ at the trigger edge
    std::uint32_t probeTriggerSequence = 0; // incremented on each new trigger event
    float probeTriggerX = 0.0f;
    float probeTriggerY = 0.0f;
    float probeTriggerZ = 0.0f;
};
