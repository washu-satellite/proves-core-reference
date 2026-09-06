cmake_minimum_required(VERSION 3.20)
project(proves_unit_tests CXX)

# Pin the standard: googletest and the sources under test require C++14+, and
# some host compilers (e.g. AppleClang) default lower.
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../lib/fprime/googletest googletest-build)

# --- Helper Libraries ---

# DetumbleManager BDot
add_library(detumble_manager_bdot STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../PROVESFlightControllerReference/Components/DetumbleManager/BDot.cpp
)
target_include_directories(detumble_manager_bdot PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)

# DetumbleManager Magnetorquer
add_library(detumble_manager_magnetorquer STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../PROVESFlightControllerReference/Components/DetumbleManager/Magnetorquer.cpp
)
target_include_directories(detumble_manager_magnetorquer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)

# DetumbleManager StrategySelector
add_library(detumble_manager_strategy_selector STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../PROVESFlightControllerReference/Components/DetumbleManager/StrategySelector.cpp
)
target_include_directories(detumble_manager_strategy_selector PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)

# TcSecurityDeframer Parser
add_library(security_deframer_parser STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../PROVESFlightControllerReference/Components/TcSecurityDeframer/Parser.cpp
)
target_include_directories(security_deframer_parser PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)

# TcSecurityDeframer Validator
add_library(security_deframer_validator STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../PROVESFlightControllerReference/Components/TcSecurityDeframer/Validator.cpp
)
target_include_directories(security_deframer_validator PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)

# TcSecurityDeframer Authenticator
add_library(security_deframer_authenticator STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../PROVESFlightControllerReference/Components/TcSecurityDeframer/Authenticator.cpp
)
target_include_directories(security_deframer_authenticator PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
    ${CMAKE_CURRENT_SOURCE_DIR} # for AuthDefaultKey.h
)

# RtcManager RtcHelper
add_library(rtc_manager_rtc_helper STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../PROVESFlightControllerReference/Components/Drv/RtcManager/RtcHelper.cpp
)
target_include_directories(rtc_manager_rtc_helper PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)

# ProvesRouter Bypasser
add_library(proves_router_bypasser STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../PROVESFlightControllerReference/Components/ProvesRouter/Bypasser.cpp
)
target_include_directories(proves_router_bypasser PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)

# Find PSA provider (we use libmbedcrypto) and ensure PSA headers exist
find_path(PSA_CRYPTO_H psa/crypto.h)
find_library(MBEDCRYPTO_LIB mbedcrypto)

if(NOT PSA_CRYPTO_H)
    message(FATAL_ERROR "psa/crypto.h not found; install Mbed TLS PSA provider headers or set CMAKE_INCLUDE_PATH to the directory containing psa/crypto.h")
endif()

if(NOT MBEDCRYPTO_LIB)
    message(FATAL_ERROR "mbedcrypto library not found; install mbedtls (brew install mbedtls) or provide library in LIBRARY_PATH")
endif()

target_include_directories(security_deframer_authenticator PUBLIC ${PSA_CRYPTO_H})
target_link_libraries(security_deframer_authenticator PUBLIC ${MBEDCRYPTO_LIB})

# PersistedRecord codec (pure logic, no F Prime / Os includes)
add_library(persisted_record_codec STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../PROVESFlightControllerReference/Components/PersistedRecord/PersistedRecordCodec.cpp
)
target_include_directories(persisted_record_codec PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)

# PersistedRecord file layer (real PersistedRecordFile.cpp built against the
# in-memory Os::File / Os::FileSystem stubs in support/). support/ comes first
# on the include path so "Os/File.hpp" resolves to the stub, not lib/fprime.
add_library(persisted_record_file STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../PROVESFlightControllerReference/Components/PersistedRecord/PersistedRecordFile.cpp
)
target_include_directories(persisted_record_file PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/support
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)
target_link_libraries(persisted_record_file PUBLIC persisted_record_codec)

# TelemetryGate component (real TelemetryGate.cpp built against the host stubs
# in support/: a recording ComponentAc base and an in-memory Os::File)
add_library(telemetry_gate_component STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../PROVESFlightControllerReference/Components/TelemetryGate/TelemetryGate.cpp
)
target_include_directories(telemetry_gate_component PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/support
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)
target_link_libraries(telemetry_gate_component PUBLIC persisted_record_file)

# ThermalManager component (real ThermalManager.cpp built against the host
# stubs in support/: a recording ComponentAc base and a host FW_ASSERT).
add_library(thermal_manager_component STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../PROVESFlightControllerReference/Components/ThermalManager/ThermalManager.cpp
)
target_include_directories(thermal_manager_component PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/support
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)

# ModeManager component (real ModeManager.cpp built against the host stubs in
# support/: a recording ComponentAc base, the in-memory Os::File, and the Fw
# string / assert stubs).
add_library(mode_manager_component STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../PROVESFlightControllerReference/Components/ModeManager/ModeManager.cpp
)
target_include_directories(mode_manager_component PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/support
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)

# --- Auto-discover and build tests ---

file(GLOB TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")

foreach(test_src ${TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)

    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name}
        gtest_main
        detumble_manager_bdot
        detumble_manager_magnetorquer
        detumble_manager_strategy_selector
        security_deframer_parser
        security_deframer_validator
        security_deframer_authenticator
        rtc_manager_rtc_helper
        proves_router_bypasser
        persisted_record_codec
        persisted_record_file
        telemetry_gate_component
        thermal_manager_component
        mode_manager_component
    )

    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
