/**
 * \file OrbitPropagatorTestMain.cpp
 * 
 * \brief  GoogleTest entry point for the OrbitPropagator component tests
 */

#include "OrbitPropagatorTester.hpp"

using Gnc::Environment::OrbitPropagatorTester;

TEST(OrbitPropagator, ORB0001_TimeBaseRejected) {
    OrbitPropagatorTester tester;
    tester.testTimeBaseRejected();
}

TEST(OrbitPropagator, ORB0002_AcceptedTimeBases) {
    OrbitPropagatorTester tester;
    tester.testAcceptedTimeBases();
}

TEST(OrbitPropagator, ORB0003_EmitsOnEveryPath) {
    OrbitPropagatorTester tester;
    tester.testEmitsOnEveryPath();
}

TEST(OrbitPropagator, ORB0003_NoPositionTelemetryWhenInvalid) {
    OrbitPropagatorTester tester;
    tester.testNoPositionTelemetryWhenInvalid();
}

TEST(OrbitPropagator, ORB0004_NoTleStillPublishesTime) {
    OrbitPropagatorTester tester;
    tester.testNoTleStillPublishesTime();
}

TEST(OrbitPropagator, ORB0006_BadTleRejected) {
    OrbitPropagatorTester tester;
    tester.testBadTleRejected();
}

TEST(OrbitPropagator, ORB0007_ClearTle) {
    OrbitPropagatorTester tester;
    tester.testClearTle();
}

TEST(OrbitPropagator, ORB0010_GmstIsPublishedAndAdvances) {
    OrbitPropagatorTester tester;
    tester.testGmstIsPublishedAndAdvances();
}

TEST(OrbitPropagator, ORB0012_ParameterFallback) {
    OrbitPropagatorTester tester;
    tester.testParameterFallback();
}

TEST(OrbitPropagator, ORB0013_UtcOffsetApplied) {
    OrbitPropagatorTester tester;
    tester.testUtcOffsetApplied();
}

TEST(OrbitPropagator, ORB0016_BroadcastToAllPorts) {
    OrbitPropagatorTester tester;
    tester.testBroadcastToAllPorts();
}

TEST(OrbitPropagator, ORB0017_CycleBudgetIsReported) {
    OrbitPropagatorTester tester;
    tester.testCycleBudgetIsReported();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
