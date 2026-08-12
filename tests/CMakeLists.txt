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

# CMake definition for Core unit tests of HostInputs (Milestone 9).
add_executable(hostinputs_tests
    hostinputs_tests.cpp
)

target_link_libraries(hostinputs_tests
    PRIVATE
        core
)

add_test(NAME hostinputs_tests COMMAND hostinputs_tests)

# CMake definition for Core unit tests of force control (Milestone 10).
add_executable(forcecontrol_tests
    forcecontrol_tests.cpp
)

target_link_libraries(forcecontrol_tests
    PRIVATE
    core
)

add_test(NAME forcecontrol_tests COMMAND forcecontrol_tests)

# CMake definition for Core unit tests of safety (Milestone 11).
add_executable(safety_tests
    safety_tests.cpp
)

target_link_libraries(safety_tests
    PRIVATE
    core
)

add_test(NAME safety_tests COMMAND safety_tests)

# CMake definition for M12 communication protocol tests.
add_executable(protocol_tests
    protocol_tests.cpp
)

target_link_libraries(protocol_tests
    PRIVATE
        communication
)

add_test(NAME protocol_tests COMMAND protocol_tests)

# M13 simulation bridge tests.
add_executable(bridge_stream_tests
    bridge_stream_tests.cpp
)
target_link_libraries(bridge_stream_tests PRIVATE simulation_bridge)
add_test(NAME bridge_stream_tests COMMAND bridge_stream_tests)

add_executable(bridge_status_tests
    bridge_status_tests.cpp
)
target_link_libraries(bridge_status_tests PRIVATE simulation_bridge)
add_test(NAME bridge_status_tests COMMAND bridge_status_tests)

add_executable(bridge_command_tests
    bridge_command_tests.cpp
)
target_link_libraries(bridge_command_tests PRIVATE simulation_bridge)
add_test(NAME bridge_command_tests COMMAND bridge_command_tests)

add_executable(bridge_integration_tests
    bridge_integration_tests.cpp
)
target_link_libraries(bridge_integration_tests PRIVATE simulation_bridge)
add_test(NAME bridge_integration_tests COMMAND bridge_integration_tests)

add_executable(bridge_tcp_tests
    bridge_tcp_tests.cpp
)
target_link_libraries(bridge_tcp_tests PRIVATE communication)
if(WIN32)
    target_link_libraries(bridge_tcp_tests PRIVATE ws2_32)
endif()
add_dependencies(bridge_tcp_tests simulation_protocol)
target_compile_definitions(bridge_tcp_tests PRIVATE
    SIMULATION_PROTOCOL_EXE="$<TARGET_FILE:simulation_protocol>"
)
add_test(NAME bridge_tcp_tests COMMAND bridge_tcp_tests)
set_tests_properties(bridge_tcp_tests PROPERTIES TIMEOUT 30)

