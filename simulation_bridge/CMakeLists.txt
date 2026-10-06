# M13 simulation protocol bridge (maps M12 frames to Core APIs).
add_library(simulation_bridge STATIC
    FrameStreamBuffer.cpp
    SimHostInputProvider.cpp
    StatusSnapshot.cpp
    CommandDispatcher.cpp
    ForceCalibHost.cpp
    SimProtocolBridge.cpp
)

target_include_directories(simulation_bridge
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(simulation_bridge
    PUBLIC
        core
        communication
        simulation_plant
)

target_compile_features(simulation_bridge PUBLIC cxx_std_17)
