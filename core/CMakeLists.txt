# CMake definition for the platform-independent Core library.
add_library(core STATIC
    # Trivial Core source used by Milestone 1.
    core.cpp
)

# Expose the core/ directory so dependents can include Core headers.
target_include_directories(core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
