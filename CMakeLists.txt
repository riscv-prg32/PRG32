cmake_minimum_required(VERSION 3.16)

set(PRG32_FIRMWARE_VERSION_FALLBACK "0.0.0-dev")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --tags --dirty --always
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
        OUTPUT_VARIABLE PRG32_FIRMWARE_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if(NOT PRG32_FIRMWARE_VERSION)
    set(PRG32_FIRMWARE_VERSION "${PRG32_FIRMWARE_VERSION_FALLBACK}")
endif()
set(PROJECT_VER "${PRG32_FIRMWARE_VERSION}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(PRG32)

idf_build_set_property(
    COMPILE_DEFINITIONS
    "PRG32_FIRMWARE_VERSION=\"${PRG32_FIRMWARE_VERSION}\""
    APPEND
)
