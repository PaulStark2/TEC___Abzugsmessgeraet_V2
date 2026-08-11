# CMake definition for the platform-independent Core library.
add_library(core STATIC
    # Trivial Core source used by Milestone 1.
    core.cpp
    # Single-axis motion model for Milestone 2.
    Axis.cpp
    # Multi-axis pose motion for Milestone 4.
    MotionPlanner.cpp
    # Machine state machine layer for Milestone 5.
    Machine.cpp
    # Test program sequencing for Milestone 7.
    TestExecutor.cpp
)

# Expose the core/ directory so dependents can include Core headers.
target_include_directories(core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
