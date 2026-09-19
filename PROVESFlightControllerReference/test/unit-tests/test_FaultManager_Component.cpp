// ======================================================================
// \title  test_FaultManager_Component.cpp
// \brief  Host unit tests for the FaultManager component.
//
// Level: Unit. Runs on the host against the recorder stub in
// support/PROVESFlightControllerReference/Components/FaultManager/; no F Prime
// or Zephyr code is linked (test/unit-tests/README.md).
//
// Requirements verified: FaultManager-1, -2, -4, -5, -7, -9.
//
// Oracle (TP-3): expected values are the parameter defaults declared in
// FaultManager.fpp (AUTHORITY_ENABLED false, AUTHORITY_MASK 0,
// DEBOUNCE_LOW_BATTERY 10, DEBOUNCE_THERMAL 1) and the producer behaviour the
// action map reproduces (ModeManager::commandLossCheck enters safe mode with
// reason COMMAND_LOSS and stops the watchdog, since the 2026-09 upstream sync
// retired the AuthenticationRouter; ModeManager enters safe mode with reason
// LOW_BATTERY). None is read back from the code under test.
// ======================================================================

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "PROVESFlightControllerReference/Components/FaultManager/FaultManager.hpp"

namespace {

using Components::FaultManager;
using Components::FaultManagerComponentBase;
using Disposition = Components::FaultDisposition;
using FaultAction = Components::FaultAction;
using FaultSource = Components::FaultSource;
using FaultType = Components::FaultType;

// Parameter defaults, FaultManager.fpp.
constexpr U8 DEBOUNCE_LOW_BATTERY = 10;
constexpr U8 DEBOUNCE_THERMAL = 1;

// Mask bits, FaultTypes.fpp: bit = 1 << (FaultType - 1).
constexpr U8 BIT_LOW_BATTERY = 0x10;
constexpr U8 BIT_COMMAND_LOSS = 0x20;
constexpr U8 BIT_WATCHDOG_STOPPED = 0x40;

// Every tracked fault type, and the sources and severities a producer may use.
constexpr FaultType::T ALL_TYPES[8] = {FaultType::FACE_TEMP_HIGH,   FaultType::FACE_TEMP_LOW, FaultType::BATT_TEMP_HIGH,
                                       FaultType::BATT_TEMP_LOW,    FaultType::LOW_BATTERY,   FaultType::COMMAND_LOSS,
                                       FaultType::WATCHDOG_STOPPED, FaultType::ADCS_UNSTABLE};
constexpr FaultSource::T ALL_SOURCES[5] = {FaultSource::THERMAL_MANAGER, FaultSource::MODE_MANAGER,
                                           FaultSource::AUTH_ROUTER, FaultSource::WATCHDOG,
                                           FaultSource::DETUMBLE_MANAGER};
constexpr Components::FaultSeverity::T ALL_SEVERITIES[2] = {Components::FaultSeverity::WARNING,
                                                            Components::FaultSeverity::CRITICAL};

// More reports than the largest debounce, so every type is driven past it.
constexpr int REPORTS_PAST_DEBOUNCE = DEBOUNCE_LOW_BATTERY + 2;

//! Fixture: a fresh component per test, with the FPP parameter defaults
//! already in the recorder, so "do nothing" is the shipped configuration.
class FaultManagerComponentTest : public ::testing::Test {
  protected:
    FaultManagerComponentTest() : fm("faultManager") {}

    FaultManagerComponentBase& base() { return static_cast<FaultManagerComponentBase&>(this->fm); }

    //! One 1 Hz tick, driven through the base reference as autocode does.
    void tick() { this->base().run_handler(0, 0); }

    //! Push the cached parameters into the component, as PrmDb does at boot
    //! and after every PRM_SET.
    void applyParameters() { this->base().parameterUpdated(FaultManagerComponentBase::PARAMID_AUTHORITY_ENABLED); }

    //! One report through the guarded port, on the slot the topology assigns.
    Disposition report(FaultType::T type,
                       FaultSource::T source = FaultSource::MODE_MANAGER,
                       Components::FaultSeverity::T severity = Components::FaultSeverity::CRITICAL,
                       F32 value = 0.0f) {
        return this->base().faultIn_guarded(0, FaultType(type), FaultSource(source),
                                            Components::FaultSeverity(severity), value);
    }

    //! Report a type past its debounce, then tick so any confirmation drains.
    void driveToConfirmation(FaultType::T type, FaultSource::T source = FaultSource::MODE_MANAGER, F32 value = 0.0f) {
        for (int i = 0; i < REPORTS_PAST_DEBOUNCE; i++) {
            this->report(type, source, Components::FaultSeverity::CRITICAL, value);
        }
        this->tick();
    }

    FaultManager fm;
};

// ----------------------------------------------------------------------
// FaultManager-1: the shadow gate
// ----------------------------------------------------------------------

TEST_F(FaultManagerComponentTest, ShadowDefaultsLeaveEveryActionPortUnreachable) {
    RecordProperty("verifies", "FaultManager-1");

    // The recorder starts at the FPP defaults; confirm that is what shadow
    // mode means before relying on it.
    ASSERT_FALSE(fm.authorityEnabled);
    ASSERT_EQ(fm.authorityMask, 0u);
    applyParameters();

    // Every fault type, from every source, at both severities, driven past its
    // debounce, with a tick after each pattern.
    for (const FaultType::T type : ALL_TYPES) {
        for (const FaultSource::T source : ALL_SOURCES) {
            for (const Components::FaultSeverity::T severity : ALL_SEVERITIES) {
                for (int i = 0; i < REPORTS_PAST_DEBOUNCE; i++) {
                    const Disposition disposition = report(type, source, severity, 1.0f);
                    EXPECT_EQ(disposition, Disposition::OBSERVED)
                        << "Shadow mode must observe every report (type " << static_cast<int>(type) << ", source "
                        << static_cast<int>(source) << ", severity " << static_cast<int>(severity) << ")";
                }
                tick();
            }
        }
    }

    EXPECT_TRUE(fm.forceSafeModeCalls.empty()) << "Shadow mode must never force safe mode";
    EXPECT_EQ(fm.stopWatchdogCalls, 0u) << "Shadow mode must never stop the watchdog";
    EXPECT_EQ(fm.actionOrder.size(), 0u);
    EXPECT_TRUE(fm.eventsFaultActionTaken.empty());

    // The decisions were still made and reported, which is the point of shadow
    // mode: LOW_BATTERY and COMMAND_LOSS are the only types with an action.
    EXPECT_FALSE(fm.eventsFaultActionSuppressed.empty());
    EXPECT_GT(fm.tlmShadowActionsSuppressed.back(), 0u);
}

TEST_F(FaultManagerComponentTest, InvalidOrUninitParametersFallBackToShadow) {
    RecordProperty("verifies", "FaultManager-1");

    const Fw::ParamValid::T unusable[2] = {Fw::ParamValid::INVALID, Fw::ParamValid::UNINIT};

    for (const Fw::ParamValid::T validity : unusable) {
        FaultManager local("faultManager");
        FaultManagerComponentBase& localBase = static_cast<FaultManagerComponentBase&>(local);

        // A parameter database that would grant full authority, but cannot be
        // trusted, must not grant anything.
        local.authorityEnabled = true;
        local.authorityMask = 0xFF;
        local.paramValidity = validity;
        localBase.parameterUpdated(FaultManagerComponentBase::PARAMID_AUTHORITY_ENABLED);

        for (const FaultType::T type : ALL_TYPES) {
            for (const FaultSource::T source : ALL_SOURCES) {
                for (int i = 0; i < REPORTS_PAST_DEBOUNCE; i++) {
                    EXPECT_EQ(
                        localBase.faultIn_guarded(0, FaultType(type), FaultSource(source),
                                                  Components::FaultSeverity(Components::FaultSeverity::CRITICAL), 1.0f),
                        Disposition::OBSERVED)
                        << "An unusable parameter read must fall back to shadow mode (validity "
                        << static_cast<int>(validity) << ", type " << static_cast<int>(type) << ")";
                }
                localBase.run_handler(0, 0);
            }
        }

        EXPECT_TRUE(local.forceSafeModeCalls.empty()) << "validity " << static_cast<int>(validity);
        EXPECT_EQ(local.stopWatchdogCalls, 0u) << "validity " << static_cast<int>(validity);
        EXPECT_EQ(local.tlmAuthorityState.back(), 0u) << "AuthorityState must read 0 (shadow) for an unusable read";
    }
}

// ----------------------------------------------------------------------
// FaultManager-2: counting and telemetry
// ----------------------------------------------------------------------

TEST_F(FaultManagerComponentTest, CountersAndChannelsTrackReportsAndChangeOnly) {
    RecordProperty("verifies", "FaultManager-2");

    applyParameters();

    constexpr int THERMAL_REPORTS = 3;
    for (int i = 0; i < THERMAL_REPORTS; i++) {
        report(FaultType::FACE_TEMP_HIGH, FaultSource::THERMAL_MANAGER, Components::FaultSeverity::WARNING, 61.0f);
    }
    report(FaultType::COMMAND_LOSS, FaultSource::AUTH_ROUTER, Components::FaultSeverity::CRITICAL, 0.0f);
    report(FaultType::WATCHDOG_STOPPED, FaultSource::WATCHDOG, Components::FaultSeverity::CRITICAL, 7.0f);
    tick();

    ASSERT_FALSE(fm.tlmFaultsDetected.empty());
    EXPECT_EQ(fm.tlmFaultsDetected.back(), static_cast<U32>(THERMAL_REPORTS + 2));
    EXPECT_EQ(fm.tlmFaultCountThermal.back(), static_cast<U32>(THERMAL_REPORTS));
    EXPECT_EQ(fm.tlmFaultCountCommandLoss.back(), 1u);
    EXPECT_EQ(fm.tlmFaultCountWatchdogStop.back(), 1u);
    EXPECT_EQ(fm.tlmFaultCountLowBattery.back(), 0u);

    // FACE_TEMP_HIGH (0x01) confirms at DEBOUNCE_THERMAL, COMMAND_LOSS (0x20)
    // and WATCHDOG_STOPPED (0x40) on their single report.
    ASSERT_EQ(DEBOUNCE_THERMAL, 1);
    EXPECT_EQ(fm.tlmActiveFaults.back(), 0x01 | BIT_COMMAND_LOSS | BIT_WATCHDOG_STOPPED);
    EXPECT_EQ(fm.tlmFaultsConfirmed.back(), 3u);
    EXPECT_EQ(fm.eventsFaultConfirmed.size(), 3u);

    // A tick that changes nothing must write nothing.
    const size_t detectedWrites = fm.tlmFaultsDetected.size();
    const size_t activeWrites = fm.tlmActiveFaults.size();
    tick();
    EXPECT_EQ(fm.tlmFaultsDetected.size(), detectedWrites)
        << "Channels are update-on-change; an idle tick must be silent";
    EXPECT_EQ(fm.tlmActiveFaults.size(), activeWrites);

    // A new report writes the changed channels again.
    report(FaultType::FACE_TEMP_HIGH, FaultSource::THERMAL_MANAGER, Components::FaultSeverity::WARNING, 62.0f);
    tick();
    EXPECT_EQ(fm.tlmFaultsDetected.size(), detectedWrites + 1);
    EXPECT_EQ(fm.tlmFaultsDetected.back(), static_cast<U32>(THERMAL_REPORTS + 3));
}

// ----------------------------------------------------------------------
// FaultManager-4: the action map, once authority is granted
// ----------------------------------------------------------------------

TEST_F(FaultManagerComponentTest, LowBatteryAuthorityForcesSafeModeWithTodaysReason) {
    RecordProperty("verifies", "FaultManager-4");

    fm.authorityEnabled = true;
    fm.authorityMask = BIT_LOW_BATTERY;
    applyParameters();

    // Nine consecutive samples must not act; the tenth must.
    for (int i = 1; i < DEBOUNCE_LOW_BATTERY; i++) {
        report(FaultType::LOW_BATTERY, FaultSource::MODE_MANAGER, Components::FaultSeverity::CRITICAL, 6.5f);
        tick();
    }
    EXPECT_TRUE(fm.forceSafeModeCalls.empty()) << "Entry must wait for the full debounce";

    report(FaultType::LOW_BATTERY, FaultSource::MODE_MANAGER, Components::FaultSeverity::CRITICAL, 6.5f);
    tick();

    ASSERT_EQ(fm.forceSafeModeCalls.size(), 1u);
    EXPECT_EQ(fm.forceSafeModeCalls[0], Components::SafeModeReason::LOW_BATTERY)
        << "The manager must use the same reason ModeManager uses today";
    EXPECT_EQ(fm.stopWatchdogCalls, 0u) << "A low battery does not stop the watchdog";
    ASSERT_EQ(fm.eventsFaultActionTaken.size(), 1u);
    EXPECT_EQ(fm.eventsFaultActionTaken[0].action, FaultAction::SAFE_MODE);
    EXPECT_TRUE(fm.eventsFaultActionSuppressed.empty());
    EXPECT_EQ(fm.tlmActionsTaken.back(), 1u);

    // Still low: the action fires once, not once per tick.
    for (int i = 0; i < 5; i++) {
        report(FaultType::LOW_BATTERY, FaultSource::MODE_MANAGER, Components::FaultSeverity::CRITICAL, 6.5f);
        tick();
    }
    EXPECT_EQ(fm.forceSafeModeCalls.size(), 1u);
}

TEST_F(FaultManagerComponentTest, CommandLossAuthorityStopsWatchdogThenForcesSafeMode) {
    RecordProperty("verifies", "FaultManager-4");

    fm.authorityEnabled = true;
    fm.authorityMask = BIT_COMMAND_LOSS;
    applyParameters();

    EXPECT_EQ(report(FaultType::COMMAND_LOSS, FaultSource::MODE_MANAGER), Disposition::CLAIMED);
    tick();

    // Same two ports as ModeManager::commandLossCheck, in the order the
    // FaultManager-4 criterion pins (stopWatchdog first), and the reason
    // upstream's own path persists: COMMAND_LOSS (Cycle F row F3), so a CLAIMED
    // command loss leaves the same CurrentSafeModeReason behind as an OBSERVED one.
    ASSERT_EQ(fm.actionOrder.size(), 2u);
    EXPECT_EQ(fm.actionOrder[0], "stopWatchdog");
    EXPECT_EQ(fm.actionOrder[1], "forceSafeMode");
    ASSERT_EQ(fm.forceSafeModeCalls.size(), 1u);
    EXPECT_EQ(fm.forceSafeModeCalls[0], Components::SafeModeReason::COMMAND_LOSS)
        << "reasonFor(COMMAND_LOSS) must map to SafeModeReason::COMMAND_LOSS";
    EXPECT_EQ(fm.stopWatchdogCalls, 1u);
    ASSERT_EQ(fm.eventsFaultActionTaken.size(), 1u);
    EXPECT_EQ(fm.eventsFaultActionTaken[0].action, FaultAction::SAFE_MODE_AND_REBOOT);
}

TEST_F(FaultManagerComponentTest, TypesWithoutAnActionNeverActEvenAtFullAuthority) {
    RecordProperty("verifies", "FaultManager-4");

    fm.authorityEnabled = true;
    fm.authorityMask = 0xFF;
    applyParameters();

    // WATCHDOG_STOPPED and the four thermal types have no action by design.
    const FaultType::T noActionTypes[5] = {FaultType::WATCHDOG_STOPPED, FaultType::FACE_TEMP_HIGH,
                                           FaultType::FACE_TEMP_LOW, FaultType::BATT_TEMP_HIGH,
                                           FaultType::BATT_TEMP_LOW};
    for (const FaultType::T type : noActionTypes) {
        EXPECT_EQ(report(type, FaultSource::WATCHDOG, Components::FaultSeverity::CRITICAL, 1.0f), Disposition::OBSERVED)
            << "A type with no action must never be claimed (type " << static_cast<int>(type) << ")";
    }
    tick();

    EXPECT_TRUE(fm.forceSafeModeCalls.empty());
    EXPECT_EQ(fm.stopWatchdogCalls, 0u);
    EXPECT_EQ(fm.eventsFaultConfirmed.size(), 5u) << "They are still detected, counted and reported";
    EXPECT_TRUE(fm.eventsFaultActionTaken.empty());
    EXPECT_TRUE(fm.eventsFaultActionSuppressed.empty()) << "There is no action to suppress";
}

// ----------------------------------------------------------------------
// FaultManager-5: disposition contract at the port
// ----------------------------------------------------------------------

TEST_F(FaultManagerComponentTest, DispositionFollowsEnabledAndMaskAndActionMap) {
    RecordProperty("verifies", "FaultManager-5");

    // Only LOW_BATTERY and COMMAND_LOSS carry an action, so only they can ever
    // be claimed, and only when both gates open for their own bit.
    struct Case {
        bool enabled;
        U8 mask;
        FaultType::T type;
        Disposition::T expected;
    };
    const Case cases[] = {
        {false, 0x00, FaultType::LOW_BATTERY, Disposition::OBSERVED},
        {false, 0xFF, FaultType::LOW_BATTERY, Disposition::OBSERVED},
        {true, 0x00, FaultType::LOW_BATTERY, Disposition::OBSERVED},
        {true, BIT_COMMAND_LOSS, FaultType::LOW_BATTERY, Disposition::OBSERVED},
        {true, BIT_LOW_BATTERY, FaultType::LOW_BATTERY, Disposition::CLAIMED},
        {true, 0xFF, FaultType::LOW_BATTERY, Disposition::CLAIMED},
        {false, 0xFF, FaultType::COMMAND_LOSS, Disposition::OBSERVED},
        {true, BIT_LOW_BATTERY, FaultType::COMMAND_LOSS, Disposition::OBSERVED},
        {true, BIT_COMMAND_LOSS, FaultType::COMMAND_LOSS, Disposition::CLAIMED},
        {true, BIT_WATCHDOG_STOPPED, FaultType::WATCHDOG_STOPPED, Disposition::OBSERVED},
        {true, 0xFF, FaultType::WATCHDOG_STOPPED, Disposition::OBSERVED},
        {true, 0xFF, FaultType::FACE_TEMP_HIGH, Disposition::OBSERVED},
        {true, 0xFF, FaultType::ADCS_UNSTABLE, Disposition::OBSERVED},
    };

    for (const Case& c : cases) {
        FaultManager local("faultManager");
        FaultManagerComponentBase& localBase = static_cast<FaultManagerComponentBase&>(local);
        local.authorityEnabled = c.enabled;
        local.authorityMask = c.mask;
        localBase.parameterUpdated(FaultManagerComponentBase::PARAMID_AUTHORITY_ENABLED);

        const Disposition actual =
            localBase.faultIn_guarded(0, FaultType(c.type), FaultSource(FaultSource::MODE_MANAGER),
                                      Components::FaultSeverity(Components::FaultSeverity::CRITICAL), 0.0f);
        EXPECT_EQ(actual, c.expected) << "enabled=" << c.enabled << " mask=" << static_cast<int>(c.mask)
                                      << " type=" << static_cast<int>(c.type);
    }
}

TEST_F(FaultManagerComponentTest, AuthorityChangeIsAnnouncedAndReportedInTelemetry) {
    RecordProperty("verifies", "FaultManager-5");

    applyParameters();
    tick();
    EXPECT_EQ(fm.tlmAuthorityState.back(), 0u);
    EXPECT_TRUE(fm.eventsFaultAuthorityChanged.empty()) << "Applying the defaults is not a change";

    fm.authorityEnabled = true;
    fm.authorityMask = BIT_LOW_BATTERY;
    applyParameters();
    ASSERT_EQ(fm.eventsFaultAuthorityChanged.size(), 1u);
    EXPECT_TRUE(fm.eventsFaultAuthorityChanged[0].enabled);
    EXPECT_EQ(fm.eventsFaultAuthorityChanged[0].mask, BIT_LOW_BATTERY);

    tick();
    EXPECT_EQ(fm.tlmAuthorityState.back(), BIT_LOW_BATTERY);

    // Re-applying the same values is not a change.
    applyParameters();
    EXPECT_EQ(fm.eventsFaultAuthorityChanged.size(), 1u);
}

// ----------------------------------------------------------------------
// FaultManager-7: commands
// ----------------------------------------------------------------------

TEST_F(FaultManagerComponentTest, ClearFaultsAndGetFaultStatusReportAndRespondOk) {
    RecordProperty("verifies", "FaultManager-7");

    applyParameters();
    // COMMAND_LOSS first: it is not sampled, so it stays confirmed across the
    // ticks that follow. LOW_BATTERY is sampled and would re-arm on a tick
    // with no low-voltage sample, which is the behaviour, not a defect.
    driveToConfirmation(FaultType::COMMAND_LOSS, FaultSource::AUTH_ROUTER, 0.0f);
    driveToConfirmation(FaultType::LOW_BATTERY, FaultSource::MODE_MANAGER, 6.4f);

    base().GET_FAULT_STATUS_cmdHandler(0x100, 7);
    ASSERT_EQ(fm.eventsFaultStatusReport.size(), 1u);
    EXPECT_EQ(fm.eventsFaultStatusReport[0].active, BIT_LOW_BATTERY | BIT_COMMAND_LOSS);
    EXPECT_EQ(fm.eventsFaultStatusReport[0].detected, static_cast<U32>(2 * REPORTS_PAST_DEBOUNCE));
    EXPECT_EQ(fm.eventsFaultStatusReport[0].confirmed, 2u);
    EXPECT_EQ(fm.eventsFaultStatusReport[0].authority, 0u);
    ASSERT_EQ(fm.cmdResponses.size(), 1u);
    EXPECT_EQ(fm.cmdResponses[0].response, Fw::CmdResponse::OK);

    base().CLEAR_FAULTS_cmdHandler(0x101, 8);
    EXPECT_EQ(fm.eventsFaultsCleared, 1u);
    ASSERT_EQ(fm.cmdResponses.size(), 2u);
    EXPECT_EQ(fm.cmdResponses[1].response, Fw::CmdResponse::OK);

    tick();
    EXPECT_EQ(fm.tlmFaultsDetected.back(), 0u);
    EXPECT_EQ(fm.tlmFaultsConfirmed.back(), 0u);
    EXPECT_EQ(fm.tlmActiveFaults.back(), 0u);
    EXPECT_EQ(fm.tlmFaultCountLowBattery.back(), 0u);
    EXPECT_EQ(fm.tlmFaultCountCommandLoss.back(), 0u);

    // The cleared table keeps the configured debounce, not a reset one.
    for (int i = 1; i < DEBOUNCE_LOW_BATTERY; i++) {
        report(FaultType::LOW_BATTERY, FaultSource::MODE_MANAGER, Components::FaultSeverity::CRITICAL, 6.4f);
        tick();
    }
    EXPECT_EQ(fm.tlmFaultsConfirmed.back(), 0u) << "CLEAR_FAULTS must not shorten the debounce";
}

// ----------------------------------------------------------------------
// FaultManager-9: the locking contract
// ----------------------------------------------------------------------

TEST_F(FaultManagerComponentTest, NoActionPortIsCalledWhileTheGuardedLockIsHeld) {
    RecordProperty("verifies", "FaultManager-9");

    // Exercise both action paths as well as the shadow path: the recorder
    // flags any action port call made with a non-zero lock depth, which is the
    // condition that would deadlock against the guarded faultIn on target.
    fm.authorityEnabled = true;
    fm.authorityMask = 0xFF;
    applyParameters();

    for (const FaultType::T type : ALL_TYPES) {
        driveToConfirmation(type, FaultSource::MODE_MANAGER, 1.0f);
    }
    base().GET_FAULT_STATUS_cmdHandler(0x100, 1);
    base().CLEAR_FAULTS_cmdHandler(0x101, 2);
    tick();

    ASSERT_GT(fm.actionOrder.size(), 0u) << "The action paths must actually have run for this to prove anything";
    EXPECT_FALSE(fm.actionWhileLocked) << "An output action port was called while the guarded mutex was held";
    EXPECT_FALSE(fm.lockUnderflow) << "unLock() was called without a matching lock()";
    EXPECT_EQ(fm.lockDepth, 0) << "Every lock must be released";
    EXPECT_GT(fm.lockCalls, 0u);
}

}  // namespace
