# bioshock - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(bioshock LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(BIOSHOCK_SOURCES
    src/main.cpp
    src/cheats.cpp
    src/give.cpp
)

if(WIN32)
    add_executable(bioshock WIN32 ${BIOSHOCK_SOURCES})
else()
    add_executable(bioshock ${BIOSHOCK_SOURCES})
endif()

rexglue_setup_target(bioshock GPU_PLUGINS xenos)

