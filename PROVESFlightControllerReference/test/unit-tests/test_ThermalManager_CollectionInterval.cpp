// ======================================================================
// \title  test_ThermalManager_CollectionInterval.cpp
// \brief  Host unit tests for the ThermalManager COLLECTION_INTERVAL_S parameter.
//
// Level: Unit. Runs on the host against the recorder stub in
// support/PROVESFlightControllerReference/Components/ThermalManager/; no
// F Prime or Zephyr code is linked (test/unit-tests/README.md).
//
// Requirements verified: ThermalManager-1, ThermalManager-2, ThermalManager-3.
//   ThermalManager-1 pass criteria: "Over 12 ticks at interval 3 exactly 4
//     sweeps occur (ticks 1,4,7,10); at the default interval every tick sweeps".
//   ThermalManager-2 pass criteria: "An interval of 0 or >60 or an INVALID
//     param yields effective 1 s, one CollectionIntervalRejected event, and
//     CollectionIntervalS telemetry 1".
//   ThermalManager-3 pass criteria: "With COLLECTION_INTERVAL_S = 3 VALID in the
//     stub and no parameterUpdated call, after parametersLoaded() 12 ticks
//     sweep exactly 4 times (ticks 1,4,7,10) and the first CollectionIntervalS
//     write is 3; with paramValidity INVALID every tick sweeps, one
//     CollectionIntervalRejected, first write 1; a parameterUpdated after boot
//     applies as today with no duplicate event".
//
// Oracle (TP-3): the sweep shape (5 face + 4 battery + 1 pico reads per sweep)
// comes from ThermalManager.fpp:27-39 port counts; the schedule and the
// fallback rule come from the two pass criteria quoted above. No expected value
// is read out of the logic under test.
// ======================================================================

#include <gtest/gtest.h>

#include "PROVESFlightControllerReference/Components/ThermalManager/ThermalManager.hpp"

namespace {

using Components::ThermalManager;
using Components::ThermalManagerComponentBase;

// Port counts, ThermalManager.fpp:27-39.
constexpr U32 FACE_SENSORS = 5;
constexpr U32 BATT_SENSORS = 4;
constexpr U32 PICO_SENSORS = 1;

// Parameter id of COLLECTION_INTERVAL_S, ThermalManager.fpp "id 4".
constexpr FwPrmIdType PARAMID_INTERVAL = 4;

//! Fixture: a fresh component with every sensor reading a comfortable
//! mid-range value, so no threshold event perturbs the read counts.
class ThermalCollectionIntervalTest : public ::testing::Test {
  protected:
    ThermalCollectionIntervalTest() : thermal("thermal") {}

    void SetUp() override {
        for (int i = 0; i < 5; ++i) {
            this->thermal.faceTemp[i] = 20.0;
        }
        for (int i = 0; i < 4; ++i) {
            this->thermal.battTemp[i] = 20.0;
        }
        this->thermal.picoTemp = 20.0;
    }

    //! Drive the parameter-update path the generated code calls after a store.
    void applyInterval(U8 requested, Fw::ParamValid validity) {
        this->thermal.collectionIntervalS = requested;
        this->thermal.paramValidity = validity;
        static_cast<ThermalManagerComponentBase&>(this->thermal).parameterUpdated(PARAMID_INTERVAL);
    }

    void tick(int count) {
        for (int i = 0; i < count; ++i) {
            static_cast<ThermalManagerComponentBase&>(this->thermal).run_handler(0, 0);
        }
    }

    //! Stage the recorder as the generated loadParameters() leaves it after
    //! /prmDb.dat is read, then call the parametersLoaded() hook exactly as the
    //! framework does. No parameterUpdated call is made.
    void bootWith(U8 saved, Fw::ParamValid validity) {
        this->thermal.collectionIntervalS = saved;
        this->thermal.paramValidity = validity;
        static_cast<ThermalManagerComponentBase&>(this->thermal).parametersLoaded();
    }

    ThermalManager thermal;
};

TEST_F(ThermalCollectionIntervalTest, IntervalThreeSweepsFourTimesInTwelveTicks) {
    RecordProperty("verifies", "ThermalManager-1");
    this->applyInterval(3, Fw::ParamValid::VALID);

    this->tick(12);

    EXPECT_EQ(this->thermal.faceTempReads, 4 * FACE_SENSORS);
    EXPECT_EQ(this->thermal.battTempReads, 4 * BATT_SENSORS);
    EXPECT_EQ(this->thermal.picoTempReads, 4 * PICO_SENSORS);
    ASSERT_FALSE(this->thermal.tlmCollectionIntervalS.empty());
    EXPECT_EQ(this->thermal.tlmCollectionIntervalS.back(), 3);
    EXPECT_TRUE(this->thermal.eventsCollectionIntervalRejected.empty());
}

TEST_F(ThermalCollectionIntervalTest, DefaultBehaviourUnchanged) {
    RecordProperty("verifies", "ThermalManager-1");
    // No parameter is ever set: the component must sweep on every tick, exactly
    // as the ungated handler did before this parameter existed.
    this->tick(5);

    EXPECT_EQ(this->thermal.faceTempReads, 5 * FACE_SENSORS);
    EXPECT_EQ(this->thermal.battTempReads, 5 * BATT_SENSORS);
    EXPECT_EQ(this->thermal.picoTempReads, 5 * PICO_SENSORS);
}

TEST_F(ThermalCollectionIntervalTest, IntervalSixtyIsAccepted) {
    RecordProperty("verifies", "ThermalManager-1");
    // The upper end of the accepted range is not a rejection: 60 s is stored.
    this->applyInterval(60, Fw::ParamValid::VALID);

    EXPECT_TRUE(this->thermal.eventsCollectionIntervalRejected.empty());
    ASSERT_FALSE(this->thermal.tlmCollectionIntervalS.empty());
    EXPECT_EQ(this->thermal.tlmCollectionIntervalS.back(), 60);

    this->tick(12);
    // Only the first tick sweeps within a 60 s gap.
    EXPECT_EQ(this->thermal.faceTempReads, FACE_SENSORS);
}

TEST_F(ThermalCollectionIntervalTest, ZeroIsRejectedAndFallsBackToOne) {
    RecordProperty("verifies", "ThermalManager-2");
    this->applyInterval(0, Fw::ParamValid::VALID);

    ASSERT_EQ(this->thermal.eventsCollectionIntervalRejected.size(), 1u);
    EXPECT_EQ(this->thermal.eventsCollectionIntervalRejected.front(), 0);
    ASSERT_FALSE(this->thermal.tlmCollectionIntervalS.empty());
    EXPECT_EQ(this->thermal.tlmCollectionIntervalS.back(), 1);

    this->tick(5);
    EXPECT_EQ(this->thermal.faceTempReads, 5 * FACE_SENSORS);
}

TEST_F(ThermalCollectionIntervalTest, AboveRangeIsRejectedAndFallsBackToOne) {
    RecordProperty("verifies", "ThermalManager-2");
    this->applyInterval(61, Fw::ParamValid::VALID);

    ASSERT_EQ(this->thermal.eventsCollectionIntervalRejected.size(), 1u);
    EXPECT_EQ(this->thermal.eventsCollectionIntervalRejected.front(), 61);
    ASSERT_FALSE(this->thermal.tlmCollectionIntervalS.empty());
    EXPECT_EQ(this->thermal.tlmCollectionIntervalS.back(), 1);

    this->tick(5);
    EXPECT_EQ(this->thermal.faceTempReads, 5 * FACE_SENSORS);
}

TEST_F(ThermalCollectionIntervalTest, InvalidParamIsRejectedAndFallsBackToOne) {
    RecordProperty("verifies", "ThermalManager-2");
    // An in-range value that the parameter database reports as INVALID must not
    // be trusted; the safe 1 s default stays in force and the rejection is told.
    this->applyInterval(30, Fw::ParamValid::INVALID);

    ASSERT_EQ(this->thermal.eventsCollectionIntervalRejected.size(), 1u);
    EXPECT_EQ(this->thermal.eventsCollectionIntervalRejected.front(), 30);
    ASSERT_FALSE(this->thermal.tlmCollectionIntervalS.empty());
    EXPECT_EQ(this->thermal.tlmCollectionIntervalS.back(), 1);

    this->tick(5);
    EXPECT_EQ(this->thermal.faceTempReads, 5 * FACE_SENSORS);
}

TEST_F(ThermalCollectionIntervalTest, UninitParamIsRejectedAndFallsBackToOne) {
    RecordProperty("verifies", "ThermalManager-2");
    this->applyInterval(30, Fw::ParamValid::UNINIT);

    ASSERT_EQ(this->thermal.eventsCollectionIntervalRejected.size(), 1u);
    ASSERT_FALSE(this->thermal.tlmCollectionIntervalS.empty());
    EXPECT_EQ(this->thermal.tlmCollectionIntervalS.back(), 1);
}

// ----------------------------------------------------------------------
// ThermalManager-3: a saved interval is applied at boot
// ----------------------------------------------------------------------

TEST_F(ThermalCollectionIntervalTest, SavedIntervalIsEffectiveOnTheFirstTickAfterBoot) {
    RecordProperty("verifies", "ThermalManager-3");
    // The database holds 3 (as after PRM_SAVE_FILE and a reboot); the framework
    // calls parametersLoaded() and nothing else before the first tick.
    this->bootWith(3, Fw::ParamValid::VALID);

    ASSERT_FALSE(this->thermal.tlmCollectionIntervalS.empty());
    EXPECT_EQ(this->thermal.tlmCollectionIntervalS.front(), 3);
    EXPECT_TRUE(this->thermal.eventsCollectionIntervalRejected.empty());

    this->tick(12);

    EXPECT_EQ(this->thermal.faceTempReads, 4 * FACE_SENSORS);
    EXPECT_EQ(this->thermal.battTempReads, 4 * BATT_SENSORS);
    EXPECT_EQ(this->thermal.picoTempReads, 4 * PICO_SENSORS);
}

TEST_F(ThermalCollectionIntervalTest, InvalidSavedIntervalAtBootFallsBackToOneWithOneRejection) {
    RecordProperty("verifies", "ThermalManager-3");
    // Host-stub case: on the target loadParameters() leaves VALID or DEFAULT.
    this->bootWith(30, Fw::ParamValid::INVALID);

    ASSERT_EQ(this->thermal.eventsCollectionIntervalRejected.size(), 1u);
    EXPECT_EQ(this->thermal.eventsCollectionIntervalRejected.front(), 30);
    ASSERT_FALSE(this->thermal.tlmCollectionIntervalS.empty());
    EXPECT_EQ(this->thermal.tlmCollectionIntervalS.front(), 1);

    this->tick(5);
    EXPECT_EQ(this->thermal.faceTempReads, 5 * FACE_SENSORS);
    EXPECT_EQ(this->thermal.eventsCollectionIntervalRejected.size(), 1u);
}

TEST_F(ThermalCollectionIntervalTest, ParameterSetAfterBootStillAppliesOnce) {
    RecordProperty("verifies", "ThermalManager-3");
    this->bootWith(3, Fw::ParamValid::VALID);

    // A PRM_SET after boot reaches parameterUpdated exactly as today: one
    // write per application, so boot + set is two writes, not three.
    this->applyInterval(6, Fw::ParamValid::VALID);
    ASSERT_EQ(this->thermal.tlmCollectionIntervalS.size(), 2u);
    EXPECT_EQ(this->thermal.tlmCollectionIntervalS[1], 6);
    EXPECT_TRUE(this->thermal.eventsCollectionIntervalRejected.empty());

    this->tick(12);
    // Ticks 1 and 7 run at interval 6.
    EXPECT_EQ(this->thermal.faceTempReads, 2 * FACE_SENSORS);

    // A rejected set after boot emits its one event; the boot refresh adds none.
    this->applyInterval(0, Fw::ParamValid::VALID);
    EXPECT_EQ(this->thermal.eventsCollectionIntervalRejected.size(), 1u);
    EXPECT_EQ(this->thermal.tlmCollectionIntervalS.back(), 1);
}

}  // namespace
