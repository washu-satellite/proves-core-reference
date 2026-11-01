/**
 * \file  OrbitPropagatorTester.hpp
 * \brief  Component unit tests for Gnc::Environment::OrbitPropagator
 * 
 * Covers the requirements that need the autocoded layer: the time base
 * gate, the emit-on-every-path contract, event content, command
 * handling and port fan-out.
 */

#ifndef Gnc_Environment_OrbitPropagatorTester_HPP
#define Gnc_Environment_OrbitPropagatorTester_HPP

#include "PROVESFlightControllerReference/Gnc/Environment/OrbitPropagator/OrbitPropagator.hpp"
#include "PROVESFlightControllerReference/Gnc/Environment/OrbitPropagator/OrbitPropagatorGTestBase.hpp"

namespace Gnc {
namespace Environment {

class OrbitPropagatorTester : public OrbitPropagatorGTestBase {
  public:
    /*
    * ============================================================================
    * Constants
    * ============================================================================
    */

    //! Instance name for the component under test
    static const char* const TEST_INSTANCE_NAME;

    //! Queue depth for the active component's message queue
    static const FwSizeType TEST_INSTANCE_QUEUE_DEPTH = 10;

    //! Instance ID for the component under test
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;

    //! History depth. Several tests tick repeatedly, so this is generous.
    static const U32 MAX_HISTORY_SIZE = 100;

    OrbitPropagatorTester();
    ~OrbitPropagatorTester();

    /*
    * ============================================================================
    * Tests 
    * ============================================================================
    */

    //! ORB0001: a non-wall-clock time base suspends navigation.
    void testTimeBaseRejected();

    //! ORB0002: both TB_WORKSTATION_TIME and TB_SC_TIME are accepted.
    void testAcceptedTimeBases();

    //! ORB0003/ORB0004: no TLE still publishes valid time products.
    void testNoTleStillPublishesTime();

    //! ORB0003: position telemetry doesn't publish on failure paths.
    void testNoPositionTelemetryWhenInvalid();

    //! ORB0003: every path emits
    void testEmitsOnEveryPath();

    //! ORB0006: a malformed TLE is rejected and changes nothing.
    void testBadTleRejected();

    //! ORB0007: CLEAR_TLE stops position but not time.
    void testClearTle();

    //! ORB0012: unset parameters fall back to the FPP defaults.
    void testParameterFallback();

    //! ORB0013: UTC_OFFSET_SEC shifts the derived time scales.
    void testUtcOffsetApplied();

    //! ORB0016: OrbitState reaches every connected output port.
    void testBroadcastToAllPorts();

    //! ORB0017: CycleUsec is written on every tick.
    void testCycleBudgetIsReported();

    //! ORB0010: GMST is published and advances at the sidereal rate.
    void testGmstIsPublishedAndAdvances();

  private:
    /*
    * ============================================================================
    * Handler overrides
    * ============================================================================
    */

    /**
     * \brief Record which port index each OrbitState arrived on.
     *
     * 
     * \details The generated FromPortEntry_orbitOut doesn't record the port number, 
     * which is needed by ORB0016. This override records the port number for the comparison, 
     * then fills the port history as normal.
     */
    void from_orbitOut_handler(FwIndexType portNum, Gnc::OrbitState& orbitState) override;
    
    /*
    * ============================================================================
    * Helpers
    * ============================================================================
    */

    //! Wire the harness to the component and start it.
    void initComponents();
    void connectPorts();

    /**
     * \brief Set the harness clock.
     */
    void setClock(U32 seconds, U32 useconds, TimeBase::T base);

    //! Advance the clock without changing the time base.
    void advanceClock(U32 seconds);

    //! Drive one rate group tick and drain the active component's queue.
    void tick();

    //! Most recent OrbitState on the from-port history.
    const Gnc::OrbitState& lastOrbitState() const;

    OrbitPropagator component;

    //! Port indices seen by from_orbitOut_handler, in arrival order.
    FwIndexType m_orbitOutPorts[16];
    FwSizeType m_orbitOutCount = 0;

    //! Clock state served to the component through the time port.
    U32 m_seconds = 0;
    U32 m_useconds = 0;
    TimeBase::T m_timeBase = TimeBase::TB_WORKSTATION_TIME;
};

}  // namespace Environment
}  // namespace Gnc

#endif
