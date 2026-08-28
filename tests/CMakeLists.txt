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

add_executable(limit_blocking_tests
    limit_blocking_tests.cpp
)
target_link_libraries(limit_blocking_tests PRIVATE core)
add_test(NAME limit_blocking_tests COMMAND limit_blocking_tests)

add_executable(homing_tests
    homing_tests.cpp
)
target_link_libraries(homing_tests PRIVATE core)
add_test(NAME homing_tests COMMAND homing_tests)

add_executable(config_validation_tests
    config_validation_tests.cpp
)
target_link_libraries(config_validation_tests PRIVATE core)
add_test(NAME config_validation_tests COMMAND config_validation_tests)

add_executable(collision_context_tests
    collision_context_tests.cpp
)
target_link_libraries(collision_context_tests PRIVATE core)
add_test(NAME collision_context_tests COMMAND collision_context_tests)

add_executable(spreader_timing_tests
    spreader_timing_tests.cpp
)
target_link_libraries(spreader_timing_tests PRIVATE core)
add_test(NAME spreader_timing_tests COMMAND spreader_timing_tests)

add_executable(spreader_safety_tests
    spreader_safety_tests.cpp
)
target_link_libraries(spreader_safety_tests PRIVATE simulation_bridge)
add_test(NAME spreader_safety_tests COMMAND spreader_safety_tests)

add_executable(communication_watchdog_tests
    communication_watchdog_tests.cpp
)
target_link_libraries(communication_watchdog_tests PRIVATE core)
add_test(NAME communication_watchdog_tests COMMAND communication_watchdog_tests)

add_executable(runtime_lock_tests
    runtime_lock_tests.cpp
)
target_link_libraries(runtime_lock_tests PRIVATE simulation_bridge)
add_test(NAME runtime_lock_tests COMMAND runtime_lock_tests)

add_executable(preclamp_tests
    preclamp_tests.cpp
)
target_link_libraries(preclamp_tests PRIVATE core)
add_test(NAME preclamp_tests COMMAND preclamp_tests)

add_executable(transform_tests
    transform_tests.cpp
)
target_link_libraries(transform_tests PRIVATE core)
add_test(NAME transform_tests COMMAND transform_tests)

add_executable(probe_geometry_tests
    probe_geometry_tests.cpp
)
target_link_libraries(probe_geometry_tests PRIVATE core)
add_test(NAME probe_geometry_tests COMMAND probe_geometry_tests)

add_executable(probe_search_tests
    probe_search_tests.cpp
)
target_link_libraries(probe_search_tests PRIVATE core)
add_test(NAME probe_search_tests COMMAND probe_search_tests)

add_executable(probe_calibration_tests
    probe_calibration_tests.cpp
)
target_link_libraries(probe_calibration_tests PRIVATE core)
add_test(NAME probe_calibration_tests COMMAND probe_calibration_tests)

add_executable(procedure_phase_tests
    procedure_phase_tests.cpp
)
target_link_libraries(procedure_phase_tests PRIVATE core)
add_test(NAME procedure_phase_tests COMMAND procedure_phase_tests)

add_executable(procedure_probe_tests
    procedure_probe_tests.cpp
)
target_link_libraries(procedure_probe_tests PRIVATE core)
add_test(NAME procedure_probe_tests COMMAND procedure_probe_tests)

add_executable(force_path_recorder_tests
    force_path_recorder_tests.cpp
)
target_link_libraries(force_path_recorder_tests PRIVATE core)
add_test(NAME force_path_recorder_tests COMMAND force_path_recorder_tests)

add_executable(procedure_insert_tests
    procedure_insert_tests.cpp
)
target_link_libraries(procedure_insert_tests PRIVATE core)
add_test(NAME procedure_insert_tests COMMAND procedure_insert_tests)

add_executable(safe_z_tests
    safe_z_tests.cpp
)
target_link_libraries(safe_z_tests PRIVATE core)
add_test(NAME safe_z_tests COMMAND safe_z_tests)

add_executable(homing_start_tests
    homing_start_tests.cpp
)
target_link_libraries(homing_start_tests PRIVATE simulation_bridge)
add_test(NAME homing_start_tests COMMAND homing_start_tests)

add_executable(force_safety_tests
    force_safety_tests.cpp
)
target_link_libraries(force_safety_tests PRIVATE core)
add_test(NAME force_safety_tests COMMAND force_safety_tests)

add_executable(procedure_abort_tests
    procedure_abort_tests.cpp
)
target_link_libraries(procedure_abort_tests PRIVATE core)
add_test(NAME procedure_abort_tests COMMAND procedure_abort_tests)

add_executable(procedure_runtime_e2e_tests
    procedure_runtime_e2e_tests.cpp
)
target_link_libraries(procedure_runtime_e2e_tests PRIVATE core)
add_test(NAME procedure_runtime_e2e_tests COMMAND procedure_runtime_e2e_tests)

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

add_executable(mcs_rehome_verify
    mcs_rehome_verify.cpp
)
target_link_libraries(mcs_rehome_verify PRIVATE simulation_bridge)

add_executable(coord_frame_diagnose
    coord_frame_diagnose.cpp
)
target_link_libraries(coord_frame_diagnose PRIVATE simulation_bridge)

add_executable(m19_frame_diagnose
    m19_frame_diagnose.cpp
)
target_link_libraries(m19_frame_diagnose PRIVATE simulation_bridge)

add_executable(m19_trigger_frame_tests
    m19_trigger_frame_tests.cpp
)
target_link_libraries(m19_trigger_frame_tests PRIVATE simulation_bridge)
add_test(NAME m19_trigger_frame_tests COMMAND m19_trigger_frame_tests)

add_executable(procedure_start_homing_regression_tests
    procedure_start_homing_regression_tests.cpp
)
target_link_libraries(procedure_start_homing_regression_tests PRIVATE simulation_bridge)
add_test(NAME procedure_start_homing_regression_tests COMMAND procedure_start_homing_regression_tests)

add_executable(bridge_integration_tests
    bridge_integration_tests.cpp
)
target_link_libraries(bridge_integration_tests PRIVATE simulation_bridge)
add_test(NAME bridge_integration_tests COMMAND bridge_integration_tests)

add_executable(process_force_model_tests
    process_force_model_tests.cpp
)
target_link_libraries(process_force_model_tests PRIVATE simulation_plant)
add_test(NAME process_force_model_tests COMMAND process_force_model_tests)

add_executable(procedure_sim_phases_force_e2e_tests
    procedure_sim_phases_force_e2e_tests.cpp
)
target_link_libraries(procedure_sim_phases_force_e2e_tests PRIVATE simulation_bridge)
add_test(NAME procedure_sim_phases_force_e2e_tests COMMAND procedure_sim_phases_force_e2e_tests)

# M15 virtual outside world (sensor plant) tests.
add_executable(simplant_tests
    simplant_tests.cpp
)
target_link_libraries(simplant_tests PRIVATE simulation_plant)
add_test(NAME simplant_tests COMMAND simplant_tests)

# M15 simulation plant channel codec tests.
add_executable(plantchannel_tests
    plantchannel_tests.cpp
)
target_link_libraries(plantchannel_tests PRIVATE simulation_plant)
add_test(NAME plantchannel_tests COMMAND plantchannel_tests)

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

