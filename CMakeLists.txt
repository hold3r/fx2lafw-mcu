cmake_minimum_required(VERSION 3.22)

# Project settings.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

set(FX2LAFW_BOARD "stm32f411_blackpill" CACHE STRING "Board directory in src/boards")
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/boards/${FX2LAFW_BOARD}/CMakeLists.txt")
    message(FATAL_ERROR "Unknown board '${FX2LAFW_BOARD}'. Expected src/boards/${FX2LAFW_BOARD}/CMakeLists.txt")
endif()

# Match the workspace name used by STM32CubeIDE Build Analyzer.
project(fx2lafw-mcu LANGUAGES C ASM)
message(STATUS "Board: ${FX2LAFW_BOARD}; build type: ${CMAKE_BUILD_TYPE}")

# Shared application sources.
add_executable(${PROJECT_NAME}
    src/main.c
    src/fx2lafw_protocol.c
    src/usb_descriptors.c
    src/runtime/syscalls.c
    src/runtime/sysmem.c
)

# This INTERFACE target shares headers and defines without compiling any sources.
add_library(fx2lafw_config INTERFACE)
target_include_directories(fx2lafw_config INTERFACE
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src/config"
)
target_compile_definitions(fx2lafw_config INTERFACE $<$<CONFIG:Debug>:DEBUG>)

# Load the board before FreeRTOS so it can select the CPU port.
add_subdirectory(src/boards/${FX2LAFW_BOARD})
target_link_libraries(fx2lafw_config INTERFACE fx2lafw_board)

# FreeRTOS requires a target named freertos_config for its configuration.
add_library(freertos_config INTERFACE)
target_link_libraries(freertos_config INTERFACE fx2lafw_config)
add_subdirectory(lib/FreeRTOS-Kernel)

add_subdirectory(lib/tinyusb/src)
tinyusb_target_add(${PROJECT_NAME})
target_link_libraries(${PROJECT_NAME} PRIVATE
    fx2lafw_config freertos_kernel ${TOOLCHAIN_LINK_LIBRARIES}
)

# Firmware output and linker settings.
target_link_options(${PROJECT_NAME} PRIVATE
    "-Wl,-Map=${PROJECT_NAME}.map"
    -Wl,--gc-sections
    -Wl,--print-memory-usage
)
set_target_properties(${PROJECT_NAME} PROPERTIES ADDITIONAL_CLEAN_FILES "${PROJECT_NAME}.map")
