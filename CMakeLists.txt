cmake_minimum_required(VERSION 3.15...3.31)

# Set up output of compile commands
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Set up language configuration
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Pull in SDK from submodule
set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/ext/picoinf/ext/pico-sdk")
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

# Name project
project(TraquitoJetpack LANGUAGES C CXX ASM)

# Configure and add picoinf
set(APP_BUILD_VERSION "2025-02-05 10:31:00")
set(PICO_BOARD "pico")
set(PICO_INF_ENABLE_JERRYSCRIPT 1)
add_subdirectory(ext/picoinf)

# Add source
add_subdirectory(src)
