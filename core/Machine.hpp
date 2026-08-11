#pragma once

#include "MotionPlanner.hpp" // compose motion planner
#include "HostInputs.hpp" // host input container
#include "HostOutputs.hpp" // host output container
#include "Types.hpp" // MachineState and MotionState

// Machine provides the overgeordneten Betriebszustandsautomaten über MotionPlanner. // state machine
class Machine
{
public:
    // Construct machine with deterministic default motion parameters. // init defaults
    Machine(); // create

    // Return current machine state. // query state
    MachineState machineState() const; // getter

    // Return current motion state from the owned MotionPlanner. // thin getter
    MotionState motionState() const; // for TestExecutor AtTarget checks

    // Provide host I/O snapshot (no evaluation in M5). // set inputs
    void setHostInputs(const HostInputs& inputs); // assign

    // Provide current host outputs snapshot (placeholders in M5). // query outputs
    HostOutputs hostOutputs() const; // getter

    // Start a new move to a target pose. // command move
    void moveTo(float x, float y, float z); // delegate

    // Non-blocking progress update. // step
    void tick(float dt); // no blocking

    // Pause motion if Active, resume only from Paused. // pause
    void pause(); // command
    void resume(); // command

    // Stop motion from any non-terminal state. // stop
    void stop(); // command

    // Pose getters (delegated from MotionPlanner). // read pose
    float x() const; // getter
    float y() const; // getter
    float z() const; // getter

private:
    MotionPlanner motion_planner_; // direct composition
    MachineState machine_state_; // state

    // Default motion parameters used when (re)starting motion after stopMotion(). // stored defaults
    float default_max_velocity_; // max velocity
    float default_acceleration_; // acceleration

    // Store target pose for resume after pause. // stored target
    float stored_target_x_; // stored X
    float stored_target_y_; // stored Y
    float stored_target_z_; // stored Z

    // Host I/O containers (no evaluation). // placeholders
    HostInputs host_inputs_; // last inputs
    HostOutputs host_outputs_; // produced outputs
};

