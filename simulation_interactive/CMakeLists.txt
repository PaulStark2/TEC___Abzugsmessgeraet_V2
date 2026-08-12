# CMake definition for the interactive Core Machine TCP host (M8.5).
# Temporary pre-protocol transport — not the future M12/M13 communication protocol.
add_executable(simulation_interactive
    main.cpp
)

target_link_libraries(simulation_interactive
    PRIVATE
        core
)

if(WIN32)
    target_link_libraries(simulation_interactive
        PRIVATE
            ws2_32
    )
endif()

# MinGW: make this host executable start without MinGW runtime DLLs on PATH
# (fixes STATUS_DLL_NOT_FOUND / 0xC0000135 for libgcc/libstdc++/libwinpthread).
# Prefer static GCC/libstdc++ as requested; also force static winpthread because
# std::thread otherwise keeps a dynamic dependency on libwinpthread-1.dll.
# Scoped to this target only.
if(MINGW)
    target_link_options(simulation_interactive PRIVATE
        -static-libgcc
        -static-libstdc++
        -static
    )
endif()

set_target_properties(simulation_interactive
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/simulation_interactive"
)
