// ======================================================================
// \title  test_ADCS_CollectionInterval.cpp
// \brief  Host unit tests for the ADCS COLLECTION_INTERVAL_S parameter.
//
// Level: Unit. Runs on the host against the recorder stub in
// support/PROVESFlightControllerReference/Components/ADCS/; no F Prime or
// Zephyr code is linked (test/unit-tests/README.md).
//
// Requirements verified: ADCS-1, ADCS-2.
//   ADCS-1 pass criteria: "Over 12 ticks at interval 3 exactly 4 sweeps occur
//     (ticks 1,4,7,10); at the default interval every tick sweeps".
//   ADCS-2 pass criteria: "An interval of 0 or >60 or an INVALID param yields
//     effective 1 s, one CollectionIntervalRejected event, and
//     CollectionIntervalS telemetry 1".
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

}  // namespace
