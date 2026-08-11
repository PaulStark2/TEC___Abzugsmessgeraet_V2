// Platform-independent Core type definitions.
#pragma once

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
    // Fault placeholder state (not automatically triggered in M5).
    Fault
};

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
