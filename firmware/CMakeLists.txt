cmake_minimum_required(VERSION 3.16)

if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

project(tec_firmware C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

set(TEC_MCU_FLAGS -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)

add_compile_options(
    ${TEC_MCU_FLAGS}
    -Wall
    -Wextra
    -Werror=return-type
    -ffunction-sections
    -fdata-sections
)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
add_compile_definitions(STM32F446xx USE_HAL_DRIVER)

# Host UART for Phase-2.1 bench and later M12. One instance only — never dual M12.
# Bring-up default: USART2 (Nucleo ST-Link VCP). Production Pi: -DTEC_HOST_UART=USART3.
set(TEC_HOST_UART "USART2" CACHE STRING "Host UART: USART2 (ST-Link VCP) or USART3 (Pi PC10/PC11)")
set_property(CACHE TEC_HOST_UART PROPERTY STRINGS USART2 USART3)
if(TEC_HOST_UART STREQUAL "USART2")
    add_compile_definitions(TEC_HOST_UART_USART2=1)
elseif(TEC_HOST_UART STREQUAL "USART3")
    add_compile_definitions(TEC_HOST_UART_USART3=1)
else()
    message(FATAL_ERROR "TEC_HOST_UART must be USART2 or USART3 (got '${TEC_HOST_UART}')")
endif()
message(STATUS "TEC_HOST_UART=${TEC_HOST_UART}")

add_link_options(
    ${TEC_MCU_FLAGS}
    -T${CMAKE_CURRENT_SOURCE_DIR}/linker/STM32F446RETX_FLASH.ld
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/tec_firmware.map
    --specs=nano.specs
    --specs=nosys.specs
)

include(FetchContent)

FetchContent_Declare(
    cmsis_core
    GIT_REPOSITORY https://github.com/ARM-software/CMSIS_5.git
    GIT_TAG 5.9.0
    GIT_SHALLOW TRUE
)
FetchContent_Declare(
    cmsis_device_f4
    GIT_REPOSITORY https://github.com/STMicroelectronics/cmsis_device_f4.git
    GIT_TAG v2.6.10
    GIT_SHALLOW TRUE
)
FetchContent_Declare(
    stm32f4xx_hal
    GIT_REPOSITORY https://github.com/STMicroelectronics/stm32f4xx_hal_driver.git
    GIT_TAG v1.8.3
    GIT_SHALLOW TRUE
)

FetchContent_GetProperties(cmsis_core)
if(NOT cmsis_core_POPULATED)
    FetchContent_Populate(cmsis_core)
endif()
FetchContent_GetProperties(cmsis_device_f4)
if(NOT cmsis_device_f4_POPULATED)
    FetchContent_Populate(cmsis_device_f4)
endif()
FetchContent_GetProperties(stm32f4xx_hal)
if(NOT stm32f4xx_hal_POPULATED)
    FetchContent_Populate(stm32f4xx_hal)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../core ${CMAKE_CURRENT_BINARY_DIR}/core)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../communication ${CMAKE_CURRENT_BINARY_DIR}/communication)

set(TEC_SIM_BRIDGE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../simulation_bridge)

add_library(stm32f4_hal STATIC
    ${stm32f4xx_hal_SOURCE_DIR}/Src/stm32f4xx_hal.c
    ${stm32f4xx_hal_SOURCE_DIR}/Src/stm32f4xx_hal_cortex.c
    ${stm32f4xx_hal_SOURCE_DIR}/Src/stm32f4xx_hal_rcc.c
    ${stm32f4xx_hal_SOURCE_DIR}/Src/stm32f4xx_hal_rcc_ex.c
    ${stm32f4xx_hal_SOURCE_DIR}/Src/stm32f4xx_hal_gpio.c
    ${stm32f4xx_hal_SOURCE_DIR}/Src/stm32f4xx_hal_pwr.c
    ${stm32f4xx_hal_SOURCE_DIR}/Src/stm32f4xx_hal_pwr_ex.c
    ${stm32f4xx_hal_SOURCE_DIR}/Src/stm32f4xx_hal_flash.c
    ${stm32f4xx_hal_SOURCE_DIR}/Src/stm32f4xx_hal_flash_ex.c
    ${stm32f4xx_hal_SOURCE_DIR}/Src/stm32f4xx_hal_iwdg.c
    ${stm32f4xx_hal_SOURCE_DIR}/Src/stm32f4xx_hal_dma.c
    ${stm32f4xx_hal_SOURCE_DIR}/Src/stm32f4xx_hal_adc.c
    ${stm32f4xx_hal_SOURCE_DIR}/Src/stm32f4xx_hal_adc_ex.c
    ${stm32f4xx_hal_SOURCE_DIR}/Src/stm32f4xx_hal_uart.c
    ${stm32f4xx_hal_SOURCE_DIR}/Src/stm32f4xx_hal_tim.c
    ${stm32f4xx_hal_SOURCE_DIR}/Src/stm32f4xx_hal_tim_ex.c
    ${cmsis_device_f4_SOURCE_DIR}/Source/Templates/system_stm32f4xx.c
)
target_compile_options(stm32f4_hal PRIVATE -Wno-unused-parameter)
target_include_directories(stm32f4_hal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${stm32f4xx_hal_SOURCE_DIR}/Inc
    ${stm32f4xx_hal_SOURCE_DIR}/Inc/Legacy
    ${cmsis_device_f4_SOURCE_DIR}/Include
    ${cmsis_core_SOURCE_DIR}/CMSIS/Core/Include
)

add_executable(tec_firmware
    src/main.cpp
    src/FirmwareHost.cpp
    src/HostIo.cpp
    src/StepGenerator.cpp
    src/FixedFrameBuffer.cpp
    src/syscalls.cpp
    ${TEC_SIM_BRIDGE_DIR}/CommandDispatcher.cpp
    ${TEC_SIM_BRIDGE_DIR}/StatusSnapshot.cpp
    ${TEC_SIM_BRIDGE_DIR}/ForceCalibHost.cpp
    ${cmsis_device_f4_SOURCE_DIR}/Source/Templates/gcc/startup_stm32f446xx.s
)
target_include_directories(tec_firmware PRIVATE include ${TEC_SIM_BRIDGE_DIR})
target_link_libraries(tec_firmware PRIVATE core communication stm32f4_hal)
set_target_properties(tec_firmware PROPERTIES OUTPUT_NAME tec_firmware SUFFIX ".elf")

add_custom_command(TARGET tec_firmware POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:tec_firmware> ${CMAKE_CURRENT_BINARY_DIR}/tec_firmware.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:tec_firmware> ${CMAKE_CURRENT_BINARY_DIR}/tec_firmware.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:tec_firmware>
    COMMENT "Generate hex/bin and print size"
)
