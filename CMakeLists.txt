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

if(NOT DEFINED SDKCONFIG_DEFAULTS AND NOT DEFINED ENV{SDKCONFIG_DEFAULTS})
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
        set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
    else()
        set(SDKCONFIG_DEFAULTS "profiles/sdkconfig.defaults")
    endif()
elseif(DEFINED SDKCONFIG_DEFAULTS AND SDKCONFIG_DEFAULTS STREQUAL "sdkconfig.defaults" AND NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
    set(SDKCONFIG_DEFAULTS "profiles/sdkconfig.defaults")
endif()

if(EXTRA_CONFIGS)
    if(DEFINED ENV{SDKCONFIG_DEFAULTS})
        set(SDKCONFIG_DEFAULTS "$ENV{SDKCONFIG_DEFAULTS};${EXTRA_CONFIGS}")
    else()
        set(SDKCONFIG_DEFAULTS "${SDKCONFIG_DEFAULTS};${EXTRA_CONFIGS}")
    endif()
endif()

if(NOT SDKCONFIG_DEFAULTS MATCHES "sdkconfig\\.pins\\.")
    set(PRG32_CANDIDATE_SDKCONFIG "")
    if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/sdkconfig")
        set(PRG32_CANDIDATE_SDKCONFIG "${CMAKE_CURRENT_BINARY_DIR}/sdkconfig")
    elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/build-esp32c6/sdkconfig")
        set(PRG32_CANDIDATE_SDKCONFIG "${CMAKE_CURRENT_LIST_DIR}/build-esp32c6/sdkconfig")
    elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig")
        set(PRG32_CANDIDATE_SDKCONFIG "${CMAKE_CURRENT_LIST_DIR}/sdkconfig")
    endif()

    if(PRG32_CANDIDATE_SDKCONFIG)
        file(STRINGS "${PRG32_CANDIDATE_SDKCONFIG}" PRG32_SDK_LINES)
        set(PRG32_IS_CUSTOM FALSE)
        set(PRG32_AUTOPROFILE "")
        foreach(PRG32_LINE ${PRG32_SDK_LINES})
            if(PRG32_LINE STREQUAL "CONFIG_PRG32_PIN_PROFILE_CUSTOM=y")
                set(PRG32_IS_CUSTOM TRUE)
            elseif(PRG32_LINE MATCHES "^CONFIG_PRG32_DEFAULT_PIN_PROFILE=\"([^\"]+)\"")
                set(PRG32_AUTOPROFILE "${CMAKE_MATCH_1}")
            elseif(PRG32_LINE STREQUAL "CONFIG_PRG32_PIN_PROFILE_PROTOTYPE1=y" AND NOT PRG32_AUTOPROFILE)
                set(PRG32_AUTOPROFILE "prototype1")
            elseif(PRG32_LINE STREQUAL "CONFIG_PRG32_PIN_PROFILE_PROTOTYPE2=y" AND NOT PRG32_AUTOPROFILE)
                set(PRG32_AUTOPROFILE "prototype2")
            endif()
        endforeach()

        if(NOT PRG32_IS_CUSTOM AND PRG32_AUTOPROFILE AND NOT PRG32_AUTOPROFILE STREQUAL "none")
            set(PRG32_PROFILE_FILE "${CMAKE_CURRENT_LIST_DIR}/profiles/sdkconfig.pins.${PRG32_AUTOPROFILE}")
            if(EXISTS "${PRG32_PROFILE_FILE}")
                set(SDKCONFIG_DEFAULTS "${SDKCONFIG_DEFAULTS};${PRG32_PROFILE_FILE}")
            endif()
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

if(IDF_TARGET STREQUAL "esp32c6" AND NOT CONFIG_PRG32_PINS_CONFIGURED)
    message(FATAL_ERROR
        "\n=================================================================\n"
        "ERROR: No PRG32 hardware pin profile configured!\n"
        "Pins are no longer defaulted in Kconfig.\n"
        "Specify a pin profile when building, e.g.:\n"
        "  python3 -m prg32 esp32c6 build --pin-profile prototype1\n"
        "Or include a pin profile in SDKCONFIG_DEFAULTS:\n"
        "  -D SDKCONFIG_DEFAULTS=\"...;profiles/sdkconfig.pins.prototype1\"\n"
        "Or select Custom in idf.py menuconfig.\n"
        "=================================================================\n"
    )
endif()
