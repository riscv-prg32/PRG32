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

set(EXTRA_CONFIGS "")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/profiles/sdkconfig.defaults.local")
    set(EXTRA_CONFIGS "${CMAKE_CURRENT_LIST_DIR}/profiles/sdkconfig.defaults.local")
endif()

if(EXTRA_CONFIGS)
    if(DEFINED ENV{SDKCONFIG_DEFAULTS})
        set(SDKCONFIG_DEFAULTS "$ENV{SDKCONFIG_DEFAULTS};${EXTRA_CONFIGS}")
    elseif(DEFINED SDKCONFIG_DEFAULTS)
        set(SDKCONFIG_DEFAULTS "${SDKCONFIG_DEFAULTS};${EXTRA_CONFIGS}")
    else()
        if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
            set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;${EXTRA_CONFIGS}")
        else()
            set(SDKCONFIG_DEFAULTS "${EXTRA_CONFIGS}")
        endif()
    endif()
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(PRG32)

idf_build_set_property(
    COMPILE_DEFINITIONS
    "PRG32_FIRMWARE_VERSION=\"${PRG32_FIRMWARE_VERSION}\""
    APPEND
)
