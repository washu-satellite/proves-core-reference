// ======================================================================
// \title  DriverBoardHandlerComponentAc.hpp (host-test stub)
// \brief  Stand-in for the fpp-generated DriverBoardHandler component base.
//
// Declares the exact base-class surface DriverBoardHandler.cpp/.hpp use, and
// records every outgoing effect (uartSend bytes, uartRecvReturn buffer
// identities, getMode polls, sampleOut calls, telemetry writes, events,
// command responses) into public members so host tests can assert on them —
// the same role the autocoded TesterBase plays in a full F Prime UT build.
//
// Properties of the real base reproduced here:
//   * lock()/unLock() bracket the guarded region and count depth. The two
//     output ports that reach ANOTHER component's guarded or sync port from
//     the 1 Hz or command thread — uartSend_out (driverBoardUart.$send,
//     guarded) and getMode_out (modeManager.getMode) — set actionWhileLocked
//     if called with the count non-zero. That is the host-side form of the
//     deadlock rule in the plan (review amendment 2).
//   * uartRecvReturn_out and sampleOut_out are called from inside the guarded
//     uartRecv handler by design (the driver's own thread already holds the
//     driver mutex, as on the Svc.FrameAccumulator uplink path); they are
//     recorded with the lock depth seen, never flagged.
//   * uartRecv_guarded() invokes the handler the way the generated guarded
//     port does: lock, handler, unlock.
//   * getMode_out is connected by default (the E5 topology wires it);
//     sampleOut is NOT connected by default (unwired until A9).
//   * Event throttles are not modelled: every log_* call is recorded.
//
// Shapes mirror DriverBoardHandler.fpp: params PULSE_DURATION_MS U16 2000 /
// PULSE_DUTY_PCT U8 50 / PULSE_CHANNEL_MASK U8 0x07 / LINK_TIMEOUT_MS U16
// 1000 / HK_INTERVAL_S U8 1, ids 0..4 in declaration order.
// ======================================================================

#ifndef UnitTestSupport_DriverBoardHandlerComponentAc_HPP
#define UnitTestSupport_DriverBoardHandlerComponentAc_HPP

#include <string>
#include <vector>

#include "../../../FpTypesStub.hpp"
#include "../../../Fw/Buffer/Buffer.hpp"
#include "../../../Fw/Time/Time.hpp"
#include "../ModeManager/ModeManagerComponentAc.hpp"

namespace Components {

//! Mirrors the generated FPP enum Components.DriverState.
class DriverState {
  public:
    enum T { DISARMED = 0, ARMED = 1, PULSING = 2, FAULT = 3, UNKNOWN = 4 };
    DriverState() : e(DISARMED) {}
    DriverState(T e1) : e(e1) {}            // NOLINT(runtime/explicit) -- mirrors generated code
    operator T() const { return this->e; }  // NOLINT(runtime/explicit) -- enables switch/case
    bool operator==(T e1) const { return this->e == e1; }
    bool operator!=(T e1) const { return this->e != e1; }
    T e;
};

//! Mirrors the generated FPP enum Components.LinkState.
class LinkState {
  public:
    enum T { DOWN = 0, UP = 1 };
    LinkState() : e(DOWN) {}
    LinkState(T e1) : e(e1) {}              // NOLINT(runtime/explicit) -- mirrors generated code
    operator T() const { return this->e; }  // NOLINT(runtime/explicit) -- enables switch/case
    bool operator==(T e1) const { return this->e == e1; }
    bool operator!=(T e1) const { return this->e != e1; }
    T e;
};

//! Mirrors the generated FPP enum Components.DisarmReason.
class DisarmReason {
  public:
    enum T { COMMAND = 0, LINK_LOST = 1, SAFE_MODE = 2, BOARD_FAULT = 3, ABORT = 4 };
    DisarmReason() : e(COMMAND) {}
    DisarmReason(T e1) : e(e1) {}           // NOLINT(runtime/explicit) -- mirrors generated code
    operator T() const { return this->e; }  // NOLINT(runtime/explicit) -- enables switch/case
    bool operator==(T e1) const { return this->e == e1; }
    bool operator!=(T e1) const { return this->e != e1; }
    T e;
};

//! Mirrors the generated FPP enum Components.RefuseReason.
class RefuseReason {
  public:
    enum T { LINK_DOWN = 0, NOT_ARMED = 1, SAFE_MODE = 2, DRIVER_NOT_READY = 3, BOARD_REFUSED = 4 };
    RefuseReason() : e(LINK_DOWN) {}
    RefuseReason(T e1) : e(e1) {}           // NOLINT(runtime/explicit) -- mirrors generated code
    operator T() const { return this->e; }  // NOLINT(runtime/explicit) -- enables switch/case
    bool operator==(T e1) const { return this->e == e1; }
    bool operator!=(T e1) const { return this->e != e1; }
    T e;
};

class DriverBoardHandlerComponentBase {
  public:
    struct CmdResponseRecord {
        FwOpcodeType opCode;
        U32 cmdSeq;
        Fw::CmdResponse response;
    };

    //! One recorded uartRecvReturn call: buffer identity, never a copy.
    struct BufferReturnRecord {
        U8* data;
        FwSizeType size;
        I32 lockDepthSeen;
    };

    //! One recorded sampleOut call.
    struct SampleRecord {
        U32 boardTimeMs;
        I16 currentMa[3];
        I8 dutyPct[3];
        I32 lockDepthSeen;
    };

    struct PulseStartedRecord {
        U16 durationMs;
        U8 dutyPct;
        U8 channelMask;
        U8 polarityMask;
    };

    struct CommandRefusedRecord {
        U8 cmd;
        RefuseReason::T reason;
    };

    struct BoardFaultRecord {
        U8 flags;
        I16 value;
    };

    struct PongRecord {
        U16 fwVersion;
        U8 protoVersion;
    };

    struct StatusReportRecord {
        LinkState::T link;
        DriverState::T state;
        U8 flags;
        U16 version;
        U32 uptime;
    };

    explicit DriverBoardHandlerComponentBase(const char* const compName) : compName(compName) {}
    virtual ~DriverBoardHandlerComponentBase() {}

    //! Parameter ids, in DriverBoardHandler.fpp declaration order.
    static constexpr FwPrmIdType PARAMID_PULSE_DURATION_MS = 0;
    static constexpr FwPrmIdType PARAMID_PULSE_DUTY_PCT = 1;
    static constexpr FwPrmIdType PARAMID_PULSE_CHANNEL_MASK = 2;
    static constexpr FwPrmIdType PARAMID_LINK_TIMEOUT_MS = 3;
    static constexpr FwPrmIdType PARAMID_HK_INTERVAL_S = 4;

    // ---- handlers implemented by the component (private overrides there) ----
    virtual void uartRecv_handler(FwIndexType portNum, Fw::Buffer& buffer, const Drv::ByteStreamStatus& status) = 0;
    virtual void uartReady_handler(FwIndexType portNum) = 0;
    virtual void run_handler(FwIndexType portNum, U32 context) = 0;
    virtual void ARM_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) = 0;
    virtual void DISARM_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) = 0;
    virtual void PULSE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U8 polarityMask) = 0;
    virtual void ABORT_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) = 0;
    virtual void PING_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) = 0;
    virtual void GET_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) = 0;

    //! Public here so a test can drive the parameter-update path directly; the
    //! component overrides it privately, exactly as against real autocode.
    virtual void parameterUpdated(FwPrmIdType id) = 0;

    //! Invoke uartRecv the way the generated guarded port would: take the
    //! lock, call the handler, release the lock.
    void uartRecv_guarded(FwIndexType portNum, Fw::Buffer& buffer, const Drv::ByteStreamStatus& status) {
        this->lock();
        this->uartRecv_handler(portNum, buffer, status);
        this->unLock();
    }

    //! Time returned by getTime(); tests set it.
    Fw::Time now;

    // ---- test-controlled port connectivity ----
    bool getModeConnected = true;
    bool sampleOutConnected = false;

    //! Value getMode_out returns.
    SystemMode::T mode = SystemMode::NORMAL;

    //! Status uartSend_out returns.
    Drv::ByteStreamStatus::T sendStatus = Drv::ByteStreamStatus::OP_OK;

    // ---- test-controlled parameter values (DriverBoardHandler.fpp defaults) ----
    U16 pulseDurationMs = 2000;
    U8 pulseDutyPct = 50;
    U8 pulseChannelMask = 0x07;
    U16 linkTimeoutMs = 1000;
    U8 hkIntervalS = 1;
    Fw::ParamValid paramValidity = Fw::ParamValid::VALID;

    // ---- recorded outgoing effects, public for test inspection ----
    std::string compName;
    //! Every uartSend_out call, as a copy of the bytes, in order.
    std::vector<std::vector<U8>> uartSendCalls;
    std::vector<BufferReturnRecord> uartRecvReturnCalls;
    U32 getModeCalls = 0;
    std::vector<SampleRecord> sampleOutCalls;
    //! Set if uartSend_out or getMode_out was called while the lock was held.
    bool actionWhileLocked = false;
    //! Set if unLock() was called without a matching lock().
    bool lockUnderflow = false;
    I32 lockDepth = 0;
    U32 lockCalls = 0;

    std::vector<F32> tlmCoilCurrent0;
    std::vector<F32> tlmCoilCurrent1;
    std::vector<F32> tlmCoilCurrent2;
    std::vector<F32> tlmCoilTemperature0;
    std::vector<F32> tlmCoilTemperature1;
    std::vector<I8> tlmPwmDuty0;
    std::vector<I8> tlmPwmDuty1;
    std::vector<I8> tlmPwmDuty2;
    std::vector<DriverState::T> tlmDriverState;
    std::vector<U8> tlmFaultFlags;
    std::vector<LinkState::T> tlmLinkState;
    std::vector<U32> tlmBoardUptime;
    std::vector<U32> tlmFramesReceived;
    std::vector<U16> tlmFramesRejected;
    std::vector<U32> tlmSamplesReceived;
    std::vector<U16> tlmLinkTimeouts;
    std::vector<U16> tlmFirmwareVersion;
    std::vector<U16> tlmPulsesCommanded;
    std::vector<U16> tlmPulseDurationMs;
    std::vector<U8> tlmPulseDutyPct;
    std::vector<U16> tlmLinkTimeoutMs;
    std::vector<U8> tlmHkIntervalS;

    std::vector<U16> eventsLinkUp;
    std::vector<U32> eventsLinkLost;
    std::vector<U8> eventsFrameRejected;
    U32 eventsArmed = 0;
    std::vector<DisarmReason::T> eventsDisarmed;
    std::vector<PulseStartedRecord> eventsPulseStarted;
    std::vector<U8> eventsPulseRefused;
    std::vector<CommandRefusedRecord> eventsCommandRefused;
    std::vector<BoardFaultRecord> eventsBoardFault;
    std::vector<PongRecord> eventsPongReceived;
    std::vector<StatusReportRecord> eventsStatusReport;
    std::vector<U32> eventsParameterRejected;
    std::vector<CmdResponseRecord> cmdResponses;

    //! Total events recorded, for "no events" assertions.
    U32 totalEvents() const {
        return static_cast<U32>(
            this->eventsLinkUp.size() + this->eventsLinkLost.size() + this->eventsFrameRejected.size() +
            this->eventsArmed + this->eventsDisarmed.size() + this->eventsPulseStarted.size() +
            this->eventsPulseRefused.size() + this->eventsCommandRefused.size() + this->eventsBoardFault.size() +
            this->eventsPongReceived.size() + this->eventsStatusReport.size() + this->eventsParameterRejected.size());
    }

  protected:
    // ---- guarded-port mutex stand-in ----
    virtual void lock() {
        this->lockDepth++;
        this->lockCalls++;
    }
    virtual void unLock() {
        if (this->lockDepth == 0) {
            this->lockUnderflow = true;
        } else {
            this->lockDepth--;
        }
    }

    // ---- time ----
    Fw::Time getTime() { return this->now; }

    // ---- output ports ----
    Drv::ByteStreamStatus uartSend_out(FwIndexType portNum, Fw::Buffer& buffer) {
        (void)portNum;
        if (this->lockDepth > 0) {
            this->actionWhileLocked = true;
        }
        const U8* data = buffer.getData();
        this->uartSendCalls.push_back(std::vector<U8>(data, data + buffer.getSize()));
        return this->sendStatus;
    }

    void uartRecvReturn_out(FwIndexType portNum, Fw::Buffer& buffer) {
        (void)portNum;
        this->uartRecvReturnCalls.push_back(BufferReturnRecord{buffer.getData(), buffer.getSize(), this->lockDepth});
    }

    bool isConnected_getMode_OutputPort(FwIndexType portNum) const {
        (void)portNum;
        return this->getModeConnected;
    }
    SystemMode getMode_out(FwIndexType portNum) {
        (void)portNum;
        if (this->lockDepth > 0) {
            this->actionWhileLocked = true;
        }
        this->getModeCalls++;
        return SystemMode(this->mode);
    }

    bool isConnected_sampleOut_OutputPort(FwIndexType portNum) const {
        (void)portNum;
        return this->sampleOutConnected;
    }
    void sampleOut_out(FwIndexType portNum,
                       U32 boardTimeMs,
                       I16 currentMa0,
                       I16 currentMa1,
                       I16 currentMa2,
                       I8 dutyPct0,
                       I8 dutyPct1,
                       I8 dutyPct2) {
        (void)portNum;
        SampleRecord record;
        record.boardTimeMs = boardTimeMs;
        record.currentMa[0] = currentMa0;
        record.currentMa[1] = currentMa1;
        record.currentMa[2] = currentMa2;
        record.dutyPct[0] = dutyPct0;
        record.dutyPct[1] = dutyPct1;
        record.dutyPct[2] = dutyPct2;
        record.lockDepthSeen = this->lockDepth;
        this->sampleOutCalls.push_back(record);
    }

    // ---- parameters ----
    U16 paramGet_PULSE_DURATION_MS(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return this->pulseDurationMs;
    }
    U8 paramGet_PULSE_DUTY_PCT(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return this->pulseDutyPct;
    }
    U8 paramGet_PULSE_CHANNEL_MASK(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return this->pulseChannelMask;
    }
    U16 paramGet_LINK_TIMEOUT_MS(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return this->linkTimeoutMs;
    }
    U8 paramGet_HK_INTERVAL_S(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return this->hkIntervalS;
    }

    // ---- telemetry ----
    void tlmWrite_CoilCurrent0(F32 v) { this->tlmCoilCurrent0.push_back(v); }
    void tlmWrite_CoilCurrent1(F32 v) { this->tlmCoilCurrent1.push_back(v); }
    void tlmWrite_CoilCurrent2(F32 v) { this->tlmCoilCurrent2.push_back(v); }
    void tlmWrite_CoilTemperature0(F32 v) { this->tlmCoilTemperature0.push_back(v); }
    void tlmWrite_CoilTemperature1(F32 v) { this->tlmCoilTemperature1.push_back(v); }
    void tlmWrite_PwmDuty0(I8 v) { this->tlmPwmDuty0.push_back(v); }
    void tlmWrite_PwmDuty1(I8 v) { this->tlmPwmDuty1.push_back(v); }
    void tlmWrite_PwmDuty2(I8 v) { this->tlmPwmDuty2.push_back(v); }
    void tlmWrite_DriverState(const DriverState& v) { this->tlmDriverState.push_back(v.e); }
    void tlmWrite_FaultFlags(U8 v) { this->tlmFaultFlags.push_back(v); }
    void tlmWrite_LinkState(const LinkState& v) { this->tlmLinkState.push_back(v.e); }
    void tlmWrite_BoardUptime(U32 v) { this->tlmBoardUptime.push_back(v); }
    void tlmWrite_FramesReceived(U32 v) { this->tlmFramesReceived.push_back(v); }
    void tlmWrite_FramesRejected(U16 v) { this->tlmFramesRejected.push_back(v); }
    void tlmWrite_SamplesReceived(U32 v) { this->tlmSamplesReceived.push_back(v); }
    void tlmWrite_LinkTimeouts(U16 v) { this->tlmLinkTimeouts.push_back(v); }
    void tlmWrite_FirmwareVersion(U16 v) { this->tlmFirmwareVersion.push_back(v); }
    void tlmWrite_PulsesCommanded(U16 v) { this->tlmPulsesCommanded.push_back(v); }
    void tlmWrite_PulseDurationMs(U16 v) { this->tlmPulseDurationMs.push_back(v); }
    void tlmWrite_PulseDutyPct(U8 v) { this->tlmPulseDutyPct.push_back(v); }
    void tlmWrite_LinkTimeoutMs(U16 v) { this->tlmLinkTimeoutMs.push_back(v); }
    void tlmWrite_HkIntervalS(U8 v) { this->tlmHkIntervalS.push_back(v); }

    // ---- events ----
    void log_ACTIVITY_HI_LinkUp(U16 version) { this->eventsLinkUp.push_back(version); }
    void log_WARNING_HI_LinkLost(U32 silentMs) { this->eventsLinkLost.push_back(silentMs); }
    void log_WARNING_LO_FrameRejected(U8 reason) { this->eventsFrameRejected.push_back(reason); }
    void log_ACTIVITY_HI_Armed() { this->eventsArmed++; }
    void log_ACTIVITY_HI_Disarmed(const DisarmReason& reason) { this->eventsDisarmed.push_back(reason.e); }
    void log_ACTIVITY_HI_PulseStarted(U16 durationMs, U8 dutyPct, U8 channelMask, U8 polarityMask) {
        this->eventsPulseStarted.push_back(PulseStartedRecord{durationMs, dutyPct, channelMask, polarityMask});
    }
    void log_WARNING_LO_PulseRefused(U8 status) { this->eventsPulseRefused.push_back(status); }
    void log_WARNING_LO_CommandRefused(U8 cmd, const RefuseReason& reason) {
        this->eventsCommandRefused.push_back(CommandRefusedRecord{cmd, reason.e});
    }
    void log_WARNING_HI_BoardFault(U8 flags, I16 value) {
        this->eventsBoardFault.push_back(BoardFaultRecord{flags, value});
    }
    void log_ACTIVITY_LO_PongReceived(U16 fwVersion, U8 protoVersion) {
        this->eventsPongReceived.push_back(PongRecord{fwVersion, protoVersion});
    }
    void log_ACTIVITY_LO_StatusReport(const LinkState& link,
                                      const DriverState& state,
                                      U8 flags,
                                      U16 version,
                                      U32 uptime) {
        this->eventsStatusReport.push_back(StatusReportRecord{link.e, state.e, flags, version, uptime});
    }
    void log_WARNING_LO_ParameterRejected(U32 paramId) { this->eventsParameterRejected.push_back(paramId); }

    void cmdResponse_out(FwOpcodeType opCode, U32 cmdSeq, const Fw::CmdResponse& response) {
        this->cmdResponses.push_back(CmdResponseRecord{opCode, cmdSeq, response});
    }
};

}  // namespace Components

#endif
