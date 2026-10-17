// ======================================================================
// \title  test_ModeManager_ModeNotification.cpp
// \brief  Host unit tests for ModeManager mode-change fan-out and the boot
//         safe mode sequence.
//
// Level: Unit. Compiles the real ModeManager.cpp against the recorder stub in
// support/PROVESFlightControllerReference/Components/ModeManager/ plus the
// in-memory Os::File / Os::FileSystem; no F Prime or Zephyr code is linked
// (test/unit-tests/README.md).
//
// Behaviour under test (comms software implementation, bug fixes):
//  - every mode change, into and out of safe mode, is sent on every connected
//    modeChanged port, so a listener on port 1 (CommsModeManager) hears that
//    safe mode ended as well as that it began;
//  - the three boot paths into safe mode (restored SAFE record, unintended
//    reboot, corrupt record) run the safe mode sequence on the first run tick,
//    never inside restorePersistentState(), which runs before parameters are
//    loaded (ReferenceDeploymentTopology.cpp setupTopology).
//
// Requirements verified: MM0014 (fan-out on every connected modeChanged port)
// and MM0015 (boot paths into safe mode run the sequence once, after parameters
// are loaded), against their Pass Criteria in ModeManager/docs/sdd.md.
// ======================================================================

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Os/File.hpp"
#include "PROVESFlightControllerReference/Components/ModeManager/ModeManager.hpp"

namespace {

using Components::ModeManager;
using Components::ModeManagerComponentBase;
using Components::SafeModeReason;
using Components::SystemMode;

constexpr const char* STATE_FILE = "/mode_state.bin";
constexpr const char* SEQUENCE_FILE = "/seq/enter_safe.bin";
constexpr FwOpcodeType OPCODE = 0x77;
constexpr F64 RECOVERY_VOLTAGE_V = 8.0;
constexpr U32 DEBOUNCE_TICKS = 10;

class ModeManagerNotificationTest : public ::testing::Test {
  protected:
    void SetUp() override { Os::Test::resetFileSystem(); }

    static ModeManagerComponentBase& base(ModeManager& component) {
        return static_cast<ModeManagerComponentBase&>(component);
    }

    static SystemMode::T currentMode(ModeManager& component) { return base(component).getMode_handler(0).value(); }

    static void tick(ModeManager& component, U32 count = 1) {
        for (U32 i = 0; i < count; i++) {
            base(component).run_handler(0, 0);
        }
    }

    static void boot(ModeManager& component) {
        component.init(0);
        component.restorePersistentState();
    }

    //! Leave a record from a first life that ended in SAFE_MODE with reason
    //! GROUND_COMMAND and an orderly shutdown.
    static void seedCleanSafeRecord() {
        ModeManager first("modeManager");
        boot(first);
        base(first).FORCE_SAFE_MODE_cmdHandler(OPCODE, 0);
        base(first).prepareForReboot_handler(0);
    }

    //! Leave a record from a first life in NORMAL that lost power without
    //! prepareForReboot (clean-shutdown flag clear).
    static void seedUncleanNormalRecord() {
        ModeManager first("modeManager");
        boot(first);
    }

    //! Leave a record from a first life in NORMAL with an orderly shutdown.
    static void seedCleanNormalRecord() {
        ModeManager first("modeManager");
        boot(first);
        base(first).prepareForReboot_handler(0);
    }

    static void expectBothPortsSaw(const ModeManager& component, SystemMode::T mode, const char* where) {
        ASSERT_EQ(component.modeChangedCalls.size(), 2u) << where;
        ASSERT_EQ(component.modeChangedPorts.size(), 2u) << where;
        EXPECT_EQ(component.modeChangedPorts[0], 0) << where;
        EXPECT_EQ(component.modeChangedPorts[1], 1) << where;
        EXPECT_EQ(component.modeChangedCalls[0], mode) << where;
        EXPECT_EQ(component.modeChangedCalls[1], mode) << where;
    }
};

// ----------------------------------------------------------------------
// modeChanged fan-out
// ----------------------------------------------------------------------

TEST_F(ModeManagerNotificationTest, ForcedEntryNotifiesEveryConnectedPort) {
    RecordProperty("verifies", "MM0014");

    ModeManager mm("modeManager");
    boot(mm);
    mm.modeChangedCalls.clear();
    mm.modeChangedPorts.clear();

    base(mm).FORCE_SAFE_MODE_cmdHandler(OPCODE, 0);

    expectBothPortsSaw(mm, SystemMode::SAFE_MODE, "FORCE_SAFE_MODE");
}

TEST_F(ModeManagerNotificationTest, CommandedExitNotifiesEveryConnectedPort) {
    RecordProperty("verifies", "MM0014");

    ModeManager mm("modeManager");
    boot(mm);
    base(mm).FORCE_SAFE_MODE_cmdHandler(OPCODE, 0);
    mm.modeChangedCalls.clear();
    mm.modeChangedPorts.clear();

    base(mm).EXIT_SAFE_MODE_cmdHandler(OPCODE, 0);

    ASSERT_EQ(currentMode(mm), SystemMode::NORMAL);
    expectBothPortsSaw(mm, SystemMode::NORMAL, "EXIT_SAFE_MODE");
}

TEST_F(ModeManagerNotificationTest, AutomaticExitNotifiesEveryConnectedPort) {
    RecordProperty("verifies", "MM0014");

    ModeManager mm("modeManager");
    boot(mm);
    base(mm).forceSafeMode_handler(0, SafeModeReason::LOW_BATTERY);
    ASSERT_EQ(currentMode(mm), SystemMode::SAFE_MODE);
    mm.modeChangedCalls.clear();
    mm.modeChangedPorts.clear();

    mm.injectedVoltage = RECOVERY_VOLTAGE_V + 0.1;
    tick(mm, DEBOUNCE_TICKS);

    ASSERT_EQ(currentMode(mm), SystemMode::NORMAL);
    expectBothPortsSaw(mm, SystemMode::NORMAL, "voltage recovery");
}

TEST_F(ModeManagerNotificationTest, DisconnectedModeChangedPortIsSkipped) {
    RecordProperty("verifies", "MM0014");

    ModeManager mm("modeManager");
    mm.modeChangedConnected[1] = false;
    boot(mm);
    base(mm).FORCE_SAFE_MODE_cmdHandler(OPCODE, 0);
    mm.modeChangedCalls.clear();
    mm.modeChangedPorts.clear();

    base(mm).EXIT_SAFE_MODE_cmdHandler(OPCODE, 0);

    ASSERT_EQ(mm.modeChangedPorts.size(), 1u);
    EXPECT_EQ(mm.modeChangedPorts[0], 0);
}

// ----------------------------------------------------------------------
// Boot paths into safe mode run the sequence on the first tick
// ----------------------------------------------------------------------

TEST_F(ModeManagerNotificationTest, RestoredSafeModeRunsSequenceOnFirstTickOnly) {
    RecordProperty("verifies", "MM0015");

    seedCleanSafeRecord();

    ModeManager mm("modeManager");
    boot(mm);
    ASSERT_EQ(currentMode(mm), SystemMode::SAFE_MODE);
    EXPECT_TRUE(mm.runSequenceCalls.empty()) << "No sequence before parameters are loaded";

    tick(mm);
    ASSERT_EQ(mm.runSequenceCalls.size(), 1u);
    EXPECT_EQ(mm.runSequenceCalls[0], SEQUENCE_FILE);

    tick(mm, 5);
    EXPECT_EQ(mm.runSequenceCalls.size(), 1u) << "The boot sequence runs once, not every tick";
}

TEST_F(ModeManagerNotificationTest, UnintendedRebootRunsSequenceOnFirstTick) {
    RecordProperty("verifies", "MM0015");

    seedUncleanNormalRecord();

    ModeManager mm("modeManager");
    boot(mm);
    ASSERT_EQ(currentMode(mm), SystemMode::SAFE_MODE);
    ASSERT_EQ(mm.eventsUnintendedRebootDetected, 1u);
    EXPECT_TRUE(mm.runSequenceCalls.empty());

    tick(mm);
    EXPECT_EQ(mm.runSequenceCalls.size(), 1u);
}

TEST_F(ModeManagerNotificationTest, CorruptRecordRunsSequenceOnFirstTick) {
    RecordProperty("verifies", "MM0015");

    Os::Test::fileSystem().files[STATE_FILE].assign(7, 0xA5);

    ModeManager mm("modeManager");
    boot(mm);
    ASSERT_EQ(currentMode(mm), SystemMode::SAFE_MODE);
    ASSERT_EQ(mm.eventsStatePersistenceFailure.size(), 1u);
    EXPECT_TRUE(mm.runSequenceCalls.empty());

    tick(mm);
    EXPECT_EQ(mm.runSequenceCalls.size(), 1u);
}

TEST_F(ModeManagerNotificationTest, CleanNormalBootRunsNoSequence) {
    RecordProperty("verifies", "MM0015");

    seedCleanNormalRecord();

    ModeManager mm("modeManager");
    boot(mm);
    ASSERT_EQ(currentMode(mm), SystemMode::NORMAL);

    tick(mm, 3);
    EXPECT_TRUE(mm.runSequenceCalls.empty());
}

TEST_F(ModeManagerNotificationTest, FirstBootRunsNoSequence) {
    RecordProperty("verifies", "MM0015");

    ModeManager mm("modeManager");
    boot(mm);
    ASSERT_EQ(currentMode(mm), SystemMode::NORMAL);

    tick(mm, 3);
    EXPECT_TRUE(mm.runSequenceCalls.empty());
}

TEST_F(ModeManagerNotificationTest, ExitBeforeFirstTickDropsPendingSequence) {
    RecordProperty("verifies", "MM0015");

    seedCleanSafeRecord();

    ModeManager mm("modeManager");
    boot(mm);
    base(mm).EXIT_SAFE_MODE_cmdHandler(OPCODE, 0);
    ASSERT_EQ(currentMode(mm), SystemMode::NORMAL);

    tick(mm, 3);
    EXPECT_TRUE(mm.runSequenceCalls.empty()) << "A safe mode sequence must not run after safe mode has ended";
}

}  // namespace
