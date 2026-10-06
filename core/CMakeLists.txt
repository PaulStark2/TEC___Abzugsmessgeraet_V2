# CMake definition for the platform-independent Core library.
add_library(core STATIC
    core.cpp
    Axis.cpp
    MotionPlanner.cpp
    Machine.cpp
    TestExecutor.cpp
    ProcedureController.cpp
    ProbeCalibration.cpp
    ProbeSearch.cpp
    MachineConfig.cpp
    TestConfiguration.cpp
    ForceCalibration.cpp
)

target_include_directories(core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
