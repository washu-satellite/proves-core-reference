// ======================================================================
// \title  test_FaultManager_FaultTable.cpp
// \brief  Host unit tests for the pure FaultManager bookkeeping module.
//
// Level: Unit. FaultTable.cpp is F Prime free by construction, so this binary
// links nothing but the module itself (test/unit-tests/README.md).
//
// Requirements verified: FaultManager-3, FaultManager-4, FaultManager-5,
// FaultManager-7.
//
// Oracle (TP-3): every expected value below is a named constant taken from the
// requirement's pass criteria and from the design sources those criteria
// quote, never from the logic under test:
//   * the LOW_BATTERY debounce of 10 is ModeManager's SafeModeDebounceSeconds
//     default (ModeManager.fpp "param SafeModeDebounceSeconds U32 default 10");
//   * the COMMAND_LOSS action "stop the watchdog, then force safe mode" is
//     what ModeManager::commandLossCheck does today (the AuthenticationRouter
//     that first owned it was retired at the 2026-09 upstream sync);
//   * WATCHDOG_STOPPED has no action because the hardware reset is already
//     under way when Watchdog::stop_handler runs;
//   * the four thermal types have no action because ThermalManager's whole
//     response to an out-of-range reading is its WARNING event.
// ======================================================================

#include <gtest/gtest.h>

#include "PROVESFlightControllerReference/Components/FaultManager/FaultTable.hpp"

namespace {

using Components::FaultLogic::Action;
using Components::FaultLogic::Decision;
using Components::FaultLogic::FaultTable;
using Components::FaultLogic::Policy;
using Components::FaultLogic::Report;
using Components::FaultLogic::Type;
namespace FL = Components::FaultLogic;

// Debounce oracle, ModeManager.fpp parameter default (also quoted in the
// FaultManager-3 pass criteria as "With debounce 10").
constexpr uint8_t LOW_BATTERY_DEBOUNCE = 10;

// Every fault type the table tracks, in mask-bit order.
constexpr Type ALL_TYPES[8] = {FL::FACE_TEMP_HIGH, FL::FACE_TEMP_LOW, FL::BATT_TEMP_HIGH,   FL::BATT_TEMP_LOW,
                               FL::LOW_BATTERY,    FL::COMMAND_LOSS,  FL::WATCHDOG_STOPPED, FL::ADCS_UNSTABLE};

//! Build a report for a type; the source and value do not affect debounce.
Report reportOf(Type type, float value = 0.0f, FL::Source source = FL::MODE_MANAGER) {
    Report r;
    r.type = type;
    r.source = source;
    r.value = value;
    return r;
}

//! Drain one tick into a fixed buffer and return how many decisions came out.
uint8_t tickInto(FaultTable& table, uint32_t tickNumber, Decision* out) {
    return table.tick(tickNumber, out, FL::NUM_FAULT_TYPES);
}

// ----------------------------------------------------------------------
// FaultManager-3: sampled debounce
// ----------------------------------------------------------------------

TEST(FaultManagerFaultTable, SampledFaultConfirmsOnTheNthConsecutiveReportOnly) {
    RecordProperty("verifies", "FaultManager-3");

    FaultTable table;
    table.setDebounce(FL::LOW_BATTERY, LOW_BATTERY_DEBOUNCE);
    Decision decisions[FL::NUM_FAULT_TYPES];

    // Nine consecutive reporting ticks must not confirm.
    for (uint32_t t = 1; t < LOW_BATTERY_DEBOUNCE; t++) {
        EXPECT_FALSE(table.report(reportOf(FL::LOW_BATTERY, 6.5f), t))
            << "LOW_BATTERY must not confirm before " << static_cast<int>(LOW_BATTERY_DEBOUNCE)
            << " consecutive reports (report " << t << ")";
        EXPECT_EQ(tickInto(table, t, decisions), 0u);
    }

    // The tenth report is the confirmation edge, and it fires exactly once.
    EXPECT_TRUE(table.report(reportOf(FL::LOW_BATTERY, 6.5f), LOW_BATTERY_DEBOUNCE));
    ASSERT_EQ(tickInto(table, LOW_BATTERY_DEBOUNCE, decisions), 1u);
    EXPECT_EQ(decisions[0].type, FL::LOW_BATTERY);
    EXPECT_EQ(decisions[0].action, FL::SAFE_MODE);
    EXPECT_FLOAT_EQ(decisions[0].value, 6.5f);

    // Staying in the fault does not re-confirm.
    for (uint32_t t = LOW_BATTERY_DEBOUNCE + 1; t <= LOW_BATTERY_DEBOUNCE + 5; t++) {
        EXPECT_FALSE(table.report(reportOf(FL::LOW_BATTERY, 6.5f), t));
        EXPECT_EQ(tickInto(table, t, decisions), 0u) << "The confirmation edge must fire once, not once per tick";
    }
    EXPECT_EQ(table.totalConfirmed(), 1u);
}

TEST(FaultManagerFaultTable, SampledFaultRearmsAfterATickWithNoReport) {
    RecordProperty("verifies", "FaultManager-3");

    FaultTable table;
    table.setDebounce(FL::LOW_BATTERY, LOW_BATTERY_DEBOUNCE);
    Decision decisions[FL::NUM_FAULT_TYPES];

    // Nine reports, then a silent tick: the debounce must re-arm.
    for (uint32_t t = 1; t < LOW_BATTERY_DEBOUNCE; t++) {
        table.report(reportOf(FL::LOW_BATTERY, 6.5f), t);
        tickInto(table, t, decisions);
    }
    tickInto(table, LOW_BATTERY_DEBOUNCE, decisions);  // silent tick

    // Nine further reports must therefore still not confirm.
    for (uint32_t t = LOW_BATTERY_DEBOUNCE + 1; t < 2 * LOW_BATTERY_DEBOUNCE; t++) {
        EXPECT_FALSE(table.report(reportOf(FL::LOW_BATTERY, 6.5f), t))
            << "A tick without a report must reset the consecutive count";
        EXPECT_EQ(tickInto(table, t, decisions), 0u);
    }
    EXPECT_EQ(table.totalConfirmed(), 0u);

    // The tenth after the re-arm confirms.
    EXPECT_TRUE(table.report(reportOf(FL::LOW_BATTERY, 6.5f), 2 * LOW_BATTERY_DEBOUNCE));
    EXPECT_EQ(tickInto(table, 2 * LOW_BATTERY_DEBOUNCE, decisions), 1u);
}

TEST(FaultManagerFaultTable, EventFaultsConfirmOnTheFirstReportAndStayConfirmed) {
    RecordProperty("verifies", "FaultManager-3");

    FaultTable table;
    Decision decisions[FL::NUM_FAULT_TYPES];

    // COMMAND_LOSS is not sampled: one report confirms, and a silent tick does
    // not re-arm it, so ActiveFaults keeps the bit until CLEAR_FAULTS.
    EXPECT_TRUE(table.report(reportOf(FL::COMMAND_LOSS), 1));
    ASSERT_EQ(tickInto(table, 1, decisions), 1u);
    EXPECT_EQ(decisions[0].type, FL::COMMAND_LOSS);

    for (uint32_t t = 2; t <= 5; t++) {
        EXPECT_EQ(tickInto(table, t, decisions), 0u);
        EXPECT_NE(table.activeMask() & FL::bitFor(FL::COMMAND_LOSS), 0)
            << "An event-driven fault must stay confirmed across silent ticks";
    }
}

// ----------------------------------------------------------------------
// FaultManager-4: the action map equals today's behaviour
// ----------------------------------------------------------------------

TEST(FaultManagerFaultTable, DefaultPolicyReproducesTodaysProducerBehaviour) {
    RecordProperty("verifies", "FaultManager-4");

    // Expected policy per type, restated from the pass criteria and the design
    // sources they quote, not read back from defaultPolicy().
    struct Expected {
        Type type;
        uint8_t debounce;
        Action action;
        bool sampled;
    };
    const Expected expected[8] = {{FL::FACE_TEMP_HIGH, 1, FL::NO_ACTION, false},
                                  {FL::FACE_TEMP_LOW, 1, FL::NO_ACTION, false},
                                  {FL::BATT_TEMP_HIGH, 1, FL::NO_ACTION, false},
                                  {FL::BATT_TEMP_LOW, 1, FL::NO_ACTION, false},
                                  {FL::LOW_BATTERY, LOW_BATTERY_DEBOUNCE, FL::SAFE_MODE, true},
                                  {FL::COMMAND_LOSS, 1, FL::SAFE_MODE_AND_REBOOT, false},
                                  {FL::WATCHDOG_STOPPED, 1, FL::NO_ACTION, false},
                                  {FL::ADCS_UNSTABLE, 1, FL::NO_ACTION, false}};

    for (const Expected& e : expected) {
        const Policy policy = Components::FaultLogic::defaultPolicy(e.type);
        EXPECT_EQ(policy.debounce, e.debounce) << "debounce for type " << static_cast<int>(e.type);
        EXPECT_EQ(policy.action, e.action) << "action for type " << static_cast<int>(e.type);
        EXPECT_EQ(policy.sampled, e.sampled) << "sampled flag for type " << static_cast<int>(e.type);
    }

    // NONE is not a fault and carries no action.
    EXPECT_EQ(Components::FaultLogic::defaultPolicy(FL::NONE).action, FL::NO_ACTION);
}

// ----------------------------------------------------------------------
// FaultManager-5: the shadow gate truth table
// ----------------------------------------------------------------------

TEST(FaultManagerFaultTable, ClaimsIsTrueOnlyWhenEnabledAndMaskedAndActionable) {
    RecordProperty("verifies", "FaultManager-5");

    for (const Type type : ALL_TYPES) {
        const uint8_t bit = FL::bitFor(type);
        const bool actionable = Components::FaultLogic::defaultPolicy(type).action != FL::NO_ACTION;

        // Disabled: never claimed, whatever the mask.
        EXPECT_FALSE(FaultTable::claims(type, false, 0x00));
        EXPECT_FALSE(FaultTable::claims(type, false, bit));
        EXPECT_FALSE(FaultTable::claims(type, false, 0xFF))
            << "AUTHORITY_ENABLED false must veto every type (type " << static_cast<int>(type) << ")";

        // Enabled but the bit is clear: never claimed.
        EXPECT_FALSE(FaultTable::claims(type, true, static_cast<uint8_t>(0xFF & ~bit)))
            << "A cleared mask bit must veto the type (type " << static_cast<int>(type) << ")";
        EXPECT_FALSE(FaultTable::claims(type, true, 0x00));

        // Enabled with the bit set: claimed exactly when the type has an action.
        EXPECT_EQ(FaultTable::claims(type, true, bit), actionable)
            << "claims() must follow the action map for type " << static_cast<int>(type);
        EXPECT_EQ(FaultTable::claims(type, true, 0xFF), actionable);
    }

    // NONE has no bit and can never be claimed.
    EXPECT_EQ(FL::bitFor(FL::NONE), 0);
    EXPECT_FALSE(FaultTable::claims(FL::NONE, true, 0xFF));
}

TEST(FaultManagerFaultTable, MaskBitsAreOneShiftedByTheTypeOrdinal) {
    RecordProperty("verifies", "FaultManager-5");

    // Bit assignment is the contract AUTHORITY_MASK is documented against.
    const uint8_t expected[8] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};
    for (int i = 0; i < 8; i++) {
        EXPECT_EQ(FL::bitFor(ALL_TYPES[i]), expected[i]) << "bit for type " << static_cast<int>(ALL_TYPES[i]);
    }
}

// ----------------------------------------------------------------------
// FaultManager-7: clear() zeroes everything
// ----------------------------------------------------------------------

TEST(FaultManagerFaultTable, ClearZeroesEveryCounterConfirmationAndMask) {
    RecordProperty("verifies", "FaultManager-7");

    FaultTable table;
    Decision decisions[FL::NUM_FAULT_TYPES];

    // Confirm one of every type, so every counter and mask bit is non-zero.
    for (const Type type : ALL_TYPES) {
        const uint8_t debounce = Components::FaultLogic::defaultPolicy(type).debounce;
        for (uint8_t n = 0; n < debounce; n++) {
            table.report(reportOf(type, 1.5f), 1);
        }
    }
    tickInto(table, 1, decisions);
    ASSERT_EQ(table.activeMask(), 0xFF) << "Every tracked type must be confirmed before the clear";
    ASSERT_GT(table.totalReports(), 0u);
    ASSERT_GT(table.totalConfirmed(), 0u);

    table.clear();

    EXPECT_EQ(table.totalReports(), 0u);
    EXPECT_EQ(table.totalConfirmed(), 0u);
    EXPECT_EQ(table.activeMask(), 0u);
    for (const Type type : ALL_TYPES) {
        EXPECT_EQ(table.status(type).reports, 0u) << "reports for type " << static_cast<int>(type);
        EXPECT_EQ(table.status(type).consecutive, 0u);
        EXPECT_FALSE(table.status(type).confirmed);
        EXPECT_FALSE(table.status(type).pending);
    }
    // No stale confirmation is drained on the next tick either.
    EXPECT_EQ(tickInto(table, 2, decisions), 0u);
}

// ----------------------------------------------------------------------
// Defensive behaviour of the module's own inputs
// ----------------------------------------------------------------------

TEST(FaultManagerFaultTable, ZeroDebounceIsCoercedToOneAndNoneIsIgnored) {
    RecordProperty("verifies", "FaultManager-3");

    FaultTable table;
    Decision decisions[FL::NUM_FAULT_TYPES];

    // A parameter of 0 must not mean "confirm without a report".
    table.setDebounce(FL::LOW_BATTERY, 0);
    EXPECT_EQ(tickInto(table, 1, decisions), 0u) << "A zero debounce must not confirm a fault that never reported";
    EXPECT_TRUE(table.report(reportOf(FL::LOW_BATTERY, 6.0f), 2));
    EXPECT_EQ(tickInto(table, 2, decisions), 1u);

    // NONE is not a fault: reporting it changes nothing.
    FaultTable other;
    EXPECT_FALSE(other.report(reportOf(FL::NONE), 1));
    EXPECT_EQ(other.totalReports(), 0u);
    EXPECT_EQ(other.activeMask(), 0u);
}

}  // namespace
