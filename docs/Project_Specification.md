# Automated Tensile Testing Rig

## Project Goal

Development of a 3-axis automated tensile testing rig.

The machine consists of three linear axes:
- X axis
- Y axis
- Z axis

The Z axis performs the tensile test movement.
A force sensor measures the tensile force.

The system shall be controlled automatically using predefined test points.

The final system consists of:

- Raspberry Pi 5
- Node.js web GUI
- STM32F446RE
- 3-axis stepper motor system
- force sensor
- pneumatic components
- Nanotec/stepper motor drivers

The Raspberry Pi is responsible for the user interface and high-level communication.

The STM32 is responsible for real-time machine control.

## System Responsibilities

### Raspberry Pi

The Raspberry Pi runs:
- Node.js web server
- GUI
- user interaction
- loading and validating JSON test programs
- communication with STM32

The Raspberry Pi is NOT responsible for:
- real-time motor control
- force control
- safety-critical motion control

### STM32

The STM32 runs:
- machine state machine
- axis control
- motion planning
- force control
- test execution
- sensor acquisition
- emergency stop handling
- communication with Raspberry Pi

The STM32 must remain operational even if the GUI connection is interrupted.