# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)

# set(PICO_DEOPTIMIZED_DEBUG 1)
# add_definitions(-O0)
add_definitions(-Wall)



set(PROJECT_NAME "Traquito")

set(APP_BUILD_VERSION "2024-03-14 16:39:37")


# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)
set(PICO_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../pico-sdk")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.4.0")
  message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.4.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

set(PICO_BOARD pico_w)
set(WIFI_SSID DM_WIFI_2.4)
set(WIFI_PASSWORD wifiwifi)


# Use TinyUSB with FreeRTOS
# https://www.eevblog.com/forum/microcontrollers/rp2040-how-to-use-freertos-with-tinyusb/
set(TUSB_CONFIG_H_DIR ${CMAKE_CURRENT_SOURCE_DIR})


# Pull in custom libs
set(LIB_PICO "${CMAKE_CURRENT_SOURCE_DIR}/../../lib")
add_subdirectory("${LIB_PICO}" "./LIB_PICO")


# Pull in FreeRTOS
set(FRTOS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../FreeRTOS-Kernel")
include(${FRTOS_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)


# Delcare project
project(${PROJECT_NAME} C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1
add_executable(${PROJECT_NAME} main.cpp )

pico_set_program_name(${PROJECT_NAME} "${PROJECT_NAME}")
pico_set_program_version(${PROJECT_NAME} "0.1")

# Use TinyUSB with FreeRTOS
set(TUSB_DIR ${PICO_SDK_PATH}/lib/tinyusb)
file(GLOB lib_sources ${TUSB_DIR}/*.cpp)
target_sources(${PROJECT_NAME} PRIVATE ${lib_sources})
target_include_directories(${PROJECT_NAME} PRIVATE
    ${TUSB_CONFIG_H_DIR}
    ${TUSB_DIR}/src
)

# Target FreeRTOS
target_sources(${PROJECT_NAME} PRIVATE
    ${FRTOS_PATH}/tasks.c
    ${FRTOS_PATH}/queue.c
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${LIB_PICO}
    ${FRTOS_PATH}/include
    ${FRTOS}/portable/GCC/ARM_CM0
)

# Target LittleFS
set(LITTLEFS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../littlefs")
file(GLOB lib_sources ${LITTLEFS_DIR}/*.c)
target_sources(${PROJECT_NAME} PRIVATE ${lib_sources})
target_include_directories(${PROJECT_NAME} PRIVATE
    ${LITTLEFS_DIR}
)

# Target BTstack
set(BTSTACK_ROOT ${PICO_SDK_PATH}/lib/btstack)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${BTSTACK_ROOT}/src
)

# Add the standard library to the build
target_link_libraries(${PROJECT_NAME}
    pico_stdlib
    pico_rand
    hardware_pwm
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap3
    tinyusb_device
    tinyusb_host
    pico_cyw43_arch_none
    pico_btstack_cyw43
    pico_btstack_ble
    LibPico
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    PICO_FLASH_ASSUME_CORE1_SAFE=1
)

# Add the standard include files to the build
target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts or any other standard includes, if required
)

# create map/bin/hex file etc.
pico_add_extra_outputs(${PROJECT_NAME})
