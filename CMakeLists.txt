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

# Build the PC Simulation executable that links against Core.
add_subdirectory(simulation)
