// ======================================================================
// \title  test_RunInterval.cpp
// \brief  Host unit tests for the shared tick decimator Components::RunInterval.
//
// Level: Unit. RunInterval.hpp is header-only and F Prime free, so nothing but
// <cstdint> is linked (test/unit-tests/README.md).
//
// Requirements verified: ThermalManager-1, ADCS-1, PWR-MON-REQ-008 — the
// decimation observable those three requirements share, stated once here on
// the helper that implements it. Their pass criteria read:
//   "Over 12 ticks at interval 3 exactly 4 sweeps occur (ticks 1,4,7,10); at
//    the default interval every tick sweeps"
// The per-component tests assert the same schedule against the real component
// bodies; this file pins the schedule itself.
//
// Oracle (TP-3): the expected schedule is the one written in the pass criteria
// and in the Cycle B design (interval default 1, range 1..60, first tick always
// runs). No expected value is read out of the implementation.
// ======================================================================

#include <gtest/gtest.h>

#include <vector>

#include "PROVESFlightControllerReference/Components/RunInterval/RunInterval.hpp"

namespace {

using Components::RunInterval;

// Range stated by the requirement text ("1..60") and by the parameter comment
// in each component .fpp.
constexpr uint8_t MIN_INTERVAL = 1;
constexpr uint8_t MAX_INTERVAL = 60;

//! Count how many of `ticks` consecutive calls at a fixed interval execute.
int runsOver(RunInterval& interval, uint8_t interval_s, int ticks) {
    int runs = 0;
    for (int i = 0; i < ticks; ++i) {
        if (interval.due(interval_s)) {
            runs++;
        }
    }
    return runs;
}

TEST(RunInterval, DefaultRunsEveryTick) {
    RecordProperty("verifies", "ThermalManager-1,ADCS-1,PWR-MON-REQ-008");
    // "at the default interval every tick sweeps" — the property that makes the
    // gated handlers bit-identical to the ungated ones shipped before Cycle B.
    RunInterval interval;
    for (int i = 0; i < 20; ++i) {
        EXPECT_TRUE(interval.due(MIN_INTERVAL)) << "tick " << i;
    }
}

TEST(RunInterval, FirstTickRuns) {
    // The first tick after boot always executes, whatever the interval, so a
    // long interval never delays the first reading.
    RunInterval interval;
    EXPECT_TRUE(interval.due(60));
}

TEST(RunInterval, IntervalNRunsEveryN) {
    RecordProperty("verifies", "ThermalManager-1,ADCS-1,PWR-MON-REQ-008");
    // "Over 12 ticks at interval 3 exactly 4 sweeps occur (ticks 1,4,7,10)".
    RunInterval interval;
    std::vector<int> executedTicks;
    for (int tick = 1; tick <= 12; ++tick) {
        if (interval.due(3)) {
            executedTicks.push_back(tick);
        }
    }
    EXPECT_EQ(executedTicks, (std::vector<int>{1, 4, 7, 10}));
}

TEST(RunInterval, ZeroTreatedAsOne) {
    // A zero interval must never stall the handler: it runs every tick, which
    // is the same behaviour the effective() fallback produces.
    RunInterval interval;
    EXPECT_EQ(runsOver(interval, 0, 10), 10);
}

TEST(RunInterval, DecreaseTakesEffectNextTick) {
    RunInterval interval;
    EXPECT_TRUE(interval.due(10));   // tick 1 runs
    EXPECT_FALSE(interval.due(10));  // tick 2 waits
    EXPECT_FALSE(interval.due(10));  // tick 3 waits
    // Interval drops to 2 while 2 ticks have already elapsed: the next tick is
    // immediately due rather than waiting out the old gap.
    EXPECT_TRUE(interval.due(2));
}

TEST(RunInterval, IncreaseExtendsGap) {
    RunInterval interval;
    EXPECT_TRUE(interval.due(2));   // tick 1 runs
    EXPECT_FALSE(interval.due(2));  // tick 2 waits
    // Interval rises to 5 before tick 3, which would have run at the old
    // interval; the gap in progress is extended instead.
    EXPECT_FALSE(interval.due(5));  // tick 3
    EXPECT_FALSE(interval.due(5));  // tick 4
    EXPECT_FALSE(interval.due(5));  // tick 5
    EXPECT_TRUE(interval.due(5));   // tick 6: 5 ticks since the last run
}

TEST(RunInterval, EffectiveFallsBackOutOfRange) {
    RecordProperty("verifies", "ThermalManager-2,ADCS-2,PWR-MON-REQ-009");
    // "An interval of 0 or >60 or an INVALID param yields effective 1 s".
    EXPECT_EQ(RunInterval::effective(0, true), MIN_INTERVAL);
    EXPECT_EQ(RunInterval::effective(61, true), MIN_INTERVAL);
    EXPECT_EQ(RunInterval::effective(255, true), MIN_INTERVAL);
    EXPECT_EQ(RunInterval::effective(5, false), MIN_INTERVAL);   // INVALID / UNINIT
    EXPECT_EQ(RunInterval::effective(30, false), MIN_INTERVAL);  // INVALID / UNINIT
    // The endpoints of the accepted range pass through unchanged.
    EXPECT_EQ(RunInterval::effective(MIN_INTERVAL, true), MIN_INTERVAL);
    EXPECT_EQ(RunInterval::effective(MAX_INTERVAL, true), MAX_INTERVAL);
    EXPECT_EQ(RunInterval::effective(30, true), 30);
}

}  // namespace
