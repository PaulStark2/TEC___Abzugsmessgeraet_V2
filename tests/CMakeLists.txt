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

# CMake definition for Core unit tests of Machine (Milestone 5).
add_executable(machine_tests
    # Deterministic tests for the Core Machine class.
    machine_tests.cpp
)

# Link tests against the Core library.
target_link_libraries(machine_tests
    PRIVATE
        core
)

# Register the Machine tests with CTest.
add_test(NAME machine_tests COMMAND machine_tests)

# CMake definition for Core unit tests of TestProgram (Milestone 6).
add_executable(testprogram_tests
    # Deterministic tests for TestPoint / TestProgram data containers.
    testprogram_tests.cpp
)

# Link tests against the Core library (header-only types live under core/).
target_link_libraries(testprogram_tests
    PRIVATE
        core
)

# Register the TestProgram tests with CTest.
add_test(NAME testprogram_tests COMMAND testprogram_tests)

# CMake definition for Core unit tests of TestExecutor (Milestone 7).
add_executable(testexecutor_tests
    # Deterministic tests for TestExecutor process sequencing.
    testexecutor_tests.cpp
)

# Link tests against the Core library.
target_link_libraries(testexecutor_tests
    PRIVATE
        core
)

# Register the TestExecutor tests with CTest.
add_test(NAME testexecutor_tests COMMAND testexecutor_tests)
