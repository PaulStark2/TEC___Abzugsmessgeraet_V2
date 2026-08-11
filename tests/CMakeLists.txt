# CMake definition for Core unit tests.
add_executable(axis_tests
    # Deterministic tests for the Axis class.
    axis_tests.cpp
)

# Link tests against the Core library.
target_link_libraries(axis_tests
    PRIVATE
        core
)

# Register the Axis tests with CTest.
add_test(NAME axis_tests COMMAND axis_tests)

# CMake definition for Core unit tests of MotionPlanner.
add_executable(motionplanner_tests
    # Deterministic tests for the Core MotionPlanner class.
    motionplanner_tests.cpp
)

# Link tests against the Core library.
target_link_libraries(motionplanner_tests
    PRIVATE
        core
)

# Register the MotionPlanner tests with CTest.
add_test(NAME motionplanner_tests COMMAND motionplanner_tests)
