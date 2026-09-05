// ======================================================================
// \title  Fw/Types/Assert.hpp (host-test stub)
// \brief  Stand-in for the F Prime assert header for host unit tests.
//
// Real F Prime FW_ASSERT takes a condition plus up to six diagnostic
// arguments and routes a failure through Fw::AssertHook. On the host we only
// need the condition to abort the test process, so the diagnostic arguments
// are discarded unevaluated — deliberately, because callers pass FPP enum
// class objects (e.g. ThermalManager.cpp:74 passes a TempSensorType) which
// cannot legally cross a C variadic.
// ======================================================================

#ifndef UnitTestSupport_Fw_Types_Assert_HPP
#define UnitTestSupport_Fw_Types_Assert_HPP

#include <cassert>

//! Keep only the first macro argument (the condition); the trailing 0 makes
//! the variadic tail non-empty for the single-argument form.
#define FW_ASSERT_FIRST(first, ...) (first)

// Macro name is fixed by the F Prime API.
#define FW_ASSERT(...)                           \
    do {                                         \
        assert(FW_ASSERT_FIRST(__VA_ARGS__, 0)); \
    } while (0)

#endif
