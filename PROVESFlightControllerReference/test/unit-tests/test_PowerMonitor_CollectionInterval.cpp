// ======================================================================
// \title  test_PowerMonitor_CollectionInterval.cpp
// \brief  Host unit tests for the PowerMonitor COLLECTION_INTERVAL_S parameter
//         and the energy-accumulation window that follows it.
//
// Level: Unit. Runs on the host against the recorder stub in
// support/PROVESFlightControllerReference/Components/PowerMonitor/; no F Prime
// or Zephyr code is linked (test/unit-tests/README.md).
//
// Requirements verified: PWR-MON-REQ-008, PWR-MON-REQ-009, PWR-MON-REQ-010.
//   PWR-MON-REQ-008 pass criteria: "Over 12 ticks at interval 3 exactly 4
//     sample sweeps occur (ticks 1,4,7,10); at the default interval every tick
//     samples".
//   PWR-MON-REQ-009 pass criteria: "An interval of 0 or >60 or an INVALID param
//     yields effective 1 s, one CollectionIntervalRejected event and
//     CollectionIntervalS telemetry 1; TotalPowerConsumption keeps accumulating
//     at interval 30 s".
//   PWR-MON-REQ-010 pass criteria: "With COLLECTION_INTERVAL_S = 3 VALID in the
//     stub and no parameterUpdated call, after parametersLoaded() 12 ticks make
//     exactly 4 system-power requests (ticks 1,4,7,10) and the first
//     CollectionIntervalS write is 3; with paramValidity INVALID every tick
//     samples, one CollectionIntervalRejected, first write 1; a
//     parameterUpdated after boot applies as today with no duplicate event".
//
// Oracle (TP-3): the energy figures are computed from the unit conversion the
// requirement implies — mWh = W * (dt_s / 3600) * 1000 — not read out of the
// component. The 10 s time-jump window at the default interval is the literal
// this cycle replaced (PowerMonitor.cpp before Cycle B), restated here so a
// regression at the default configuration fails the test.
// ======================================================================

#include <gtest/gtest.h>

#include "PROVESFlightControllerReference/Components/PowerMonitor/PowerMonitor.hpp"

namespace {

using Components::PowerMonitor;
using Components::PowerMonitorComponentBase;

// Parameter id of COLLECTION_INTERVAL_S, PowerMonitor.fpp "id 0".
constexpr FwPrmIdType PARAMID_INTERVAL = 0;

// Clock origin. Any non-zero start avoids the "first call" sentinel
// (m_lastUpdateTime_s == 0.0) recurring on every run.
constexpr U32 T0_S = 100;

// One watt for a whole second is 1000/3600 mWh.
constexpr F32 MWH_PER_W_SECOND = 1000.0f / 3600.0f;

class PowerMonitorCollectionIntervalTest : public ::testing::Test {
  protected:
    PowerMonitorCollectionIntervalTest() : power("powerMonitor") {}

    void SetUp() override {
        // 1 W of system draw, no solar contribution, so the accumulated energy
        // is a clean function of elapsed time.
        this->power.sysPower = 1.0;
        this->power.solPower = 0.0;
    }

    //! Drive the parameter-update path the generated code calls after a store.
    void applyInterval(U8 requested, Fw::ParamValid validity) {
        this->power.collectionIntervalS = requested;
        this->power.paramValidity = validity;
        static_cast<PowerMonitorComponentBase&>(this->power).parameterUpdated(PARAMID_INTERVAL);
    }

    //! One rate-group tick at the given absolute time. run_handler is a private
    //! override, so it is driven through the base class as the rate group does.
    void tickAt(U32 seconds) {
        this->power.now = Fw::Time(seconds, 0);
        static_cast<PowerMonitorComponentBase&>(this->power).run_handler(0, 0);
    }

    //! `count` consecutive 1 Hz ticks starting at T0_S.
    void tickSeconds(U32 count) {
        for (U32 i = 0; i < count; ++i) {
            this->tickAt(T0_S + i);
        }
    }

    //! Stage the recorder as the generated loadParameters() leaves it after
    //! /prmDb.dat is read, then call the parametersLoaded() hook exactly as the
    //! framework does. No parameterUpdated call is made.
    void bootWith(U8 saved, Fw::ParamValid validity) {
        this->power.collectionIntervalS = saved;
        this->power.paramValidity = validity;
        static_cast<PowerMonitorComponentBase&>(this->power).parametersLoaded();
    }

    PowerMonitor power;
};

TEST_F(PowerMonitorCollectionIntervalTest, IntervalThreeSamplesFourTimesInTwelveTicks) {
    RecordProperty("verifies", "PWR-MON-REQ-008");
    this->applyInterval(3, Fw::ParamValid::VALID);

    this->tickSeconds(12);

    EXPECT_EQ(this->power.sysVoltageReads, 4u);
    EXPECT_EQ(this->power.sysCurrentReads, 4u);
    EXPECT_EQ(this->power.sysPowerReads, 4u);
    EXPECT_EQ(this->power.solVoltageReads, 4u);
    EXPECT_EQ(this->power.solCurrentReads, 4u);
    EXPECT_EQ(this->power.solPowerReads, 4u);
    ASSERT_FALSE(this->power.tlmCollectionIntervalS.empty());
    EXPECT_EQ(this->power.tlmCollectionIntervalS.back(), 3);
}

TEST_F(PowerMonitorCollectionIntervalTest, DefaultBehaviourUnchanged) {
    RecordProperty("verifies", "PWR-MON-REQ-008");
    // No parameter is ever set: the component must sample on every tick.
    this->tickSeconds(5);

    EXPECT_EQ(this->power.sysPowerReads, 5u);
    EXPECT_EQ(this->power.solPowerReads, 5u);
}

TEST_F(PowerMonitorCollectionIntervalTest, DefaultIntervalAccumulatesEverySecond) {
    RecordProperty("verifies", "PWR-MON-REQ-009");
    // Ticks at T0..T0+5. The first executed run only latches the clock, so five
    // one-second deltas are accumulated over six ticks.
    this->tickSeconds(6);

    ASSERT_FALSE(this->power.tlmTotalPowerConsumption.empty());
    EXPECT_NEAR(this->power.tlmTotalPowerConsumption.back(), 5.0f * MWH_PER_W_SECOND, 0.001f);
}

TEST_F(PowerMonitorCollectionIntervalTest, DefaultIntervalStillDropsATwelveSecondJump) {
    RecordProperty("verifies", "PWR-MON-REQ-009");
    // At the default interval the accumulation window is 10 s, exactly the
    // literal in force before this cycle: a 12 s gap is a time jump and is
    // dropped, while a 9 s gap is accumulated.
    this->tickAt(T0_S);       // latches the clock
    this->tickAt(T0_S + 12);  // dropped
    ASSERT_FALSE(this->power.tlmTotalPowerConsumption.empty());
    EXPECT_NEAR(this->power.tlmTotalPowerConsumption.back(), 0.0f, 0.001f);

    this->tickAt(T0_S + 21);  // 9 s later: accumulated
    EXPECT_NEAR(this->power.tlmTotalPowerConsumption.back(), 9.0f * MWH_PER_W_SECOND, 0.001f);
}

TEST_F(PowerMonitorCollectionIntervalTest, IntervalThirtyKeepsAccumulating) {
    RecordProperty("verifies", "PWR-MON-REQ-009");
    // The pre-Cycle-B 10 s window would have discarded every 30 s delta and
    // frozen TotalPowerConsumption. The window now follows the interval.
    this->applyInterval(30, Fw::ParamValid::VALID);

    this->tickSeconds(61);  // executed runs at ticks 1, 31, 61

    EXPECT_EQ(this->power.sysPowerReads, 3u);
    ASSERT_FALSE(this->power.tlmTotalPowerConsumption.empty());
    // Two 30 s deltas at 1 W.
    EXPECT_NEAR(this->power.tlmTotalPowerConsumption.back(), 60.0f * MWH_PER_W_SECOND, 0.01f);
}

TEST_F(PowerMonitorCollectionIntervalTest, ZeroIsRejectedAndFallsBackToOne) {
    RecordProperty("verifies", "PWR-MON-REQ-009");
    this->applyInterval(0, Fw::ParamValid::VALID);

    ASSERT_EQ(this->power.eventsCollectionIntervalRejected.size(), 1u);
    EXPECT_EQ(this->power.eventsCollectionIntervalRejected.front(), 0);
    ASSERT_FALSE(this->power.tlmCollectionIntervalS.empty());
    EXPECT_EQ(this->power.tlmCollectionIntervalS.back(), 1);

    this->tickSeconds(5);
    EXPECT_EQ(this->power.sysPowerReads, 5u);
}

TEST_F(PowerMonitorCollectionIntervalTest, AboveRangeIsRejectedAndFallsBackToOne) {
    RecordProperty("verifies", "PWR-MON-REQ-009");
    this->applyInterval(61, Fw::ParamValid::VALID);

    ASSERT_EQ(this->power.eventsCollectionIntervalRejected.size(), 1u);
    EXPECT_EQ(this->power.eventsCollectionIntervalRejected.front(), 61);
    ASSERT_FALSE(this->power.tlmCollectionIntervalS.empty());
    EXPECT_EQ(this->power.tlmCollectionIntervalS.back(), 1);

    this->tickSeconds(5);
    EXPECT_EQ(this->power.sysPowerReads, 5u);
}

TEST_F(PowerMonitorCollectionIntervalTest, InvalidParamIsRejectedAndFallsBackToOne) {
    RecordProperty("verifies", "PWR-MON-REQ-009");
    this->applyInterval(30, Fw::ParamValid::INVALID);

    ASSERT_EQ(this->power.eventsCollectionIntervalRejected.size(), 1u);
    EXPECT_EQ(this->power.eventsCollectionIntervalRejected.front(), 30);
    ASSERT_FALSE(this->power.tlmCollectionIntervalS.empty());
    EXPECT_EQ(this->power.tlmCollectionIntervalS.back(), 1);

    this->tickSeconds(5);
    EXPECT_EQ(this->power.sysPowerReads, 5u);
}

// ----------------------------------------------------------------------
// PWR-MON-REQ-010: a saved interval is applied at boot
// ----------------------------------------------------------------------

TEST_F(PowerMonitorCollectionIntervalTest, SavedIntervalIsEffectiveOnTheFirstTickAfterBoot) {
    RecordProperty("verifies", "PWR-MON-REQ-010");
    // The database holds 3 (as after PRM_SAVE_FILE and a reboot); the framework
    // calls parametersLoaded() and nothing else before the first tick.
    this->bootWith(3, Fw::ParamValid::VALID);

    ASSERT_FALSE(this->power.tlmCollectionIntervalS.empty());
    EXPECT_EQ(this->power.tlmCollectionIntervalS.front(), 3);
    EXPECT_TRUE(this->power.eventsCollectionIntervalRejected.empty());

    this->tickSeconds(12);

    EXPECT_EQ(this->power.sysPowerReads, 4u);
}

TEST_F(PowerMonitorCollectionIntervalTest, InvalidSavedIntervalAtBootFallsBackToOneWithOneRejection) {
    RecordProperty("verifies", "PWR-MON-REQ-010");
    // Host-stub case: on the target loadParameters() leaves VALID or DEFAULT.
    this->bootWith(30, Fw::ParamValid::INVALID);

    ASSERT_EQ(this->power.eventsCollectionIntervalRejected.size(), 1u);
    EXPECT_EQ(this->power.eventsCollectionIntervalRejected.front(), 30);
    ASSERT_FALSE(this->power.tlmCollectionIntervalS.empty());
    EXPECT_EQ(this->power.tlmCollectionIntervalS.front(), 1);

    this->tickSeconds(5);
    EXPECT_EQ(this->power.sysPowerReads, 5u);
    EXPECT_EQ(this->power.eventsCollectionIntervalRejected.size(), 1u);
}

TEST_F(PowerMonitorCollectionIntervalTest, ParameterSetAfterBootStillAppliesOnce) {
    RecordProperty("verifies", "PWR-MON-REQ-010");
    this->bootWith(3, Fw::ParamValid::VALID);

    // A PRM_SET after boot reaches parameterUpdated exactly as today: one
    // write per application, so boot + set is two writes, not three.
    this->applyInterval(6, Fw::ParamValid::VALID);
    ASSERT_EQ(this->power.tlmCollectionIntervalS.size(), 2u);
    EXPECT_EQ(this->power.tlmCollectionIntervalS[1], 6);
    EXPECT_TRUE(this->power.eventsCollectionIntervalRejected.empty());

    this->tickSeconds(12);
    // Ticks 1 and 7 run at interval 6.
    EXPECT_EQ(this->power.sysPowerReads, 2u);

    // A rejected set after boot emits its one event; the boot refresh adds none.
    this->applyInterval(0, Fw::ParamValid::VALID);
    EXPECT_EQ(this->power.eventsCollectionIntervalRejected.size(), 1u);
    EXPECT_EQ(this->power.tlmCollectionIntervalS.back(), 1);
}

}  // namespace
