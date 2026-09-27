// ======================================================================
// \title  test_Watchdog_FaultReport.cpp
// \brief  Host unit tests for the Watchdog fault-report hook.
//
// Level: Unit. Runs on the host against the recorder stub in
// support/PROVESFlightControllerReference/Components/Watchdog/; no F Prime or
// Zephyr code is linked (test/unit-tests/README.md).
//
// Requirements verified: FaultManager-6.
//   Pass criteria clause under test: "with faultOut connected and OBSERVED,
//   exactly one report per trigger with the matching type, source and value
//   AND the pre-existing events, counters and port calls unchanged; with
//   faultOut unconnected, zero reports and the same pre-existing behaviour".
//
// Oracle (TP-3): the pre-existing behaviour is read from Watchdog.fpp and the
// requirement, not from the code under test — stopping the watchdog halts the
// GPIO toggling, emits one WatchdogStop event, and (from STOP_WATCHDOG) first
// notifies ModeManager through prepareForReboot.
// ======================================================================

#include <gtest/gtest.h>

#include "PROVESFlightControllerReference/Components/Watchdog/Watchdog.hpp"

namespace {

using Components::Watchdog;
using Components::WatchdogComponentBase;
using FaultSource = Components::FaultSource;
using FaultType = Components::FaultType;

//! Fixture: a fresh Watchdog per test. faultOut starts unconnected, which is
//! the state every test written before the port existed sees.
class WatchdogFaultReportTest : public ::testing::Test {
  protected:
    WatchdogFaultReportTest() : dog("watchdog") {}

    WatchdogComponentBase& base() { return static_cast<WatchdogComponentBase&>(this->dog); }

    void tick() { this->base().run_handler(0, 0); }

    Watchdog dog;
};

// ----------------------------------------------------------------------
// Connected: one report per stop, beside the unchanged existing behaviour
// ----------------------------------------------------------------------

TEST_F(WatchdogFaultReportTest, StopReportsExactlyOnceBesideTheUnchangedStopBehaviour) {
    RecordProperty("verifies", "FaultManager-6");

    dog.faultOutConnected = true;
    dog.faultOutDisposition = Components::FaultDisposition::OBSERVED;

    // Pet a few times so the transition count carried in the report is not 0.
    constexpr int PETS = 3;
    for (int i = 0; i < PETS; i++) {
        tick();
    }
    ASSERT_EQ(dog.gpioSetCalls.size(), static_cast<size_t>(PETS));
    EXPECT_TRUE(dog.faultOutCalls.empty()) << "Petting must not report a fault";

    base().STOP_WATCHDOG_cmdHandler(0x200, 1);

    // Existing behaviour, unchanged.
    EXPECT_EQ(dog.prepareForRebootCalls, 1u) << "STOP_WATCHDOG must still notify ModeManager first";
    EXPECT_EQ(dog.eventsWatchdogStop, 1u);
    EXPECT_EQ(dog.eventsWatchdogStart, 0u);
    ASSERT_EQ(dog.cmdResponses.size(), 1u);
    EXPECT_EQ(dog.cmdResponses[0].response, Fw::CmdResponse::OK);

    // Exactly one report, carrying the transition count as its value.
    ASSERT_EQ(dog.faultOutCalls.size(), 1u);
    EXPECT_EQ(dog.faultOutCalls[0].faultType, FaultType::WATCHDOG_STOPPED);
    EXPECT_EQ(dog.faultOutCalls[0].source, FaultSource::WATCHDOG);
    EXPECT_EQ(dog.faultOutCalls[0].severity, Components::FaultSeverity::CRITICAL);
    EXPECT_FLOAT_EQ(dog.faultOutCalls[0].value, static_cast<F32>(PETS));

    // Petting really has stopped, and no further report is produced.
    const size_t gpioCallsAtStop = dog.gpioSetCalls.size();
    for (int i = 0; i < 5; i++) {
        tick();
    }
    EXPECT_EQ(dog.gpioSetCalls.size(), gpioCallsAtStop) << "A stopped watchdog must not toggle the GPIO";
    EXPECT_EQ(dog.faultOutCalls.size(), 1u) << "The stop must report once, not once per tick";
}

TEST_F(WatchdogFaultReportTest, RestartAndStopReportOncePerStop) {
    RecordProperty("verifies", "FaultManager-6");

    dog.faultOutConnected = true;

    base().stop_handler(0);
    base().start_handler(0);
    base().stop_handler(0);

    EXPECT_EQ(dog.faultOutCalls.size(), 2u) << "Each stop is one report";
    EXPECT_EQ(dog.eventsWatchdogStop, 2u) << "The existing event count must match";
    EXPECT_EQ(dog.eventsWatchdogStart, 1u);
}

// ----------------------------------------------------------------------
// Unconnected: byte-identical to the behaviour before the port existed
// ----------------------------------------------------------------------

TEST_F(WatchdogFaultReportTest, UnconnectedFaultOutLeavesEveryExistingEffectUnchanged) {
    RecordProperty("verifies", "FaultManager-6");

    ASSERT_FALSE(dog.faultOutConnected) << "faultOut must default to unconnected on the host";

    tick();
    tick();
    base().STOP_WATCHDOG_cmdHandler(0x200, 1);
    tick();

    EXPECT_TRUE(dog.faultOutCalls.empty()) << "An unconnected port must never be called";
    EXPECT_EQ(dog.gpioSetCalls.size(), 2u);
    EXPECT_EQ(dog.prepareForRebootCalls, 1u);
    EXPECT_EQ(dog.eventsWatchdogStop, 1u);
    EXPECT_EQ(dog.tlmWatchdogTransitions.size(), 2u);
    EXPECT_EQ(dog.tlmWatchdogTransitions.back(), 2u);
    ASSERT_EQ(dog.cmdResponses.size(), 1u);
    EXPECT_EQ(dog.cmdResponses[0].response, Fw::CmdResponse::OK);
}

}  // namespace
