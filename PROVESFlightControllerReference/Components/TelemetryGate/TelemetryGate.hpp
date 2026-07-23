// ======================================================================
// \title  TelemetryGate.hpp
// \brief  hpp file for TelemetryGate component implementation class
// ======================================================================

#ifndef Components_TelemetryGate_HPP
#define Components_TelemetryGate_HPP

#include "PROVESFlightControllerReference/Components/TelemetryGate/TelemetryGateComponentAc.hpp"

namespace Components {

class TelemetryGate final : public TelemetryGateComponentBase {
  public:
    // ----------------------------------------------------------------------
    // Component construction and destruction
    // ----------------------------------------------------------------------

    //! Construct TelemetryGate object
    TelemetryGate(const char* const compName  //!< The component name
    );

    //! Destroy TelemetryGate object
    ~TelemetryGate();

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for typed input ports
    // ----------------------------------------------------------------------

    //! Handler implementation for runIn
    //!
    //! Forwards the scheduler tick to runOut when telemetry is ENABLED; when
    //! DISABLED the tick is dropped (inhibiting upstream telemetry scheduling)
    //! and the gated-tick counter is incremented.
    void runIn_handler(FwIndexType portNum,  //!< The port number
                       U32 context           //!< The call order
                       ) override;

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for commands
    // ----------------------------------------------------------------------

    //! Handler implementation for command SET_TRANSMIT_STATE
    //!
    //! Latches the requested state in RAM immediately, persists it to flash, and
    //! emits the TransmitStateSet event. Returns EXECUTION_ERROR only if the
    //! flash write fails (the in-RAM state change still takes effect).
    void SET_TRANSMIT_STATE_cmdHandler(FwOpcodeType opCode,                 //!< The opcode
                                       U32 cmdSeq,                          //!< The command sequence number
                                       Components::TelemetryTxState txState  //!< Desired telemetry transmission state
                                       ) override;

  private:
    // ----------------------------------------------------------------------
    // Private helper methods
    // ----------------------------------------------------------------------

    //! \brief Load the persisted state from flash into m_state.
    //!
    //! Missing file -> default ENABLED (no event). Corrupt/truncated file ->
    //! default ENABLED plus a StateFileCorrupt event. Called lazily on the first
    //! runIn tick so it runs after the filesystem is mounted.
    void loadState();

    //! \brief Persist the given state to flash.
    //! \return true on success, false if encode or file write failed.
    bool persistState(Components::TelemetryTxState state);

  private:
    //! Current telemetry transmission state (defaults ENABLED, fail-operational)
    Components::TelemetryTxState m_state;
    //! Count of scheduler ticks gated (dropped) while disabled
    U32 m_gated_ticks;
    //! Whether the persisted state has been loaded (or superseded by a command)
    bool m_loaded;
};

}  // namespace Components

#endif
