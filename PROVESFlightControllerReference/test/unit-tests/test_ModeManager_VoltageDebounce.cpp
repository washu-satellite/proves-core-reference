// ======================================================================
// \title  test_ModeManager_VoltageDebounce.cpp
// \brief  Host unit tests for ModeManager voltage debounce and safe-mode entry.
//
// Level: Unit. Compiles the real ModeManager.cpp against the recorder stub in
// support/PROVESFlightControllerReference/Components/ModeManager/ plus the
// in-memory Os::File; no F Prime or Zephyr code is linked
// (test/unit-tests/README.md).
//
// Requirements verified: MM0009, MM0010, MS-L2-08, MM0005.
// MM0004 (Board level, Integration Test only) is claimed by safe_mode_test.py::test_safe_04;
// NoAutoRecoveryForGroundCommand below still exercises its logic as regression cover (TP-2).
//
// Oracle (TP-3): every expected value comes from the requirement pass criteria
// and the design sources those criteria quote — ModeManager.fpp:194-200
// (SafeModeEntryVoltage 6.7 V, SafeModeRecoveryVoltage 8.0 V,
// SafeModeDebounceSeconds 10) — restated below as named constants. The
// component reads the same numbers through its parameter ports, which the stub
// serves from those defaults; no expected value is read back out of the logic
// under test.
//
// Setup note: tests 1-7 construct the component without calling init(). The
// constructor establishes NORMAL / reason NONE / zeroed counters, which is the
// precondition every voltage-debounce criterion names, and it keeps the
// persistent-state file out of the picture. StateFileRoundTripsAcrossRestart
// is the one test that exercises init()/loadState() deliberately.
//
// MM0007 is deliberately NOT claimed here: D1 set its Level to Board and its
// Method to Integration Test, and its criterion is written entirely around a
// commanded WARM_RESET on hardware. Per TP-2 a host test may not claim a
// Board-level ID, so MM0007 is claimed by the board test
// safe_mode_test.py::test_safe_09. StateFileRoundTripsAcrossRestart below is
// kept as unclaimed regression cover for the load/save decision logic.
// ======================================================================

#include <gtest/gtest.h>

#include <algorithm>

#include "Os/File.hpp"
#include "PROVESFlightControllerReference/Components/ModeManager/ModeManager.hpp"

namespace {

using Components::ModeManager;
using Components::ModeManagerComponentBase;
using Components::SafeModeReason;
using Components::SystemMode;

// Parameter defaults, ModeManager.fpp:194-200 — the same numbers the MM0009 /
// MM0010 / MS-L2-08 pass criteria quote.
constexpr F64 ENTRY_VOLTAGE_V = 6.7;
constexpr F64 RECOVERY_VOLTAGE_V = 8.0;
constexpr U32 DEBOUNCE_TICKS = 10;

// A bus voltage comfortably inside the healthy band: above the entry threshold
// so it is not a fault, below the recovery threshold so it cannot trigger an
// automatic exit either.
constexpr F64 HEALTHY_VOLTAGE_V = 7.0;

constexpr FwOpcodeType OPCODE = 0x77;

class ModeManagerVoltageTest : public ::testing::Test {
  protected:
    ModeManagerVoltageTest() : mm("modeManager") {}

    void SetUp() override { Os::Test::resetFileSystem(); }

    //! One 1 Hz scheduler tick through the component's real run handler.
    //! run_handler is a private override, so it is driven through the base
    //! reference exactly as real autocode does.
    static void tick(ModeManager& component, U32 count = 1) {
        for (U32 i = 0; i < count; ++i) {
            static_cast<ModeManagerComponentBase&>(component).run_handler(0, 0);
        }
    }

    static void forceSafeModeCmd(ModeManager& component) {
        static_cast<ModeManagerComponentBase&>(component).FORCE_SAFE_MODE_cmdHandler(OPCODE, 0);
    }

    static void exitSafeModeCmd(ModeManager& component) {
        static_cast<ModeManagerComponentBase&>(component).EXIT_SAFE_MODE_cmdHandler(OPCODE, 0);
    }

    static SystemMode::T currentMode(ModeManager& component) {
        return static_cast<ModeManagerComponentBase&>(component).getMode_handler(0).value();
    }

    static SafeModeReason::T reportedReason(ModeManager& component) {
        ModeManagerComponentBase& base = static_cast<ModeManagerComponentBase&>(component);
        base.GET_SAFE_MODE_REASON_cmdHandler(OPCODE, 0);
        return base.eventsCurrentSafeModeReasonReading.back();
    }

    //! Drive the component into SAFE_MODE(LOW_BATTERY) the way the flight
    //! software does: a full debounce window of low readings.
    void enterViaLowBattery() {
        this->mm.injectedVoltage = ENTRY_VOLTAGE_V - 0.1;
        tick(this->mm, DEBOUNCE_TICKS);
        ASSERT_EQ(currentMode(this->mm), SystemMode::SAFE_MODE);
        ASSERT_EQ(reportedReason(this->mm), SafeModeReason::LOW_BATTERY);
    }

    ModeManager mm;
};

// ----------------------------------------------------------------------
// Entry debounce
// ----------------------------------------------------------------------

TEST_F(ModeManagerVoltageTest, TenLowSamplesEnterSafeMode) {
    RecordProperty("verifies", "MM0009,MS-L2-08");

    mm.injectedVoltage = ENTRY_VOLTAGE_V - 0.1;  // 6.6 V

    // Configure a distinctive sequence file. Asserting the component forwards
    // *this* value tests the behaviour ("entry starts the sequence named by the
    // SAFEMODE_SEQUENCE_FILE parameter") rather than pinning the shipped
    // default, which a pass-through assertion against the default could not
    // distinguish from a hardcoded path.
    mm.safeModeSequenceFile = "/seq/unit_test_safe.bin";

    // Nine consecutive low samples must NOT trip the protection.
    tick(mm, DEBOUNCE_TICKS - 1);
    EXPECT_EQ(currentMode(mm), SystemMode::NORMAL)
        << "Nine consecutive low-voltage samples are one short of the debounce "
           "window and must not enter safe mode";
    EXPECT_TRUE(mm.eventsAutoSafeModeEntry.empty());
    EXPECT_TRUE(mm.modeChangedCalls.empty());

    // The tenth trips it.
    tick(mm);
    EXPECT_EQ(currentMode(mm), SystemMode::SAFE_MODE);
    ASSERT_EQ(mm.eventsAutoSafeModeEntry.size(), 1u) << "Exactly one AutoSafeModeEntry event must be emitted";
    EXPECT_EQ(mm.eventsAutoSafeModeEntry[0].reason, SafeModeReason::LOW_BATTERY);
    EXPECT_FLOAT_EQ(mm.eventsAutoSafeModeEntry[0].voltage, static_cast<F32>(ENTRY_VOLTAGE_V - 0.1));
    EXPECT_EQ(reportedReason(mm), SafeModeReason::LOW_BATTERY);

    // The transition is announced and the safe-mode sequence is started.
    ASSERT_EQ(mm.eventsEnteringSafeMode.size(), 1u);
    ASSERT_EQ(mm.modeChangedCalls.size(), 1u);
    EXPECT_EQ(mm.modeChangedCalls[0], SystemMode::SAFE_MODE);
    ASSERT_EQ(mm.runSequenceCalls.size(), 1u);
    EXPECT_EQ(mm.runSequenceCalls[0], "/seq/unit_test_safe.bin")
        << "Safe-mode entry must start the sequence named by the "
           "SAFEMODE_SEQUENCE_FILE parameter, not a hardcoded path";
}

TEST_F(ModeManagerVoltageTest, GoodSampleResetsCounter) {
    RecordProperty("verifies", "MM0009");

    mm.injectedVoltage = ENTRY_VOLTAGE_V - 0.1;
    tick(mm, DEBOUNCE_TICKS - 1);  // 9 low

    // One healthy sample in the middle of the window resets the count.
    mm.injectedVoltage = HEALTHY_VOLTAGE_V;
    tick(mm);

    mm.injectedVoltage = ENTRY_VOLTAGE_V - 0.1;
    tick(mm, DEBOUNCE_TICKS - 1);  // 9 more low
    EXPECT_EQ(currentMode(mm), SystemMode::NORMAL) << "A good sample must reset the debounce counter, so 9+1+9 samples "
                                                      "never reach 10 consecutive low readings";
    EXPECT_TRUE(mm.eventsAutoSafeModeEntry.empty());

    // Completing a full run of ten consecutive low readings does trip it,
    // proving the assertion above is not passing vacuously.
    tick(mm);
    EXPECT_EQ(currentMode(mm), SystemMode::SAFE_MODE);
    EXPECT_EQ(mm.eventsAutoSafeModeEntry.size(), 1u);
}

TEST_F(ModeManagerVoltageTest, InvalidReadingCountsAsFault) {
    RecordProperty("verifies", "MM0009");

    // An unreadable bus voltage is a fault, not a "no news is good news"
    // condition: the criterion says "voltage < SafeModeEntryVoltage (6.7 V)
    // *or invalid* on 10 consecutive run ticks".
    mm.voltageGetConnected = false;
    mm.injectedVoltage = HEALTHY_VOLTAGE_V;  // would be healthy if it were readable

    tick(mm, DEBOUNCE_TICKS - 1);
    EXPECT_EQ(currentMode(mm), SystemMode::NORMAL);

    tick(mm);
    EXPECT_EQ(currentMode(mm), SystemMode::SAFE_MODE);
    ASSERT_EQ(mm.eventsAutoSafeModeEntry.size(), 1u);
    EXPECT_EQ(mm.eventsAutoSafeModeEntry[0].reason, SafeModeReason::LOW_BATTERY);
    // No measurement exists, so no measurement is reported.
    EXPECT_FLOAT_EQ(mm.eventsAutoSafeModeEntry[0].voltage, 0.0f);
}

// ----------------------------------------------------------------------
// Recovery debounce
// ----------------------------------------------------------------------

TEST_F(ModeManagerVoltageTest, RecoveryAboveEightVoltsTenTicksExits) {
    RecordProperty("verifies", "MM0010,MS-L2-08");

    ASSERT_NO_FATAL_FAILURE(enterViaLowBattery());
    mm.eventsAutoSafeModeExit.clear();

    mm.injectedVoltage = RECOVERY_VOLTAGE_V + 0.1;  // 8.1 V
    tick(mm, DEBOUNCE_TICKS - 1);
    EXPECT_EQ(currentMode(mm), SystemMode::SAFE_MODE)
        << "Nine consecutive recovered samples are one short of the debounce "
           "window and must not exit safe mode";
    EXPECT_TRUE(mm.eventsAutoSafeModeExit.empty());

    tick(mm);
    EXPECT_EQ(currentMode(mm), SystemMode::NORMAL);
    ASSERT_EQ(mm.eventsAutoSafeModeExit.size(), 1u);
    EXPECT_FLOAT_EQ(mm.eventsAutoSafeModeExit[0], static_cast<F32>(RECOVERY_VOLTAGE_V + 0.1));
    EXPECT_EQ(reportedReason(mm), SafeModeReason::NONE) << "The entry reason is cleared on exit";
}

TEST_F(ModeManagerVoltageTest, ExactlyEightVoltsDoesNotRecover) {
    RecordProperty("verifies", "MM0010");

    ASSERT_NO_FATAL_FAILURE(enterViaLowBattery());
    mm.eventsAutoSafeModeExit.clear();

    // The criterion says recovery needs voltage *above* 8.0 V; the threshold
    // value itself is not a recovery.
    mm.injectedVoltage = RECOVERY_VOLTAGE_V;
    tick(mm, 2 * DEBOUNCE_TICKS);

    EXPECT_EQ(currentMode(mm), SystemMode::SAFE_MODE)
        << "A reading exactly at the recovery threshold must not exit safe "
           "mode, however long it persists";
    EXPECT_TRUE(mm.eventsAutoSafeModeExit.empty());
    EXPECT_EQ(reportedReason(mm), SafeModeReason::LOW_BATTERY);
}

TEST_F(ModeManagerVoltageTest, NoAutoRecoveryForGroundCommand) {
    RecordProperty("verifies", "MM0010");

    forceSafeModeCmd(mm);
    ASSERT_EQ(currentMode(mm), SystemMode::SAFE_MODE);
    ASSERT_EQ(reportedReason(mm), SafeModeReason::GROUND_COMMAND);

    // Healthy, fully recovered voltage for twice the debounce window.
    mm.injectedVoltage = RECOVERY_VOLTAGE_V + 1.0;
    tick(mm, 2 * DEBOUNCE_TICKS);

    EXPECT_EQ(currentMode(mm), SystemMode::SAFE_MODE) << "Safe mode entered by ground command must not auto-recover on "
                                                         "voltage, no matter how healthy the bus becomes";
    EXPECT_TRUE(mm.eventsAutoSafeModeExit.empty());

    // MM0004 second clause: the explicit command does return to NORMAL.
    exitSafeModeCmd(mm);
    EXPECT_EQ(currentMode(mm), SystemMode::NORMAL);
    EXPECT_EQ(mm.eventsExitingSafeMode, 1u);
    EXPECT_EQ(reportedReason(mm), SafeModeReason::NONE);
}

// ----------------------------------------------------------------------
// Load switches on safe-mode entry (MM0005 Unit clause)
// ----------------------------------------------------------------------

TEST_F(ModeManagerVoltageTest, EnterSafeModeTurnsOffAllEightSwitches) {
    RecordProperty("verifies", "MM0005");

    // MM0005 clause claimed here: the *Unit* clause — "enterSafeMode calls
    // loadSwitchTurnOff on all 8 connected ports exactly once". The Board
    // clause (every switch reads OFF via GET_IS_ON within 5 s) is covered by
    // safe_mode_test.py::test_safe_10.
    ASSERT_EQ(ModeManagerComponentBase::getNum_loadSwitchTurnOff_OutputPorts(), 8);

    forceSafeModeCmd(mm);

    ASSERT_EQ(mm.loadSwitchTurnOffCalls.size(), 8u)
        << "Entering safe mode must signal every connected load-switch-off port";
    // The requirement is "all 8 connected ports exactly once". The *order* the
    // ports are visited in is not part of the requirement and is not asserted:
    // each port must appear exactly once, in any order.
    for (FwIndexType port = 0; port < 8; ++port) {
        EXPECT_EQ(std::count(mm.loadSwitchTurnOffCalls.begin(), mm.loadSwitchTurnOffCalls.end(), port), 1)
            << "Load switch port " << port << " must be turned off exactly once";
    }
    EXPECT_TRUE(mm.loadSwitchTurnOnCalls.empty()) << "Entering safe mode must not turn any load switch on";
}

TEST_F(ModeManagerVoltageTest, DisconnectedSwitchPortsAreSkipped) {
    RecordProperty("verifies", "MM0005");

    // The criterion says "all 8 *connected* ports": an unwired port is skipped,
    // not signalled into the void.
    mm.loadSwitchTurnOffConnected[2] = false;
    mm.loadSwitchTurnOffConnected[5] = false;

    forceSafeModeCmd(mm);

    ASSERT_EQ(mm.loadSwitchTurnOffCalls.size(), 6u);
    for (FwIndexType port = 0; port < 8; ++port) {
        const long expected = (port == 2 || port == 5) ? 0 : 1;
        EXPECT_EQ(std::count(mm.loadSwitchTurnOffCalls.begin(), mm.loadSwitchTurnOffCalls.end(), port), expected)
            << "Unconnected port " << port << " must be skipped, connected ports signalled once";
    }
}

// ----------------------------------------------------------------------
// Persistent state across a restart
//
// Unclaimed by design: MM0007's Level is Board (see the file header), so this
// test is regression cover for the loadState()/saveState() decision logic
// rather than requirement evidence.
// ----------------------------------------------------------------------

TEST_F(ModeManagerVoltageTest, StateFileRoundTripsAcrossCleanRestart) {
    // First life: ground-commanded safe mode, then an orderly shutdown.
    {
        ModeManager first("modeManager");
        forceSafeModeCmd(first);
        ASSERT_EQ(currentMode(first), SystemMode::SAFE_MODE);
        static_cast<ModeManagerComponentBase&>(first).prepareForReboot_handler(0);
        EXPECT_EQ(first.eventsPreparingForReboot, 1u);
    }

    // Second life on the same filesystem.
    ModeManager second("modeManager");
    second.init(0);

    EXPECT_EQ(currentMode(second), SystemMode::SAFE_MODE) << "Safe mode must survive a clean restart";
    EXPECT_EQ(reportedReason(second), SafeModeReason::GROUND_COMMAND) << "The entry reason must survive with it";
    EXPECT_EQ(second.eventsUnintendedRebootDetected, 0u)
        << "A shutdown that ran prepareForReboot is clean and must not be "
           "reported as an unintended reboot";
    // Restoring safe mode re-asserts the hardware state rather than trusting
    // the switches to still be off across the power cycle.
    EXPECT_EQ(second.loadSwitchTurnOffCalls.size(), 8u);
    EXPECT_TRUE(second.loadSwitchTurnOnCalls.empty());
}

TEST_F(ModeManagerVoltageTest, UncleanRestartFromNormalEntersSafeModeWithSystemFault) {
    // First life: NORMAL throughout, then power is lost with no
    // prepareForReboot — the clean-shutdown flag stays clear.
    {
        ModeManager first("modeManager");
        first.init(0);
        ASSERT_EQ(currentMode(first), SystemMode::NORMAL);
    }

    ModeManager second("modeManager");
    second.init(0);

    EXPECT_EQ(currentMode(second), SystemMode::SAFE_MODE)
        << "An unintended reboot out of NORMAL must land in safe mode";
    EXPECT_EQ(reportedReason(second), SafeModeReason::SYSTEM_FAULT);
    EXPECT_EQ(second.eventsUnintendedRebootDetected, 1u);
}

}  // namespace
