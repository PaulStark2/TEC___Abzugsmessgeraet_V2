# CMake definition for the PC Simulation executable.
add_executable(simulation
    # Simulation entry point for Milestone 1.
    main.cpp
)

# Link the Simulation executable against the Core library.
target_link_libraries(simulation
    PRIVATE
        core
)
