cmake_minimum_required(VERSION 3.20.0)

set(BOARD rpi_pico)

# include modules
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../../lib)

# pull zephyr into build
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# declare my own project build
project(app)
target_sources(app PRIVATE src/main.cpp)

# I want to pass a vector<double> around without exploding warnings
# from gcc.
# Seems as long as I'm passing args within code I compile myself,
# it should be ok to suppress these warnings.
# Since I compile all my code, and I'm not passing to
# zephry, I should be ok.
# https://community.nxp.com/t5/MCUXpresso-IDE/vector-warning/td-p/802362
# https://stackoverflow.com/questions/52020305/what-exactly-does-gccs-wpsabi-option-do-what-are-the-implications-of-supressi
zephyr_library_compile_options(-Wno-psabi)

