// ======================================================================
// \title  DriverBoardHandler.hpp
// \brief  hpp file for DriverBoardHandler component implementation class
//
// Thin component: port handlers call the pure codec
// (Components/DriverBoardProtocol) and the pure link state machine
// (DriverBoardLink). See docs/sdd.md for the locking rule, the buffer
// ownership and the link state machine.
// ======================================================================

#ifndef Components_DriverBoardHandler_HPP
#define Components_DriverBoardHandler_HPP

#include "PROVESFlightControllerReference/Components/DriverBoardHandler/DriverBoardHandlerComponentAc.hpp"
#include "PROVESFlightControllerReference/Components/DriverBoardHandler/DriverBoardLink.hpp"
#include "PROVESFlightControllerReference/Components/DriverBoardProtocol/DriverBoardMessages.hpp"
#include "PROVESFlightControllerReference/Components/DriverBoardProtocol/DriverBoardProtocol.hpp"
#include "PROVESFlightControllerReference/Components/RunInterval/RunInterval.hpp"

namespace Components {

class DriverBoardHandler : public DriverBoardHandlerComponentBase {
  public:
    // ----------------------------------------------------------------------
    // Component construction and destruction
    // ----------------------------------------------------------------------

    //! Construct DriverBoardHandler object
    explicit DriverBoardHandler(const char* const compName  //!< The component name
    );

    //! Destroy DriverBoardHandler object
    ~DriverBoardHandler();

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for user-defined typed input ports
    // ----------------------------------------------------------------------

    //! Handler implementation for uartRecv
    //!
    //! Guarded (the framework holds the component lock). Parses every byte,
    //! applies each decoded board frame, then returns the buffer on
    //! uartRecvReturn. Never sends: anything a frame wants sent is queued
    //! for the next run tick.
    void uartRecv_handler(FwIndexType portNum,  //!< The port number
                          Fw::Buffer& buffer,
                          const Drv::ByteStreamStatus& status) override;

    //! Handler implementation for uartReady
    //!
    //! Marks the driver ready and sends the first host frame, DISARM.
    void uartReady_handler(FwIndexType portNum  //!< The port number
                           ) override;

    //! Handler implementation for run
    //!
    //! 1 Hz: polls the mode, runs link supervision, sends one host frame.
    void run_handler(FwIndexType portNum,  //!< The port number
                     U32 context           //!< The call order
                     ) override;

    // ----------------------------------------------------------------------
    // Handler implementations for commands
    // ----------------------------------------------------------------------

    //! Handler implementation for command ARM
    void ARM_cmdHandler(FwOpcodeType opCode,  //!< The opcode
                        U32 cmdSeq            //!< The command sequence number
                        ) override;

    //! Handler implementation for command DISARM
    void DISARM_cmdHandler(FwOpcodeType opCode,  //!< The opcode
                           U32 cmdSeq            //!< The command sequence number
                           ) override;

    //! Handler implementation for command PULSE
    void PULSE_cmdHandler(FwOpcodeType opCode,  //!< The opcode
                          U32 cmdSeq,           //!< The command sequence number
                          U8 polarityMask) override;

    //! Handler implementation for command ABORT
    void ABORT_cmdHandler(FwOpcodeType opCode,  //!< The opcode
                          U32 cmdSeq            //!< The command sequence number
                          ) override;

    //! Handler implementation for command PING
    void PING_cmdHandler(FwOpcodeType opCode,  //!< The opcode
                         U32 cmdSeq            //!< The command sequence number
                         ) override;

    //! Handler implementation for command GET_STATUS
    void GET_STATUS_cmdHandler(FwOpcodeType opCode,  //!< The opcode
                               U32 cmdSeq            //!< The command sequence number
                               ) override;

    // ----------------------------------------------------------------------
    // Parameter update hook
    // ----------------------------------------------------------------------

    //! Re-read one parameter; an INVALID/UNINIT or out-of-range value falls
    //! back to the fpp default with one ParameterRejected event.
    void parameterUpdated(FwPrmIdType id) override;

    // ----------------------------------------------------------------------
    // Private helpers
    // ----------------------------------------------------------------------

    //! Current time as a millisecond counter (wraps; differences are unsigned)
    U32 nowMs();

    //! Poll ModeManager. Called with the lock released; false when unconnected.
    bool pollSafeMode();

    //! Apply one parameter to its cache. Caller holds the lock.
    void applyParameter(FwPrmIdType id);

    //! Apply every parameter. Caller holds the lock.
    void applyAllParameters();

    //! Build the frame the link asked for into out. Caller holds the lock
    //! (the TX sequence number is state). Returns the frame length or 0.
    FwSizeType buildHostFrame(DriverBoardLink::HostFrame which, U32 hostTickMs, U8* out);

    //! Send a frame built by buildHostFrame. Caller must NOT hold the lock.
    void sendFrame(U8* frame, FwSizeType length);

    //! Apply one decoded board frame. Caller holds the lock.
    void handleFrame(const DriverBoardProtocol::Frame& frame, U32 receivedMs);

    //! Count a dropped frame and log its reason. Caller holds the lock.
    void dropFrame(U8 reason);

    //! Stamp a valid board frame and raise the link if it was down. Caller holds the lock.
    void noteValidFrame(U32 receivedMs, U16 versionForEvent);

    //! Record a local driver-state transition in telemetry. Caller holds the lock.
    void setDriverState(DriverState::T state);

    //! Publish the parser counters when they changed. Caller holds the lock.
    void publishCounters();

    // ----------------------------------------------------------------------
    // Member variables (declared in initialisation order; -Wreorder)
    // ----------------------------------------------------------------------

    DriverBoardProtocol::Parser m_parser;  //!< Byte-wise frame parser, one frame of buffer
    DriverBoardLink m_link;                //!< Link and arming state machine
    RunInterval m_hkInterval;              //!< HK_REQUEST cadence (Cycle B helper)
    U16 m_pulseDurationMs;                 //!< Effective PULSE_DURATION_MS
    U8 m_pulseDutyPct;                     //!< Effective PULSE_DUTY_PCT
    U8 m_pulseChannelMask;                 //!< Effective PULSE_CHANNEL_MASK
    U16 m_linkTimeoutMs;                   //!< Effective LINK_TIMEOUT_MS
    U8 m_hkIntervalS;                      //!< Effective HK_INTERVAL_S
    U8 m_txSeq;                            //!< SEQ of the next host frame
    U16 m_firmwareVersion;                 //!< From the last PONG, 0 until then
    U32 m_droppedFrames;                   //!< Unknown TYPE or bad payload length
    U32 m_samplesReceived;                 //!< SAMPLE frames parsed
    U16 m_pulsesCommanded;                 //!< PULSE frames sent
    U8 m_faultFlags;                       //!< Last HK/FAULT fault flags
    U32 m_boardUptimeMs;                   //!< Last HK uptime
    DriverState::T m_driverState;          //!< Last telemetered DriverState
    U32 m_tlmFramesReceived;               //!< Last FramesReceived written
    U16 m_tlmFramesRejected;               //!< Last FramesRejected written
    U32 m_lastParserRejected;              //!< Parser rejections already logged
    bool m_paramsApplied;                  //!< Parameters read on the first tick
};

}  // namespace Components

#endif
