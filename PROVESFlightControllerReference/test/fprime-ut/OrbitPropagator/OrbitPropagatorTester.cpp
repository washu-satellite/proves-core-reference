/**
 * \file  OrbitPropagatorTester.cpp
 * \brief  Component unit tests for OrbitPropagator
 */

#include "OrbitPropagatorTester.hpp"

#include <cmath>

namespace Gnc {
namespace Environment {

const char* const OrbitPropagatorTester::TEST_INSTANCE_NAME = "OrbitPropagator";

namespace {

/*
 * A malformed TLE for tests that need it: ORB0006
 */
const char* const BAD_LINE_1 =
    "1 XXXXXU 98067A   ZZZZZ.ZZZZZZZZ  .00016717  00000-0  10270-3 0  9999";
const char* const BAD_LINE_2 =
    "2 XXXXX  QQ.QQQQ QQQ.QQQQ QQQQQQQ QQQ.QQQQ QQQ.QQQQ QQ.QQQQQQQQQQQQQQ";

//! 2026-07-12T00:00:00Z as POSIX seconds.
const U32 EPOCH_SECONDS = 1784332800u;

//! Seconds in a day, for the sidereal rate check.
const U32 ONE_DAY_SECONDS = 86400u;

}  // namespace

/*
 * ============================================================================
 * Construction
 * ============================================================================
 */

OrbitPropagatorTester::OrbitPropagatorTester()
    : OrbitPropagatorGTestBase(TEST_INSTANCE_NAME, MAX_HISTORY_SIZE),
      component(TEST_INSTANCE_NAME) {
    this->initComponents();
    this->connectPorts();
}

OrbitPropagatorTester::~OrbitPropagatorTester() {}

/*
 * ============================================================================
 * Helpers
 * ============================================================================
 */

void OrbitPropagatorTester::from_orbitOut_handler(FwIndexType portNum,
                                                  Gnc::OrbitState& orbitState) {
    if (this->m_orbitOutCount < FW_NUM_ARRAY_ELEMENTS(this->m_orbitOutPorts)) {
        this->m_orbitOutPorts[this->m_orbitOutCount] = portNum;
        this->m_orbitOutCount++;
    }
    this->pushFromPortEntry_orbitOut(orbitState);
}

void OrbitPropagatorTester::setClock(U32 seconds, U32 useconds, TimeBase::T base) {
    this->m_seconds = seconds;
    this->m_useconds = useconds;
    this->m_timeBase = base;
    this->setTestTime(Fw::Time(base, seconds, useconds));
}

void OrbitPropagatorTester::advanceClock(U32 seconds) {
    this->setClock(this->m_seconds + seconds, this->m_useconds, this->m_timeBase);
}

void OrbitPropagatorTester::tick() {
    this->invoke_to_run(0, 0);
    this->dispatchCurrentMessages(this->component);
}

const Gnc::OrbitState& OrbitPropagatorTester::lastOrbitState() const {
    const U32 n = this->fromPortHistory_orbitOut->size();
    FW_ASSERT(n > 0);
    return this->fromPortHistory_orbitOut->at(n - 1).orbitState;
}


/*
 * ============================================================================
 * ORB0001 / ORB0002  Time base gate 
 * ============================================================================
 */

 // ORB0001
void OrbitPropagatorTester::testTimeBaseRejected() {
    /*
     * The gate exists because uptime and wall clock are both valid
     * Fw::Time values and only one of them can drive an ephemeris. A
     * one-minute clock error is about 450 km of along-track position.
     */
    const TimeBase::T rejected[] = {TimeBase::TB_NONE, TimeBase::TB_PROC_TIME,
                                    TimeBase::TB_DONT_CARE};

    for (TimeBase::T base : rejected) {
        this->clearHistory();
        this->m_orbitOutCount = 0;

        this->setClock(EPOCH_SECONDS, 0, base);
        this->tick();

        ASSERT_EVENTS_TimeMissing_SIZE(1);

        // Still emits, so a consumer can tell "no time" from "nothing
        // arrived at all".
        ASSERT_from_orbitOut_SIZE(static_cast<U32>(OrbitPropagator::getNum_orbitOut_OutputPorts()));

        const Gnc::OrbitState& s = this->lastOrbitState();
        ASSERT_EQ(s.get_validity(), Gnc::OrbitValidity::NO_TIME);
        ASSERT_FALSE(s.get_timeUsable());
        ASSERT_FALSE(s.get_positionUsable());

        ASSERT_TLM_Validity_SIZE(1);
        ASSERT_TLM_Validity(0, Gnc::OrbitValidity::NO_TIME);

        // GMST is not meaningful without a clock, so it is not written.
        ASSERT_TLM_GmstDeg_SIZE(0);
    }
}

// ORB0002
void OrbitPropagatorTester::testAcceptedTimeBases() {
    /*
     * TB_SC_TIME is accepted alongside TB_WORKSTATION_TIME so that
     * correcting RtcManager to use the semantically right base for
     * flight hardware does not silently suspend navigation.
     */
    const TimeBase::T accepted[] = {TimeBase::TB_WORKSTATION_TIME, TimeBase::TB_SC_TIME};

    for (TimeBase::T base : accepted) {
        this->clearHistory();
        this->m_orbitOutCount = 0;

        this->setClock(EPOCH_SECONDS, 0, base);
        this->tick();

        ASSERT_EVENTS_TimeMissing_SIZE(0);

        // No TLE is loaded, so this reaches the NO_TLE path, not NO_TIME.
        ASSERT_EVENTS_TleMissing_SIZE(1);

        const Gnc::OrbitState& s = this->lastOrbitState();
        ASSERT_EQ(s.get_validity(), Gnc::OrbitValidity::NO_TLE);
        ASSERT_TRUE(s.get_timeUsable());
    }
}

/*
 * ============================================================================
 * ORB0003 / ORB0004  Degradation  
 * ============================================================================
 */

void OrbitPropagatorTester::testNoTleStillPublishesTime() {
    this->clearHistory();
    this->setClock(EPOCH_SECONDS, 0, TimeBase::TB_WORKSTATION_TIME);
    this->tick();

    ASSERT_EVENTS_TleMissing_SIZE(1);
    ASSERT_from_orbitOut_SIZE(static_cast<U32>(OrbitPropagator::getNum_orbitOut_OutputPorts()));

    const Gnc::OrbitState& s = this->lastOrbitState();
    ASSERT_EQ(s.get_validity(), Gnc::OrbitValidity::NO_TLE);
    ASSERT_TRUE(s.get_timeUsable());
    ASSERT_FALSE(s.get_positionUsable());

    // Every time product SolarEphemeris needs is present and sane.
    ASSERT_GT(s.get_jdUt1(), 2451545.0);
    ASSERT_GT(s.get_jdTt(), s.get_jdUt1());
    ASSERT_GE(s.get_gmstRad(), 0.0);
    ASSERT_LT(s.get_gmstRad(), 2.0 * 3.14159265358979323846);

    // The stamp is carried so downstream can tag its own outputs with
    // the observation epoch without reading a clock.
    ASSERT_EQ(s.get_stamp().getSeconds(), EPOCH_SECONDS);
    ASSERT_EQ(s.get_stamp().getTimeBase(), TimeBase::TB_WORKSTATION_TIME);

    // GMST telemetry is written even without a TLE, because it is valid.
    ASSERT_TLM_GmstDeg_SIZE(1);
}

void OrbitPropagatorTester::testNoPositionTelemetryWhenInvalid() {
    /*
     * The ground must never see a position channel updating while
     * Validity reports there is no position. publish() runs only on the
     * fully valid path; the early exits write Validity directly.
     */
    this->clearHistory();
    this->setClock(EPOCH_SECONDS, 0, TimeBase::TB_WORKSTATION_TIME);
    this->tick();

    ASSERT_TLM_PosTemeKm_SIZE(0);
    ASSERT_TLM_VelTemeKmS_SIZE(0);
    ASSERT_TLM_LatDeg_SIZE(0);
    ASSERT_TLM_LonDeg_SIZE(0);
    ASSERT_TLM_AltKm_SIZE(0);
    ASSERT_TLM_TleAgeDays_SIZE(0);

    ASSERT_TLM_Validity_SIZE(1);
}

void OrbitPropagatorTester::testEmitsOnEveryPath() {
    /*
     * Ten ticks split between the NO_TIME and NO_TLE paths must produce
     * ten broadcasts per connected port. Going silent on a failure is
     * indistinguishable downstream from a dead producer, which is the
     * whole reason OrbitState carries a validity enum.
     */
    this->clearHistory();
    this->m_orbitOutCount = 0;

    const FwIndexType ports = OrbitPropagator::getNum_orbitOut_OutputPorts();

    for (U32 i = 0; i < 5; ++i) {
        this->setClock(EPOCH_SECONDS + i, 0, TimeBase::TB_NONE);
        this->tick();
    }
    for (U32 i = 0; i < 5; ++i) {
        this->setClock(EPOCH_SECONDS + 100 + i, 0, TimeBase::TB_WORKSTATION_TIME);
        this->tick();
    }

    // NO_TIME emits on port 0 only (emit() returns early before the
    // loop is reached on that path is NOT the case: it broadcasts), so
    // expect one payload per port per tick on the TB_WORKSTATION_TIME
    // ticks and likewise for NO_TIME.
    ASSERT_from_orbitOut_SIZE(static_cast<U32>(10 * ports));

    // TimeMissing is throttled at 3 in the FPP, so five NO_TIME ticks
    // produce three events. The throttle counter is component state and
    // is NOT reset by clearHistory, so this is the total for the run.
    ASSERT_EVENTS_TimeMissing_SIZE(3);
}

/*
 * ============================================================================
 * ORB0006 / ORB0007  TLE commands  
 * ============================================================================
 */

void OrbitPropagatorTester::testBadTleRejected() {
    this->clearHistory();
    this->setClock(EPOCH_SECONDS, 0, TimeBase::TB_WORKSTATION_TIME);

    const Fw::String line1(BAD_LINE_1);
    const Fw::String line2(BAD_LINE_2);

    this->sendCmd_LOAD_TLE(0, 0, line1, line2);
    this->dispatchCurrentMessages(this->component);

    ASSERT_EVENTS_TleRejected_SIZE(1);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, OrbitPropagator::OPCODE_LOAD_TLE, 0,
                        Fw::CmdResponse::VALIDATION_ERROR);

    // TleAccepted must NOT have fired.
    ASSERT_EVENTS_TleAccepted_SIZE(0);

    // And nothing was installed: the next tick is still NO_TLE.
    this->clearHistory();
    this->tick();
    ASSERT_EQ(this->lastOrbitState().get_validity(), Gnc::OrbitValidity::NO_TLE);
}

void OrbitPropagatorTester::testClearTle() {
    this->clearHistory();
    this->setClock(EPOCH_SECONDS, 0, TimeBase::TB_WORKSTATION_TIME);

    this->sendCmd_CLEAR_TLE(0, 0);
    this->dispatchCurrentMessages(this->component);

    ASSERT_EVENTS_TleCleared_SIZE(1);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, OrbitPropagator::OPCODE_CLEAR_TLE, 0, Fw::CmdResponse::OK);

    // Time output continues after the TLE is discarded.
    this->clearHistory();
    this->tick();

    const Gnc::OrbitState& s = this->lastOrbitState();
    ASSERT_EQ(s.get_validity(), Gnc::OrbitValidity::NO_TLE);
    ASSERT_TRUE(s.get_timeUsable());
    ASSERT_FALSE(s.get_positionUsable());
}

/*
 * ============================================================================
 * ORB0012 / ORB0013  Parameters   
 * ============================================================================
 */

void OrbitPropagatorTester::testParameterFallback() {
    /*
     * This deployment uses NullPrmDb, which never returns VALID, so the
     * fallback branch in currentConfig() is the one that flies. Driving
     * the component with every parameter explicitly INVALID reproduces
     * that condition, and the derived TT offset pins LEAP_SEC at its
     * declared 37.0 default.
     */
    this->paramSet_LEAP_SEC(0.0, Fw::ParamValid::INVALID);
    this->paramSet_DUT1_SEC(0.0, Fw::ParamValid::INVALID);
    this->paramSet_MAX_TLE_AGE_DAYS(0.0f, Fw::ParamValid::INVALID);
    this->paramSet_UTC_OFFSET_SEC(0.0, Fw::ParamValid::INVALID);
    this->component.loadParameters();

    this->clearHistory();
    this->setClock(EPOCH_SECONDS, 0, TimeBase::TB_WORKSTATION_TIME);
    this->tick();

    const Gnc::OrbitState& s = this->lastOrbitState();

    // TT - UT1 = (TAI - UTC) + 32.184 with dut1 = 0. The FPP default of
    // 37.0 gives 69.184 s; a fallback of 0.0 would give 32.184.
    const F64 ttMinusUt1Sec = (s.get_jdTt() - s.get_jdUt1()) * 86400.0;
    ASSERT_NEAR(ttMinusUt1Sec, 37.0 + 32.184, 1e-3);
}

void OrbitPropagatorTester::testUtcOffsetApplied() {
    // Baseline with the parameter unset.
    this->paramSet_UTC_OFFSET_SEC(0.0, Fw::ParamValid::INVALID);
    this->component.loadParameters();
    this->clearHistory();
    this->setClock(EPOCH_SECONDS, 0, TimeBase::TB_WORKSTATION_TIME);
    this->tick();
    const F64 baseJdUt1 = this->lastOrbitState().get_jdUt1();

    // Now shift the time base, as a GPS-time source would require.
    this->paramSet_UTC_OFFSET_SEC(18.0, Fw::ParamValid::VALID);
    this->component.loadParameters();
    this->clearHistory();
    this->tick();
    const F64 shiftedJdUt1 = this->lastOrbitState().get_jdUt1();

    ASSERT_NEAR((shiftedJdUt1 - baseJdUt1) * 86400.0, 18.0, 1e-3);
}


/*
 * ============================================================================
 * ORB0010 / ORB0016 / ORB0017  OrbitState and Execution time measurement
 * ============================================================================
 */

void OrbitPropagatorTester::testGmstIsPublishedAndAdvances() {
    /*
     * GMST is computed once here and carried in OrbitState precisely so
     * that no downstream component derives its own. Checking the
     * sidereal rate confirms the carried value is live rather than a
     * constant.
     */
    this->clearHistory();
    this->setClock(EPOCH_SECONDS, 0, TimeBase::TB_WORKSTATION_TIME);
    this->tick();
    const F64 g0 = this->lastOrbitState().get_gmstRad();

    this->clearHistory();
    this->advanceClock(ONE_DAY_SECONDS);
    this->tick();
    const F64 g1 = this->lastOrbitState().get_gmstRad();

    const F64 RAD2DEG = 57.295779513082321;
    F64 advanceDeg = (g1 - g0) * RAD2DEG;
    if (advanceDeg < 0.0) {
        advanceDeg += 360.0;  // the wrap, not a rate error
    }

    // 360.98564736629 deg per UT1 day, modulo the full turn.
    ASSERT_NEAR(advanceDeg, 0.98564736629, 1e-3);
}

void OrbitPropagatorTester::testBroadcastToAllPorts() {
    /*
     * orbitOut has arity 2 so the magnetic field model and the solar
     * ephemeris evaluate against the same position and epoch. Every
     * connected instance must receive an identical payload.
     */
    this->clearHistory();
    this->m_orbitOutCount = 0;

    this->setClock(EPOCH_SECONDS, 0, TimeBase::TB_WORKSTATION_TIME);
    this->tick();

    const FwIndexType ports = OrbitPropagator::getNum_orbitOut_OutputPorts();
    ASSERT_EQ(ports, 2);

    ASSERT_from_orbitOut_SIZE(static_cast<U32>(ports));
    ASSERT_EQ(this->m_orbitOutCount, static_cast<FwSizeType>(ports));

    // Each instance was invoked exactly once, and they were distinct.
    ASSERT_NE(this->m_orbitOutPorts[0], this->m_orbitOutPorts[1]);

    const Gnc::OrbitState& a = this->fromPortHistory_orbitOut->at(0).orbitState;
    const Gnc::OrbitState& b = this->fromPortHistory_orbitOut->at(1).orbitState;

    ASSERT_EQ(a.get_validity(), b.get_validity());
    ASSERT_EQ(a.get_jdUt1(), b.get_jdUt1());
    ASSERT_EQ(a.get_jdTt(), b.get_jdTt());
    ASSERT_EQ(a.get_gmstRad(), b.get_gmstRad());
    ASSERT_EQ(a.get_stamp(), b.get_stamp());
    ASSERT_EQ(a.get_timeUsable(), b.get_timeUsable());
    ASSERT_EQ(a.get_positionUsable(), b.get_positionUsable());
}

void OrbitPropagatorTester::testCycleBudgetIsReported() {
    /*
     * CycleUsec covers the synchronous fan-out, so it measures the whole
     * GNC chain rather than this component alone. The harness has no
     * consumers connected and a frozen clock, so only presence is
     * checked here; the real number comes from the board test (ORB0018).
     */
    this->clearHistory();
    this->setClock(EPOCH_SECONDS, 0, TimeBase::TB_WORKSTATION_TIME);
    this->tick();

    ASSERT_TLM_CycleUsec_SIZE(1);

    // With a frozen test clock the elapsed time is zero. The I64 guard
    // in run_handler clamps at zero rather than wrapping, so this reads
    // 0 and not a huge unsigned value.
    ASSERT_TLM_CycleUsec(0, 0u);
}

}  // namespace Environment
}  // namespace Gnc
