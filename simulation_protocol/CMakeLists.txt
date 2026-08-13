# M13 binary M12 protocol TCP host (port 8766 default).
# Also hosts the M15 simulation plant channel (port 8767 default).
add_executable(simulation_protocol
    main.cpp
    PlantChannelServer.cpp
)

target_link_libraries(simulation_protocol
    PRIVATE
        simulation_bridge
)

if(WIN32)
    target_link_libraries(simulation_protocol
        PRIVATE
            ws2_32
    )
endif()

if(MINGW)
    target_link_options(simulation_protocol PRIVATE
        -static-libgcc
        -static-libstdc++
        -static
    )
endif()

set_target_properties(simulation_protocol
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/simulation_protocol"
)
