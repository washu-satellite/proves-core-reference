// ======================================================================
// \title  zephyr/drivers/rtc.h (host-test fake)
// \brief  Stand-in for the Zephyr RTC driver header.
//
// FAKE. No Zephyr code is linked into host unit tests (see
// test/unit-tests/README.md); this header exists only so StartupManager.cpp,
// which includes <zephyr/drivers/rtc.h> and calls k_uptime_seconds(), compiles
// and links on the host. The uptime is a plain test-settable value.
// ======================================================================

#ifndef UnitTestSupport_zephyr_drivers_rtc_H
#define UnitTestSupport_zephyr_drivers_rtc_H

#include <cstdint>

namespace Zephyr {
namespace Test {

//! Process-wide fake uptime, in seconds, settable by tests.
inline uint32_t& uptimeSeconds() {
    static uint32_t seconds = 0;
    return seconds;
}

}  // namespace Test
}  // namespace Zephyr

//! Mirrors Zephyr's k_uptime_seconds(): seconds since boot.
inline uint32_t k_uptime_seconds() {
    return Zephyr::Test::uptimeSeconds();
}

#endif
