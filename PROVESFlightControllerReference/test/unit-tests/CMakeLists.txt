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

# ADCS component (real ADCS.cpp built against the host stubs in support/: a
# recording ComponentAc base and a host FW_ASSERT).
add_library(adcs_component STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../PROVESFlightControllerReference/Components/ADCS/ADCS.cpp
)
target_include_directories(adcs_component PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/support
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)

# PowerMonitor component (real PowerMonitor.cpp built against the host stubs in
# support/: a recording ComponentAc base and the fake Fw::Time).
add_library(power_monitor_component STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../PROVESFlightControllerReference/Components/PowerMonitor/PowerMonitor.cpp
)
target_include_directories(power_monitor_component PUBLIC
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
target_link_libraries(mode_manager_component PUBLIC persisted_record_file)

# StartupManager component (real StartupManager.cpp built against the host
# stubs in support/: a recording ComponentAc base, the in-memory Os::File, a
# fake Fw::Time and a fake <zephyr/drivers/rtc.h>).
add_library(startup_manager_component STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../PROVESFlightControllerReference/Components/StartupManager/StartupManager.cpp
)
target_include_directories(startup_manager_component PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/support
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)
target_link_libraries(startup_manager_component PUBLIC persisted_record_file)

# TcFrameCorrector codec (pure logic, no F Prime / Os includes)
add_library(tc_frame_corrector_codec STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../PROVESFlightControllerReference/Components/TcFrameCorrector/TcFrameCorrectorCodec.cpp
)
target_include_directories(tc_frame_corrector_codec PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)

# TcFrameCorrector component (real TcFrameCorrector.cpp built against the host
# stubs in support/: a recording ComponentAc base with minimal Fw::Buffer and
# ComCfg::FrameContext, plus stubs for the two generated FppConstantsAc.hpp
# headers it reads the frame token from). support/ comes first on the include
# path so those resolve to the stubs, not to lib/fprime.
add_library(tc_frame_corrector_component STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../PROVESFlightControllerReference/Components/TcFrameCorrector/TcFrameCorrector.cpp
)
target_include_directories(tc_frame_corrector_component PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/support
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)
target_link_libraries(tc_frame_corrector_component PUBLIC tc_frame_corrector_codec)

# DriverBoardProtocol codec (pure logic, no F Prime / Os includes; Crc16 is
# header-only and resolves through the project-root include path)
add_library(driver_board_protocol STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../PROVESFlightControllerReference/Components/DriverBoardProtocol/DriverBoardProtocol.cpp
)
target_include_directories(driver_board_protocol PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)

# DriverBoardHandler link state machine (pure logic, no F Prime / Os includes)
add_library(driver_board_link STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../PROVESFlightControllerReference/Components/DriverBoardHandler/DriverBoardLink.cpp
)
target_include_directories(driver_board_link PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)

# DriverBoardHandler component (real DriverBoardHandler.cpp built against the
# host stubs in support/: a recording ComponentAc base whose lock()/unLock()
# count the guarded region and flag uartSend/getMode calls made under it, the
# minimal Fw::Buffer, the fake Fw::Time, and Drv::ByteStreamStatus). Links the
# pure link machine and the wire codec.
add_library(driver_board_handler_component STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../PROVESFlightControllerReference/Components/DriverBoardHandler/DriverBoardHandler.cpp
)
target_include_directories(driver_board_handler_component PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/support
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)
target_link_libraries(driver_board_handler_component PUBLIC driver_board_link driver_board_protocol)

# FaultManager fault table (pure logic, no F Prime / Os includes)
add_library(fault_manager_table STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../PROVESFlightControllerReference/Components/FaultManager/FaultTable.cpp
)
target_include_directories(fault_manager_table PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)

# FaultManager component (real FaultManager.cpp built against the host stubs
# in support/: a recording ComponentAc base whose lock()/unLock() count the
# guarded region, plus the FaultTypes enum stubs).
add_library(fault_manager_component STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../PROVESFlightControllerReference/Components/FaultManager/FaultManager.cpp
)
target_include_directories(fault_manager_component PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/support
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)
target_link_libraries(fault_manager_component PUBLIC fault_manager_table)

# Watchdog component (real Watchdog.cpp built against the host stubs in
# support/: a recording ComponentAc base, Fw::On and a forwarding FpConfig).
add_library(watchdog_component STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../PROVESFlightControllerReference/Components/Watchdog/Watchdog.cpp
)
target_include_directories(watchdog_component PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/support
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)

# TaskGate component (real TaskGate.cpp built against the host stubs in
# support/: a recording ComponentAc base and a host FW_ASSERT).
add_library(task_gate_component STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../PROVESFlightControllerReference/Components/TaskGate/TaskGate.cpp
)
target_include_directories(task_gate_component PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/support
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
        adcs_component
        power_monitor_component
        mode_manager_component
        startup_manager_component
        tc_frame_corrector_codec
        tc_frame_corrector_component
        driver_board_protocol
        driver_board_link
        driver_board_handler_component
        task_gate_component
        fault_manager_table
        fault_manager_component
        watchdog_component
    )

    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
