// ======================================================================
// \title  test_ADCS_CollectionInterval.cpp
// \brief  Host unit tests for the ADCS COLLECTION_INTERVAL_S parameter.
//
// Level: Unit. Runs on the host against the recorder stub in
// support/PROVESFlightControllerReference/Components/ADCS/; no F Prime or
// Zephyr code is linked (test/unit-tests/README.md).
//
// Requirements verified: ADCS-1, ADCS-2, ADCS-3.
//   ADCS-1 pass criteria: "Over 12 ticks at interval 3 exactly 4 sweeps occur
//     (ticks 1,4,7,10); at the default interval every tick sweeps".
//   ADCS-2 pass criteria: "An interval of 0 or >60 or an INVALID param yields
//     effective 1 s, one CollectionIntervalRejected event, and
//     CollectionIntervalS telemetry 1".
//   ADCS-3 pass criteria: "With COLLECTION_INTERVAL_S = 3 VALID in the stub and
//     no parameterUpdated call, after parametersLoaded() 12 ticks perform
//     exactly 4 light-sensor sweeps (ticks 1,4,7,10) and the first
//     CollectionIntervalS write is 3; with paramValidity INVALID every tick
//     sweeps, one CollectionIntervalRejected, first write 1; a parameterUpdated
//     after boot applies as today with no duplicate event".
//
// Oracle (TP-3): the sweep shape (6 light-sensor reads per sweep) comes from
// ADCS.fpp numLightSensors = 6; the schedule and the fallback rule come from
// the two pass criteria quoted above.
// ======================================================================

#include <gtest/gtest.h>

#include "PROVESFlightControllerReference/Components/ADCS/ADCS.hpp"

namespace {

using Components::ADCS;
using Components::ADCSComponentBase;

// Port count, ADCS.fpp numLightSensors.
constexpr U32 LIGHT_SENSORS = 6;

// Parameter id of COLLECTION_INTERVAL_S, ADCS.fpp "id 0".
constexpr FwPrmIdType PARAMID_INTERVAL = 0;

class AdcsCollectionIntervalTest : public ::testing::Test {
  protected:
    AdcsCollectionIntervalTest() : adcs("adcs") {}

    //! Drive the parameter-update path the generated code calls after a store.
    void applyInterval(U8 requested, Fw::ParamValid validity) {
        this->adcs.collectionIntervalS = requested;
        this->adcs.paramValidity = validity;
        static_cast<ADCSComponentBase&>(this->adcs).parameterUpdated(PARAMID_INTERVAL);
    }

    //! run_handler is a private override in ADCS, so it is driven through the
    //! base class exactly as the rate group does.
    void tick(int count) {
        for (int i = 0; i < count; ++i) {
            static_cast<ADCSComponentBase&>(this->adcs).run_handler(0, 0);
        }
    }

    //! Stage the recorder as the generated loadParameters() leaves it after
    //! /prmDb.dat is read, then call the parametersLoaded() hook exactly as the
    //! framework does. No parameterUpdated call is made.
    void bootWith(U8 saved, Fw::ParamValid validity) {
        this->adcs.collectionIntervalS = saved;
        this->adcs.paramValidity = validity;
        static_cast<ADCSComponentBase&>(this->adcs).parametersLoaded();
    }

    ADCS adcs;
};

TEST_F(AdcsCollectionIntervalTest, IntervalThreeSweepsFourTimesInTwelveTicks) {
    RecordProperty("verifies", "ADCS-1");
    this->applyInterval(3, Fw::ParamValid::VALID);

    this->tick(12);

    EXPECT_EQ(this->adcs.visibleLightReads, 4 * LIGHT_SENSORS);
    ASSERT_FALSE(this->adcs.tlmCollectionIntervalS.empty());
    EXPECT_EQ(this->adcs.tlmCollectionIntervalS.back(), 3);
    EXPECT_TRUE(this->adcs.eventsCollectionIntervalRejected.empty());
}

TEST_F(AdcsCollectionIntervalTest, DefaultBehaviourUnchanged) {
    RecordProperty("verifies", "ADCS-1");
    // No parameter is ever set: the component must read on every tick, exactly
    // as the ungated handler did before this parameter existed.
    this->tick(5);

    EXPECT_EQ(this->adcs.visibleLightReads, 5 * LIGHT_SENSORS);
}

TEST_F(AdcsCollectionIntervalTest, IntervalSixtyIsAccepted) {
    RecordProperty("verifies", "ADCS-1");
    this->applyInterval(60, Fw::ParamValid::VALID);

    EXPECT_TRUE(this->adcs.eventsCollectionIntervalRejected.empty());
    ASSERT_FALSE(this->adcs.tlmCollectionIntervalS.empty());
    EXPECT_EQ(this->adcs.tlmCollectionIntervalS.back(), 60);

    this->tick(12);
    EXPECT_EQ(this->adcs.visibleLightReads, LIGHT_SENSORS);
}

TEST_F(AdcsCollectionIntervalTest, ZeroIsRejectedAndFallsBackToOne) {
    RecordProperty("verifies", "ADCS-2");
    this->applyInterval(0, Fw::ParamValid::VALID);

    ASSERT_EQ(this->adcs.eventsCollectionIntervalRejected.size(), 1u);
    EXPECT_EQ(this->adcs.eventsCollectionIntervalRejected.front(), 0);
    ASSERT_FALSE(this->adcs.tlmCollectionIntervalS.empty());
    EXPECT_EQ(this->adcs.tlmCollectionIntervalS.back(), 1);

    this->tick(5);
    EXPECT_EQ(this->adcs.visibleLightReads, 5 * LIGHT_SENSORS);
}

TEST_F(AdcsCollectionIntervalTest, AboveRangeIsRejectedAndFallsBackToOne) {
    RecordProperty("verifies", "ADCS-2");
    this->applyInterval(61, Fw::ParamValid::VALID);

    ASSERT_EQ(this->adcs.eventsCollectionIntervalRejected.size(), 1u);
    EXPECT_EQ(this->adcs.eventsCollectionIntervalRejected.front(), 61);
    ASSERT_FALSE(this->adcs.tlmCollectionIntervalS.empty());
    EXPECT_EQ(this->adcs.tlmCollectionIntervalS.back(), 1);

    this->tick(5);
    EXPECT_EQ(this->adcs.visibleLightReads, 5 * LIGHT_SENSORS);
}

TEST_F(AdcsCollectionIntervalTest, InvalidParamIsRejectedAndFallsBackToOne) {
    RecordProperty("verifies", "ADCS-2");
    this->applyInterval(30, Fw::ParamValid::INVALID);

    ASSERT_EQ(this->adcs.eventsCollectionIntervalRejected.size(), 1u);
    EXPECT_EQ(this->adcs.eventsCollectionIntervalRejected.front(), 30);
    ASSERT_FALSE(this->adcs.tlmCollectionIntervalS.empty());
    EXPECT_EQ(this->adcs.tlmCollectionIntervalS.back(), 1);

    this->tick(5);
    EXPECT_EQ(this->adcs.visibleLightReads, 5 * LIGHT_SENSORS);
}

TEST_F(AdcsCollectionIntervalTest, UninitParamIsRejectedAndFallsBackToOne) {
    RecordProperty("verifies", "ADCS-2");
    this->applyInterval(30, Fw::ParamValid::UNINIT);

    ASSERT_EQ(this->adcs.eventsCollectionIntervalRejected.size(), 1u);
    ASSERT_FALSE(this->adcs.tlmCollectionIntervalS.empty());
    EXPECT_EQ(this->adcs.tlmCollectionIntervalS.back(), 1);
}

// ----------------------------------------------------------------------
// ADCS-3: a saved interval is applied at boot
// ----------------------------------------------------------------------

TEST_F(AdcsCollectionIntervalTest, SavedIntervalIsEffectiveOnTheFirstTickAfterBoot) {
    RecordProperty("verifies", "ADCS-3");
    // The database holds 3 (as after PRM_SAVE_FILE and a reboot); the framework
    // calls parametersLoaded() and nothing else before the first tick.
    this->bootWith(3, Fw::ParamValid::VALID);

    ASSERT_FALSE(this->adcs.tlmCollectionIntervalS.empty());
    EXPECT_EQ(this->adcs.tlmCollectionIntervalS.front(), 3);
    EXPECT_TRUE(this->adcs.eventsCollectionIntervalRejected.empty());

    this->tick(12);

    EXPECT_EQ(this->adcs.visibleLightReads, 4 * LIGHT_SENSORS);
}

TEST_F(AdcsCollectionIntervalTest, InvalidSavedIntervalAtBootFallsBackToOneWithOneRejection) {
    RecordProperty("verifies", "ADCS-3");
    // Host-stub case: on the target loadParameters() leaves VALID or DEFAULT.
    this->bootWith(30, Fw::ParamValid::INVALID);

    ASSERT_EQ(this->adcs.eventsCollectionIntervalRejected.size(), 1u);
    EXPECT_EQ(this->adcs.eventsCollectionIntervalRejected.front(), 30);
    ASSERT_FALSE(this->adcs.tlmCollectionIntervalS.empty());
    EXPECT_EQ(this->adcs.tlmCollectionIntervalS.front(), 1);

    this->tick(5);
    EXPECT_EQ(this->adcs.visibleLightReads, 5 * LIGHT_SENSORS);
    EXPECT_EQ(this->adcs.eventsCollectionIntervalRejected.size(), 1u);
}

TEST_F(AdcsCollectionIntervalTest, ParameterSetAfterBootStillAppliesOnce) {
    RecordProperty("verifies", "ADCS-3");
    this->bootWith(3, Fw::ParamValid::VALID);

    // A PRM_SET after boot reaches parameterUpdated exactly as today: one
    // write per application, so boot + set is two writes, not three.
    this->applyInterval(6, Fw::ParamValid::VALID);
    ASSERT_EQ(this->adcs.tlmCollectionIntervalS.size(), 2u);
    EXPECT_EQ(this->adcs.tlmCollectionIntervalS[1], 6);
    EXPECT_TRUE(this->adcs.eventsCollectionIntervalRejected.empty());

    this->tick(12);
    // Ticks 1 and 7 run at interval 6.
    EXPECT_EQ(this->adcs.visibleLightReads, 2 * LIGHT_SENSORS);

    // A rejected set after boot emits its one event; the boot refresh adds none.
    this->applyInterval(0, Fw::ParamValid::VALID);
    EXPECT_EQ(this->adcs.eventsCollectionIntervalRejected.size(), 1u);
    EXPECT_EQ(this->adcs.tlmCollectionIntervalS.back(), 1);
}

}  // namespace
