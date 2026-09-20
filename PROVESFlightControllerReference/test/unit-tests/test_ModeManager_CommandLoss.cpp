// ======================================================================
// \title  test_ModeManager_CommandLoss.cpp
// \brief  Host unit tests for ModeManager's command-loss fault report.
//
// Level: Unit. Compiles the real ModeManager.cpp against the recorder stub in
// support/PROVESFlightControllerReference/Components/ModeManager/ plus the
// in-memory Os::File; no F Prime or Zephyr code is linked
// (test/unit-tests/README.md).
//
// Requirements verified: FaultManager-6 (producer ModeManager, trigger
// command loss; Cycle F row F3). MM0013 is Board level (Integration Test) and
// is not claimed here: the command-loss entry itself is exercised only as the
// pre-existing behaviour that FaultManager-6 requires to stay unchanged.
//
// Oracle (TP-3): the window is COMM_LOSS_TIME, served by the stub; the tests
// set it to a small named constant instead of the 259200 s default
// (ModeManager.fpp:229) so the window expires in a handful of ticks. The
// expected report tuple (COMMAND_LOSS, MODE_MANAGER, CRITICAL, elapsed
// seconds) is the F3 result, and the expected action after the window
// (CommandLossDetected, safe mode with reason COMMAND_LOSS, exactly one
// stopWatchdog) is upstream's commandLossCheck() as documented in
// Components/ModeManager/docs/sdd.md "Command Loss Detection". Nothing is
// read back from the logic under test.
// ======================================================================

#include <gtest/gtest.h>

#include "Os/File.hpp"
#include "PROVESFlightControllerReference/Components/ModeManager/ModeManager.hpp"

namespace {

using Components::ModeManager;
using Components::ModeManagerComponentBase;
using Components::SafeModeReason;
using Components::SystemMode;

// A short command-loss window: the tests need only that the window expires
// after a known number of 1 Hz ticks, not the flight default.
constexpr U32 COMM_LOSS_WINDOW_S = 5;

// Ticks to run after a detection to show the debounce holds.
constexpr U32 EXTRA_TICKS = 3;

constexpr FwOpcodeType OPCODE = 0x77;

class ModeManagerCommandLossTest : public ::testing::Test {
  protected:
    ModeManagerCommandLossTest() : mm("modeManager") {}

    void SetUp() override {
        Os::Test::resetFileSystem();
        this->mm.commLossTime = Fw::TimeIntervalValue(COMM_LOSS_WINDOW_S, 0);
    }

    //! One 1 Hz scheduler tick through the component's real run handler.
    static void tick(ModeManager& component, U32 count = 1) {
        for (U32 i = 0; i < count; ++i) {
            static_cast<ModeManagerComponentBase&>(component).run_handler(0, 0);
        }
    }

    //! A routed uplink packet, as the router reports it.
    static void packetRouted(ModeManager& component) {
        static_cast<ModeManagerComponentBase&>(component).packetRouted_handler(0);
    }

    static SystemMode::T currentMode(ModeManager& component) {
        return static_cast<ModeManagerComponentBase&>(component).getMode_handler(0).value();
    }

    static SafeModeReason::T reportedReason(ModeManager& component) {
        ModeManagerComponentBase& base = static_cast<ModeManagerComponentBase&>(component);
        base.GET_SAFE_MODE_REASON_cmdHandler(OPCODE, 0);
        return base.eventsCurrentSafeModeReasonReading.back();
    }

    //! The report tuple the F3 result names: type, source, severity and the
    //! elapsed seconds as the value.
    static void expectCommandLossReport(const ModeManagerComponentBase::FaultReportRecord& record) {
        EXPECT_EQ(record.faultType, Components::FaultType::COMMAND_LOSS);
        EXPECT_EQ(record.source, Components::FaultSource::MODE_MANAGER);
        EXPECT_EQ(record.severity, Components::FaultSeverity::CRITICAL);
        EXPECT_FLOAT_EQ(record.value, static_cast<F32>(COMM_LOSS_WINDOW_S));
    }

    ModeManager mm;
};

// ----------------------------------------------------------------------
// FaultManager-6 clause under test: "with faultOut connected and OBSERVED,
// exactly one report per trigger with the matching type, source and value AND
// the pre-existing events, counters and port calls unchanged".
// ----------------------------------------------------------------------

TEST_F(ModeManagerCommandLossTest, ObservedDispositionReportsOnceAndLeavesUpstreamsActionInPlace) {
    RecordProperty("verifies", "FaultManager-6");

    mm.faultOutConnected = true;
    mm.faultOutDisposition = Components::FaultDisposition::OBSERVED;

    tick(mm, COMM_LOSS_WINDOW_S - 1);
    EXPECT_TRUE(mm.faultOutCalls.empty()) << "No report before the window expires";
    EXPECT_EQ(currentMode(mm), SystemMode::NORMAL);

    tick(mm);

    // Exactly one report, carrying the elapsed seconds.
    ASSERT_EQ(mm.faultOutCalls.size(), 1u);
    expectCommandLossReport(mm.faultOutCalls[0]);

    // Upstream's action is untouched: same tick, same event, same reason,
    // sequence run once, watchdog stopped once.
    ASSERT_EQ(mm.eventsCommandLossDetected.size(), 1u);
    EXPECT_EQ(mm.eventsCommandLossDetected[0], COMM_LOSS_WINDOW_S);
    EXPECT_EQ(currentMode(mm), SystemMode::SAFE_MODE);
    EXPECT_EQ(reportedReason(mm), SafeModeReason::COMMAND_LOSS);
    EXPECT_EQ(mm.runSequenceCalls.size(), 1u);
    EXPECT_EQ(mm.stopWatchdogCalls, 1u);

    // The debounce that guards upstream's action guards the report too.
    tick(mm, EXTRA_TICKS);
    EXPECT_EQ(mm.faultOutCalls.size(), 1u) << "One report per loss episode";
    EXPECT_EQ(mm.eventsCommandLossDetected.size(), 1u);
    EXPECT_EQ(mm.stopWatchdogCalls, 1u) << "No path may stop the watchdog twice";
}

TEST_F(ModeManagerCommandLossTest, ClaimedDispositionHandsCommandLossToTheFaultManager) {
    RecordProperty("verifies", "FaultManager-6");

    mm.faultOutConnected = true;
    mm.faultOutDisposition = Components::FaultDisposition::CLAIMED;

    tick(mm, COMM_LOSS_WINDOW_S);

    ASSERT_EQ(mm.faultOutCalls.size(), 1u);
    expectCommandLossReport(mm.faultOutCalls[0]);

    // The detection event is still ModeManager's; the action is not.
    ASSERT_EQ(mm.eventsCommandLossDetected.size(), 1u);
    EXPECT_EQ(currentMode(mm), SystemMode::NORMAL) << "A CLAIMED report must stop ModeManager entering on its own";
    EXPECT_EQ(mm.stopWatchdogCalls, 0u) << "A CLAIMED report must leave the watchdog to the FaultManager";
    EXPECT_TRUE(mm.runSequenceCalls.empty());
    EXPECT_TRUE(mm.modeChangedCalls.empty());
    EXPECT_TRUE(mm.loadSwitchTurnOffCalls.empty());

    tick(mm, EXTRA_TICKS);
    EXPECT_EQ(mm.faultOutCalls.size(), 1u) << "The debounce flag still guards the report";

    // The FaultManager's own action arrives on forceSafeMode with the reason
    // its reasonFor() maps for COMMAND_LOSS.
    static_cast<ModeManagerComponentBase&>(mm).forceSafeMode_handler(0, SafeModeReason(SafeModeReason::COMMAND_LOSS));
    EXPECT_EQ(currentMode(mm), SystemMode::SAFE_MODE);
    EXPECT_EQ(reportedReason(mm), SafeModeReason::COMMAND_LOSS);
}

// ----------------------------------------------------------------------
// FaultManager-6 clause under test: "with faultOut unconnected, zero reports
// and the same pre-existing behaviour". Completing without an assert is the
// evidence that the unconnected port is never called.
// ----------------------------------------------------------------------

TEST_F(ModeManagerCommandLossTest, UnconnectedFaultOutLeavesCommandLossBehaviourUnchanged) {
    RecordProperty("verifies", "FaultManager-6");

    ASSERT_FALSE(mm.faultOutConnected) << "faultOut must default to unconnected on the host";

    tick(mm, COMM_LOSS_WINDOW_S);

    EXPECT_TRUE(mm.faultOutCalls.empty()) << "An unconnected port must never be called";
    ASSERT_EQ(mm.eventsCommandLossDetected.size(), 1u);
    EXPECT_EQ(mm.eventsCommandLossDetected[0], COMM_LOSS_WINDOW_S);
    EXPECT_EQ(currentMode(mm), SystemMode::SAFE_MODE);
    EXPECT_EQ(reportedReason(mm), SafeModeReason::COMMAND_LOSS);
    EXPECT_EQ(mm.runSequenceCalls.size(), 1u);
    EXPECT_EQ(mm.stopWatchdogCalls, 1u);
}

// ----------------------------------------------------------------------
// Regression cover (no claim): a routed packet resets the window, so the
// report follows the detection and never precedes it.
// ----------------------------------------------------------------------

TEST_F(ModeManagerCommandLossTest, PacketRoutedResetsTheWindowBeforeAnyReport) {
    mm.faultOutConnected = true;
    mm.faultOutDisposition = Components::FaultDisposition::OBSERVED;

    tick(mm, COMM_LOSS_WINDOW_S - 1);
    packetRouted(mm);
    tick(mm, COMM_LOSS_WINDOW_S - 1);

    EXPECT_TRUE(mm.faultOutCalls.empty()) << "The reset window has not expired";
    EXPECT_TRUE(mm.eventsCommandLossDetected.empty());
    EXPECT_EQ(currentMode(mm), SystemMode::NORMAL);

    tick(mm);

    ASSERT_EQ(mm.faultOutCalls.size(), 1u);
    expectCommandLossReport(mm.faultOutCalls[0]);
    EXPECT_EQ(mm.eventsCommandLossDetected.size(), 1u);
    EXPECT_EQ(currentMode(mm), SystemMode::SAFE_MODE);
}

}  // namespace
