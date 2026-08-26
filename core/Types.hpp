// Platform-independent Core type definitions.
#pragma once

#include <cstdint>

// Declare the trivial Core version query used by the simulation host.
int core_version();

// Identify each linear machine axis.
enum class AxisId
{
    // Horizontal axis X.
    X,
    // Horizontal axis Y.
    Y,
    // Vertical tensile axis Z.
    Z
};

// Position tolerance used when comparing axis position to its target.
constexpr float kPositionTolerance = 1e-5f;

// Multi-axis motion state used by the Core MotionPlanner.
enum class MotionState
{
    // No active motion command.
    Idle,
    // At least one axis is moving toward its target.
    Moving,
    // All axes reached their commanded targets.
    AtTarget
};

// Machine state used by the Core Machine state machine.
enum class MachineState
{
    // No active machine command.
    Idle,
    // Machine actively executes motion.
    Active,
    // Machine motion is paused (resume-capable).
    Paused,
    // Machine motion is stopped/aborted (resume not allowed).
    Stopped,
    // Latched safety fault (E-Stop, Fz safety — not soft limits).
    Fault,
    // Multi-axis homing sequence in progress (M16).
    Homing
};

// Homing sub-state (M16). Exposed via protocol StatusReport.homing_state.
enum class HomingState
{
    None,
    HomingZ,
    HomingX,
    HomingY,
    Homed,
    HomingError
};

// Limit-direction bitmask indices (match tecomm::kLimitBit* in ProtocolTypes.hpp).
constexpr uint8_t kLimitDirXMin = 0;
constexpr uint8_t kLimitDirXMax = 1;
constexpr uint8_t kLimitDirYMin = 2;
constexpr uint8_t kLimitDirYMax = 3;
constexpr uint8_t kLimitDirZMin = 4;
constexpr uint8_t kLimitDirZMax = 5;

// Process-level state owned by the TestExecutor (Milestone 7).
enum class ProcessState
{
    // No test program is currently executing.
    Idle,
    // A test program is sequencing through points.
    Running,
    // All program points were reached successfully.
    Completed,
    // Execution was aborted (e.g. machine stop).
    Aborted
};

// Collision-watchdog context (M17). Phase-dependent force thresholds live in MachineConfig.
enum class CollisionContext : uint8_t
{
    Approach = 0,
    Retract = 1,
    Clamp = 2,
    Unclamp = 3,
    Contact = 4,
    Measure = 5
};

// Number of CollisionContext values (for fixed arrays, no heap).
constexpr uint8_t kCollisionContextCount = 6;

// Test vs aging mode (not part of the point file).
enum class TestMode : uint8_t
{
    Test = 0,
    Aging = 1
};

// Automated procedure steps (not MachineState).
enum class ProcedurePhase : uint8_t
{
    Idle = 0,
    Homing = 1,
    MoveSafePose = 2,
    ProbeApproach = 3,
    ProbeCalibrate = 4,
    WaitPlateCalibration = 5,
    PointApproach = 6,
    PreClampXyCorrect = 7,
    Clamp = 8,
    InsertInitial = 9,
    OptimizeInsert = 10,
    TestOrAge = 11,
    Unclamp = 12,
    NextPoint = 13,
    MoveSafeEnd = 14,
    Completed = 15,
    Aborted = 16
};

// Latched reason for MachineState::Fault (M18). None when not in Fault.
enum class FaultReason : uint8_t
{
    None = 0,
    EStop = 1,
    ForceHardLimit = 2,
    ForceCollision = 3,
    ForceNonFinite = 4,
    ForceInvalidSample = 5,
    ForceStale = 6,
    CommunicationTimeout = 7,
    ProcedureAbort = 8,
    ProbeSearchNoContact = 9
};

// Electrical/timing view of the spreader. There is no end-position sensor.
// SafeDeenergized: valve off after STOP/Fault; extended() is last confirmed timer
// state only and must not be read as a mechanical guarantee.
enum class SpreaderState : uint8_t
{
    Retracted = 0,
    Extending = 1,
    Extended = 2,
    Retracting = 3,
    SafeDeenergized = 4
};

// Config validation result: Ok or a specific rejection (never silent clamp).
enum class ConfigValidationError : uint8_t
{
    Ok = 0,
    VelocityExceedsSafety,
    VelocityNotPositive,
    AccelerationExceedsSafety,
    AccelerationNegative,
    CornerExceedsSafety,
    CornerNegative,
    CornerInconsistent, // corner_radius < v^2/(2a) when blending is enabled
    ForceExceedsSafety,
    ForceNotPositive,
    TimeExceedsSafety,
    TimeNotPositive,
    BufferExceedsSafety,
    BufferNegative,
    GainNotPositive,
    IterationsZero,
    TargetForceInvalid,
    FilterInvalid,
    SafeZInvalid,
    RuntimeLocked
};
