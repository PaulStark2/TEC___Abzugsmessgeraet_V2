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
