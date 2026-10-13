cmake_minimum_required(VERSION 3.22)

project(Studio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(STUDIO_BUILD_APP   "Build the desktop app"           ON)
option(STUDIO_BUILD_CLI   "Build the studio-cli tool"       ON)
option(STUDIO_BUILD_TESTS "Build the unit test runner"      ON)
option(STUDIO_ENABLE_LTO  "Link-time optimisation (release builds)" OFF)
set(STUDIO_JUCE_DIR "" CACHE PATH "Use a local JUCE checkout instead of downloading one")

include(cmake/Dependencies.cmake)

add_subdirectory(source/core)

if(STUDIO_BUILD_APP)
    add_subdirectory(source/app)
endif()

if(STUDIO_BUILD_CLI)
    add_subdirectory(source/cli)
endif()

if(STUDIO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
