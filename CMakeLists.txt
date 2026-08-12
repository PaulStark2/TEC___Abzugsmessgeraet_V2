# Root CMake project for the tensile testing rig software.
cmake_minimum_required(VERSION 3.16)

# Define the top-level project name.
project(TEC_Abzugsmessgeraet_V2 LANGUAGES CXX)

# Require C++17 as specified by the implementation roadmap.
set(CMAKE_CXX_STANDARD 17)

# Enforce the selected C++ standard (do not silently fall back).
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Disable compiler-specific C++ extensions for portability.
set(CMAKE_CXX_EXTENSIONS OFF)

# Build the platform-independent Core library.
add_subdirectory(core)

# Build the M12 host-agnostic communication protocol library.
add_subdirectory(communication)

# Build the PC Simulation executable that links against Core.
add_subdirectory(simulation)

# Build the interactive Machine TCP host (M8.5 pre-protocol bridge).
add_subdirectory(simulation_interactive)

# Build the M13 M12 binary protocol simulation bridge.
add_subdirectory(simulation_bridge)
add_subdirectory(simulation_protocol)

# Enable CTest and build Core unit tests.
enable_testing()
add_subdirectory(tests)
