cmake_minimum_required(VERSION 3.13)

# Locate LibPico and pull in initial cmake work
set(LIB_PICO "${CMAKE_CURRENT_SOURCE_DIR}/../../lib")
include(${LIB_PICO}/cmake.preproject.cmake)

# Primary configuration for this project
set(PROJECT_NAME "Traquito")
set(APP_BUILD_VERSION "2024-03-19 16:04:22")
set(PICO_BOARD_ACTUAL "pico")

# Final cmake work
include(${LIB_PICO}/cmake.app.after.cmake)