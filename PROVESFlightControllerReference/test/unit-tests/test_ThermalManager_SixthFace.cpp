// ======================================================================
// \title  test_ThermalManager_SixthFace.cpp
// \brief  Host unit tests for the sixth face temperature sensor
//         (faceTempGet[5] -> tmp112Face6Manager) in the ThermalManager sweep.
//
// Level: Unit. Runs on the host against the recorder stub in
// support/PROVESFlightControllerReference/Components/ThermalManager/; no
// F Prime or Zephyr code is linked (test/unit-tests/README.md).
//
// Requirement verified: ThermalManager-4.
//   ThermalManager-4 pass criteria: "Host stub with six faceTempGet ports: each
//     executed collection tick calls every port 0-5 exactly once (3 ticks at the
//     default interval: 3 calls each; 12 ticks at interval 3: 4 calls each); a
//     face-5 reading of 61 C raises one TemperatureAboveThreshold(FACE, 5, 61.0)
//     and of -41 C one TemperatureBelowThreshold(FACE, 5, -41.0); existing
//     ThermalManager host tests stay green".
//
// Oracle (TP-3): six ports is Cycle L 01-normative.md R2.1
// (numFaceTempSensors = 6); the face thresholds -40 / 60 C are the
// ThermalManager.fpp parameter defaults, as in test_ThermalManager_Thresholds.
// The schedule at interval 3 (ticks 1,4,7,10) is the ThermalManager-1 criterion.
// ======================================================================

#include <gtest/gtest.h>

#include "PROVESFlightControllerReference/Components/ThermalManager/ThermalManager.hpp"

namespace {

using Components::ThermalManager;
using Components::ThermalManagerComponentBase;
using SensorType = Components::ThermalManager_TempSensorType;

// R2.1: six face sensors; the sixth is index 5.
constexpr int FACE_SENSORS = 6;
constexpr U32 SIXTH_FACE = 5;

// ThermalManager.fpp parameter defaults.
constexpr F64 FACE_LOWER_C = -40.0;
constexpr F64 FACE_UPPER_C = 60.0;

// Parameter id of COLLECTION_INTERVAL_S, ThermalManager.fpp "id 4".
constexpr FwPrmIdType PARAMID_INTERVAL = 4;

class ThermalSixthFaceTest : public ::testing::Test {
  protected:
    ThermalSixthFaceTest() : thermal("thermal") {}

    void SetUp() override {
        for (int i = 0; i < FACE_SENSORS; ++i) {
            this->thermal.faceTemp[i] = NOMINAL_C;
            this->thermal.faceTempStatus[i] = Fw::Success::SUCCESS;
        }
        for (int i = 0; i < 4; ++i) {
            this->thermal.battTemp[i] = NOMINAL_C;
        }
        this->thermal.picoTemp = NOMINAL_C;
    }

    void tick(int count) {
        for (int i = 0; i < count; ++i) {
            static_cast<ThermalManagerComponentBase&>(this->thermal).run_handler(0, 0);
        }
    }

    void applyInterval(U8 requested) {
        this->thermal.collectionIntervalS = requested;
        this->thermal.paramValidity = Fw::ParamValid::VALID;
        static_cast<ThermalManagerComponentBase&>(this->thermal).parameterUpdated(PARAMID_INTERVAL);
    }

    // Inside both face and battery bands by more than the 3 C hysteresis.
    static constexpr F64 NOMINAL_C = 25.0;

    ThermalManager thermal;
};

TEST_F(ThermalSixthFaceTest, EverySweepCallsAllSixFacePortsOnce) {
    RecordProperty("verifies", "ThermalManager-4");
    ASSERT_EQ(ThermalManagerComponentBase::getNum_faceTempGet_OutputPorts(), FACE_SENSORS)
        << "The stub must mirror numFaceTempSensors = 6 (R2.1)";

    this->tick(3);

    for (int port = 0; port < FACE_SENSORS; ++port) {
        EXPECT_EQ(this->thermal.faceTempReadsByPort[port], 3u) << "faceTempGet[" << port << "] after 3 ticks";
    }
    EXPECT_EQ(this->thermal.faceTempReads, 3u * FACE_SENSORS);
}

TEST_F(ThermalSixthFaceTest, SixthFaceFollowsTheCollectionInterval) {
    RecordProperty("verifies", "ThermalManager-4");
    this->applyInterval(3);

    this->tick(12);  // sweeps at ticks 1, 4, 7, 10

    for (int port = 0; port < FACE_SENSORS; ++port) {
        EXPECT_EQ(this->thermal.faceTempReadsByPort[port], 4u) << "faceTempGet[" << port << "] at interval 3";
    }
}

TEST_F(ThermalSixthFaceTest, SixthFaceAboveUpperReportsSensorIdFive) {
    RecordProperty("verifies", "ThermalManager-4");
    this->thermal.faceTemp[SIXTH_FACE] = FACE_UPPER_C + 1.0;  // 61.0 C

    this->tick(1);

    ASSERT_EQ(this->thermal.eventsAboveThreshold.size(), 1u);
    EXPECT_EQ(this->thermal.eventsAboveThreshold[0].sensorType, SensorType::FACE);
    EXPECT_EQ(this->thermal.eventsAboveThreshold[0].sensorId, SIXTH_FACE);
    EXPECT_DOUBLE_EQ(this->thermal.eventsAboveThreshold[0].temperature, FACE_UPPER_C + 1.0);
    EXPECT_TRUE(this->thermal.eventsBelowThreshold.empty());
}

TEST_F(ThermalSixthFaceTest, SixthFaceBelowLowerReportsSensorIdFive) {
    RecordProperty("verifies", "ThermalManager-4");
    this->thermal.faceTemp[SIXTH_FACE] = FACE_LOWER_C - 1.0;  // -41.0 C

    this->tick(1);

    ASSERT_EQ(this->thermal.eventsBelowThreshold.size(), 1u);
    EXPECT_EQ(this->thermal.eventsBelowThreshold[0].sensorType, SensorType::FACE);
    EXPECT_EQ(this->thermal.eventsBelowThreshold[0].sensorId, SIXTH_FACE);
    EXPECT_DOUBLE_EQ(this->thermal.eventsBelowThreshold[0].temperature, FACE_LOWER_C - 1.0);
    EXPECT_TRUE(this->thermal.eventsAboveThreshold.empty());
}

}  // namespace
