# Copy of firmware/cmake/arm-none-eabi-stm32f446.cmake as of 2026-10-06.
# Not synchronized automatically. The electronics test builds only from this folder.
# cmake -S firmware_hwtest -B firmware_hwtest/build --toolchain firmware_hwtest/cmake/arm-none-eabi-stm32f446.cmake

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(TEC_ARM_PREFIX "arm-none-eabi")

find_program(CMAKE_C_COMPILER NAMES ${TEC_ARM_PREFIX}-gcc REQUIRED)
find_program(CMAKE_CXX_COMPILER NAMES ${TEC_ARM_PREFIX}-g++ REQUIRED)
find_program(CMAKE_ASM_COMPILER NAMES ${TEC_ARM_PREFIX}-gcc REQUIRED)
find_program(CMAKE_AR NAMES ${TEC_ARM_PREFIX}-gcc-ar REQUIRED)
find_program(CMAKE_RANLIB NAMES ${TEC_ARM_PREFIX}-gcc-ranlib REQUIRED)
find_program(CMAKE_OBJCOPY NAMES ${TEC_ARM_PREFIX}-objcopy REQUIRED)
find_program(CMAKE_SIZE NAMES ${TEC_ARM_PREFIX}-size REQUIRED)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
