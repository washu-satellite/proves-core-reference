// ======================================================================
// \title  test_PowerMonitor_ChargeStatus.cpp
// \brief  Host unit tests for the PowerMonitor battery-charge status
//         (chargeStatusGet port, Charging channel, ChargeStateChanged event).
//
// Level: Unit. Runs on the host against the recorder stub in
// support/PROVESFlightControllerReference/Components/PowerMonitor/; no F Prime
// or Zephyr code is linked (test/unit-tests/README.md).
//
// Requirement verified: PWR-MON-REQ-011.
//   PWR-MON-REQ-011 pass criteria: "Host stub at the default interval,
//     chargeStatusGet connected: each run tick reads the port once; Charging is
//     written ON after a HIGH read and OFF after a LOW read; ChargeStateChanged
//     fires on the first read and on each change only (reads HIGH,HIGH,LOW,
//     LOW,HIGH give events ON,OFF,ON); with the port unconnected it is never
//     invoked and no Charging write or ChargeStateChanged event occurs over 5
//     ticks; system/solar reads and TotalPowerConsumption are the same with
//     the port connected or not".
//
// Oracle (TP-3): the mapping HIGH -> ON is Cycle L 01-normative.md R4.2
// ("Charging is ON iff the read is Fw::Logic::HIGH"; R4.1 makes the pin
// active-low in the devicetree so logical HIGH means charging). The energy
// figure is the unit conversion mWh = W * (dt_s / 3600) * 1000, not a value
// read out of the component. Only the default collection interval (1 s, every
// tick executes) is exercised: the criterion is stated at that interval.
//
// Telemetry: Charging is "update on change" in the .fpp; that filtering is
// done by the generated code, so a component may write the channel on every
// read. Only the most recent written value is asserted.
// ======================================================================

#include <gtest/gtest.h>

#include <vector>

#include "PROVESFlightControllerReference/Components/PowerMonitor/PowerMonitor.hpp"

namespace {

using Components::PowerMonitor;
using Components::PowerMonitorComponentBase;

// Any non-zero clock origin avoids the component's "first call" time latch
// recurring (same choice as test_PowerMonitor_CollectionInterval.cpp).
constexpr U32 T0_S = 100;

// One watt for one second is 1000/3600 mWh.
constexpr F32 MWH_PER_W_SECOND = 1000.0f / 3600.0f;

class PowerMonitorChargeStatusTest : public ::testing::Test {
  protected:
    PowerMonitorChargeStatusTest() : power("powerMonitor") {}

    void SetUp() override {
        this->power.sysPower = 1.0;
        this->power.solPower = 0.0;
        this->nextSecond = T0_S;
    }

    //! One 1 Hz rate-group tick with the charge pin at `level`.
    void tickWith(Fw::Logic::T level) {
        this->power.chargeStatusLevel = level;
        this->power.now = Fw::Time(this->nextSecond++, 0);
        static_cast<PowerMonitorComponentBase&>(this->power).run_handler(0, 0);
    }

    //! One tick without changing the pin.
    void tick() {
        this->power.now = Fw::Time(this->nextSecond++, 0);
        static_cast<PowerMonitorComponentBase&>(this->power).run_handler(0, 0);
    }

    PowerMonitor power;
    U32 nextSecond = T0_S;
};

TEST_F(PowerMonitorChargeStatusTest, FirstHighReadReportsChargingOnWithOneEvent) {
    RecordProperty("verifies", "PWR-MON-REQ-011");
    this->power.chargeStatusConnected = true;

    this->tickWith(Fw::Logic::HIGH);

    EXPECT_EQ(this->power.chargeStatusReads, 1u) << "The port must be read on the run tick";
    ASSERT_FALSE(this->power.tlmCharging.empty()) << "Charging must be written after the first read";
    EXPECT_EQ(this->power.tlmCharging.back(), Fw::On::ON);
    ASSERT_EQ(this->power.eventsChargeStateChanged.size(), 1u) << "ChargeStateChanged must fire on the first read";
    EXPECT_EQ(this->power.eventsChargeStateChanged[0], Fw::On::ON);
}

TEST_F(PowerMonitorChargeStatusTest, FirstLowReadReportsChargingOffWithOneEvent) {
    RecordProperty("verifies", "PWR-MON-REQ-011");
    this->power.chargeStatusConnected = true;

    this->tickWith(Fw::Logic::LOW);

    EXPECT_EQ(this->power.chargeStatusReads, 1u);
    ASSERT_FALSE(this->power.tlmCharging.empty());
    EXPECT_EQ(this->power.tlmCharging.back(), Fw::On::OFF);
    ASSERT_EQ(this->power.eventsChargeStateChanged.size(), 1u)
        << "ChargeStateChanged must fire once on the first read even when not charging";
    EXPECT_EQ(this->power.eventsChargeStateChanged[0], Fw::On::OFF);
}

TEST_F(PowerMonitorChargeStatusTest, EventOnlyOnChangeAndChannelFollowsEveryRead) {
    RecordProperty("verifies", "PWR-MON-REQ-011");
    this->power.chargeStatusConnected = true;

    const Fw::Logic::T reads[] = {Fw::Logic::HIGH, Fw::Logic::HIGH, Fw::Logic::LOW, Fw::Logic::LOW, Fw::Logic::HIGH};
    const Fw::On::T expectedChannel[] = {Fw::On::ON, Fw::On::ON, Fw::On::OFF, Fw::On::OFF, Fw::On::ON};
    for (size_t i = 0; i < 5; ++i) {
        this->tickWith(reads[i]);
        EXPECT_EQ(this->power.chargeStatusReads, i + 1) << "One read per run tick (tick " << i + 1 << ")";
        ASSERT_FALSE(this->power.tlmCharging.empty());
        EXPECT_EQ(this->power.tlmCharging.back(), expectedChannel[i]) << "Charging after tick " << i + 1;
    }

    const std::vector<Fw::On::T> expectedEvents = {Fw::On::ON, Fw::On::OFF, Fw::On::ON};
    EXPECT_EQ(this->power.eventsChargeStateChanged, expectedEvents)
        << "ChargeStateChanged must fire on the first read and on each change only";
}

TEST_F(PowerMonitorChargeStatusTest, UnconnectedPortIsNeverReadAndNothingNewIsEmitted) {
    RecordProperty("verifies", "PWR-MON-REQ-011");
    ASSERT_FALSE(this->power.chargeStatusConnected) << "chargeStatusGet must default to unconnected on the host";

    for (int i = 0; i < 5; ++i) {
        this->tick();
    }

    EXPECT_EQ(this->power.chargeStatusReads, 0u)
        << "An unconnected chargeStatusGet must never be invoked (an FW_ASSERT on the target)";
    EXPECT_EQ(this->power.chargeStatusReadsWhileUnconnected, 0u);
    EXPECT_TRUE(this->power.tlmCharging.empty()) << "No Charging write without a connected port";
    EXPECT_TRUE(this->power.eventsChargeStateChanged.empty()) << "No ChargeStateChanged without a connected port";

    // Existing behaviour is untouched: every tick samples, energy accumulates.
    EXPECT_EQ(this->power.sysPowerReads, 5u);
    EXPECT_EQ(this->power.solPowerReads, 5u);
    ASSERT_FALSE(this->power.tlmTotalPowerConsumption.empty());
    EXPECT_NEAR(this->power.tlmTotalPowerConsumption.back(), 4.0f * MWH_PER_W_SECOND, 0.001f);
}

TEST_F(PowerMonitorChargeStatusTest, ConnectedPortLeavesTotalsAndSamplingUnchanged) {
    RecordProperty("verifies", "PWR-MON-REQ-011");
    this->power.chargeStatusConnected = true;

    // Six ticks: the first latches the clock, five one-second deltas at 1 W.
    for (int i = 0; i < 6; ++i) {
        this->tickWith((i % 2 == 0) ? Fw::Logic::HIGH : Fw::Logic::LOW);
    }

    EXPECT_EQ(this->power.sysVoltageReads, 6u);
    EXPECT_EQ(this->power.sysCurrentReads, 6u);
    EXPECT_EQ(this->power.sysPowerReads, 6u);
    EXPECT_EQ(this->power.solVoltageReads, 6u);
    EXPECT_EQ(this->power.solCurrentReads, 6u);
    EXPECT_EQ(this->power.solPowerReads, 6u);
    ASSERT_FALSE(this->power.tlmTotalPowerConsumption.empty());
    EXPECT_NEAR(this->power.tlmTotalPowerConsumption.back(), 5.0f * MWH_PER_W_SECOND, 0.001f);
    EXPECT_TRUE(this->power.eventsCollectionIntervalRejected.empty());
}

}  // namespace
