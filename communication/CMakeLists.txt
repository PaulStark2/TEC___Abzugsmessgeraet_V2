# Platform-independent M12 communication protocol library (no core dependency).
add_library(communication STATIC
    Crc16.cpp
    Decoder.cpp
    Encoder.cpp
    PayloadCodec.cpp
    WireUtil.cpp
)

target_include_directories(communication
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(communication PUBLIC cxx_std_17)
