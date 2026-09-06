// ======================================================================
// \title  test_ThermalManager_Thresholds.cpp
// \brief  Host unit tests for ThermalManager temperature-threshold detection.
//
// Level: Unit. Runs on the host against the recorder stub in
// support/PROVESFlightControllerReference/Components/ThermalManager/; no
// F Prime or Zephyr code is linked (test/unit-tests/README.md).
//
// Requirements verified: TM-L2-08, FD-L2-03.
//   TM-L2-08 pass criteria: "Face/battery temperature outside [lower, upper]
//     (defaults -40/60 and 5/60 C) raises TemperatureBelow/AboveThreshold once,
//     re-armed only after 3 C hysteresis".
//   FD-L2-03 pass criteria: "Each detected fault produces a WARNING event
//     within one evaluation" — measured here as: the event is recorded by the
//     same run_handler call that saw the out-of-range reading (zero extra ticks).
//
// Oracle (TP-3): the thresholds and the hysteresis band are read from the
// requirement's pass criteria and from the design source that the criteria
// quote — ThermalManager.fpp:7-16 (parameter defaults) and
// ThermalManager.hpp:27 (DEBOUNCE_ERROR = 3.0 C) — and restated below as
// named constants. No expected value is taken from the logic under test.
// ======================================================================

#include <gtest/gtest.h>

#include "PROVESFlightControllerReference/Components/ThermalManager/ThermalManager.hpp"

namespace {

using Components::ThermalManager;
using Components::ThermalManagerComponentBase;
using SensorType = Components::ThermalManager_TempSensorType;

// Parameter defaults, ThermalManager.fpp:7-16 (also quoted in the TM-L2-08
// pass criteria as "defaults -40/60 and 5/60 C").
constexpr F64 FACE_LOWER_C = -40.0;
constexpr F64 FACE_UPPER_C = 60.0;
constexpr F64 BATT_LOWER_C = 5.0;
constexpr F64 BATT_UPPER_C = 60.0;

// Hysteresis band, ThermalManager.hpp:27 (DEBOUNCE_ERROR). The criterion says
// the event is "re-armed only after 3 C hysteresis".
constexpr F64 HYSTERESIS_C = 3.0;

//! Fixture: a fresh component per test, all sensors reading a comfortable
//! mid-range value so that only the sensor a test moves can raise an event.
class ThermalManagerThresholdTest : public ::testing::Test {
  protected:
    ThermalManagerThresholdTest() : thermal("thermal") {}

    void SetUp() override {
        for (int i = 0; i < 5; ++i) {
            this->thermal.faceTemp[i] = NOMINAL_FACE_C;
            this->thermal.faceTempStatus[i] = Fw::Success::SUCCESS;
        }
        for (int i = 0; i < 4; ++i) {
            this->thermal.battTemp[i] = NOMINAL_BATT_C;
            this->thermal.battTempStatus[i] = Fw::Success::SUCCESS;
        }
        this->thermal.picoTemp = NOMINAL_FACE_C;
    }

    //! One scheduler evaluation: the component reads every sensor once.
    //! run_handler is a private override in ThermalManager, so it is driven
    //! through the base reference exactly as real autocode does.
    void tick() { static_cast<ThermalManagerComponentBase&>(this->thermal).run_handler(0, 0); }

    // Comfortably inside both bands, and more than one hysteresis band away
    // from every threshold, so a nominal sensor never latches or re-arms.
    static constexpr F64 NOMINAL_FACE_C = 25.0;
    static constexpr F64 NOMINAL_BATT_C = 25.0;

    ThermalManager thermal;
};

// ----------------------------------------------------------------------
// Above-threshold detection and hysteresis re-arm
// ----------------------------------------------------------------------

TEST_F(ThermalManagerThresholdTest, AboveUpperEmitsOnceAndRearmsAfterHysteresis) {
    RecordProperty("verifies", "TM-L2-08,FD-L2-03");

    // Face 0 crosses the upper threshold: exactly one event, on this tick.
    thermal.faceTemp[0] = FACE_UPPER_C + 1.0;  // 61.0 C
    tick();
    ASSERT_EQ(thermal.eventsAboveThreshold.size(), 1u)
        << "A face temperature above the upper threshold must raise exactly one "
           "TemperatureAboveThreshold event in the evaluation that observed it";
    EXPECT_EQ(thermal.eventsAboveThreshold[0].sensorType, SensorType::FACE);
    EXPECT_EQ(thermal.eventsAboveThreshold[0].sensorId, 0u);
    EXPECT_DOUBLE_EQ(thermal.eventsAboveThreshold[0].temperature, FACE_UPPER_C + 1.0);
    EXPECT_TRUE(thermal.eventsBelowThreshold.empty());

    // Still above: latched, no repeat event.
    tick();
    EXPECT_EQ(thermal.eventsAboveThreshold.size(), 1u) << "The event must not repeat while the fault persists";

    // Back below the threshold but still inside the hysteresis band
    // (59.0 > 60.0 - 3.0): not re-armed yet.
    thermal.faceTemp[0] = FACE_UPPER_C - 1.0;  // 59.0 C
    tick();
    EXPECT_EQ(thermal.eventsAboveThreshold.size(), 1u);

    // Below the hysteresis band (56.9 < 57.0): re-armed, but crossing back down
    // is not itself a fault.
    thermal.faceTemp[0] = FACE_UPPER_C - HYSTERESIS_C - 0.1;  // 56.9 C
    tick();
    EXPECT_EQ(thermal.eventsAboveThreshold.size(), 1u) << "Re-arming must not itself emit an event";

    // Crossing the threshold again after re-arm raises a second event.
    thermal.faceTemp[0] = FACE_UPPER_C + 1.0;
    tick();
    EXPECT_EQ(thermal.eventsAboveThreshold.size(), 2u)
        << "After the reading has fallen a full hysteresis band below the "
           "threshold, a fresh excursion must raise a new event";
    EXPECT_TRUE(thermal.eventsBelowThreshold.empty());
}

// ----------------------------------------------------------------------
// Below-threshold detection and hysteresis re-arm (battery band)
// ----------------------------------------------------------------------

TEST_F(ThermalManagerThresholdTest, BelowLowerEmitsOnceAndRearmsAfterHysteresis) {
    RecordProperty("verifies", "TM-L2-08,FD-L2-03");

    // Battery cell 2 drops below the lower threshold.
    thermal.battTemp[2] = BATT_LOWER_C - 0.1;  // 4.9 C
    tick();
    ASSERT_EQ(thermal.eventsBelowThreshold.size(), 1u)
        << "A battery temperature below the lower threshold must raise exactly "
           "one TemperatureBelowThreshold event in that evaluation";
    EXPECT_EQ(thermal.eventsBelowThreshold[0].sensorType, SensorType::BATTERY);
    EXPECT_EQ(thermal.eventsBelowThreshold[0].sensorId, 2u);
    EXPECT_DOUBLE_EQ(thermal.eventsBelowThreshold[0].temperature, BATT_LOWER_C - 0.1);
    EXPECT_TRUE(thermal.eventsAboveThreshold.empty());

    // Recovered above the threshold but still inside the hysteresis band
    // (7.9 < 5.0 + 3.0): not re-armed.
    thermal.battTemp[2] = BATT_LOWER_C + HYSTERESIS_C - 0.1;  // 7.9 C
    tick();
    EXPECT_EQ(thermal.eventsBelowThreshold.size(), 1u);

    // Above the hysteresis band (8.1 > 8.0): re-armed, no event for recovery.
    thermal.battTemp[2] = BATT_LOWER_C + HYSTERESIS_C + 0.1;  // 8.1 C
    tick();
    EXPECT_EQ(thermal.eventsBelowThreshold.size(), 1u) << "Re-arming must not itself emit an event";

    // A fresh excursion raises a second event.
    thermal.battTemp[2] = BATT_LOWER_C - 0.1;
    tick();
    EXPECT_EQ(thermal.eventsBelowThreshold.size(), 2u);
    EXPECT_TRUE(thermal.eventsAboveThreshold.empty());
}

// ----------------------------------------------------------------------
// Negative path: a failed sensor read must not be treated as a measurement
// ----------------------------------------------------------------------

TEST_F(ThermalManagerThresholdTest, FailedReadNeverEvaluatesThreshold) {
    RecordProperty("verifies", "TM-L2-08,FD-L2-03");

    // A sensor that failed to read reports a wildly out-of-range value; the
    // reading is not a measurement and must not raise a threshold fault.
    thermal.faceTemp[1] = 200.0;
    thermal.faceTempStatus[1] = Fw::Success::FAILURE;
    thermal.battTemp[0] = -200.0;
    thermal.battTempStatus[0] = Fw::Success::FAILURE;

    tick();
    tick();

    EXPECT_TRUE(thermal.eventsAboveThreshold.empty())
        << "A failed sensor read must not raise TemperatureAboveThreshold";
    EXPECT_TRUE(thermal.eventsBelowThreshold.empty())
        << "A failed sensor read must not raise TemperatureBelowThreshold";
    // The port was still polled — the fault is in the read, not in the schedule.
    EXPECT_GT(thermal.faceTempReads, 0u);
}

// ----------------------------------------------------------------------
// Boundary: the threshold value itself is inside the allowed range
// ----------------------------------------------------------------------

TEST_F(ThermalManagerThresholdTest, BoundaryValueIsNotAFault) {
    RecordProperty("verifies", "TM-L2-08,FD-L2-03");

    // TM-L2-08 says "outside [lower, upper]" — the endpoint is inside.
    thermal.faceTemp[3] = FACE_UPPER_C;  // exactly 60.0 C
    tick();
    EXPECT_TRUE(thermal.eventsAboveThreshold.empty())
        << "A reading exactly at the upper threshold is inside the range and "
           "must not raise a fault";

    thermal.battTemp[1] = BATT_LOWER_C;  // exactly 5.0 C
    tick();
    EXPECT_TRUE(thermal.eventsBelowThreshold.empty())
        << "A reading exactly at the lower threshold is inside the range and "
           "must not raise a fault";

    // Just outside is a fault, which proves the boundary check above is not
    // passing vacuously.
    thermal.faceTemp[3] = FACE_UPPER_C + 0.01;
    tick();
    ASSERT_EQ(thermal.eventsAboveThreshold.size(), 1u);
    EXPECT_EQ(thermal.eventsAboveThreshold[0].sensorId, 3u);
}

}  // namespace
