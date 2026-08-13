# M15 virtual outside world (sensors only, no controller logic).
add_library(simulation_plant STATIC
    SimPlant.cpp
    PlantCommandCodec.cpp
)

target_include_directories(simulation_plant
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(simulation_plant
    PUBLIC
        core
)

target_compile_features(simulation_plant PUBLIC cxx_std_17)
