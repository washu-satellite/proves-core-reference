// ======================================================================
// \title  DriverBoardHandler.cpp
// \brief  cpp file for DriverBoardHandler component implementation class
//
// Locking rule (docs/sdd.md): uartRecv is guarded, so the framework holds
// the component lock for the whole handler; run, uartReady and every command
// take the lock explicitly around state work only. uartSend_out and
// getMode_out are never called with the lock held: frames are built into a
// stack-local array under the lock and sent after unLock(). uartRecv never
// sends at all; a frame that wants a DISARM sent queues it for the next tick.
// ======================================================================

#include "PROVESFlightControllerReference/Components/DriverBoardHandler/DriverBoardHandler.hpp"

namespace Components {

namespace {

namespace Proto = Components::DriverBoardProtocol;

//! Parameter defaults, DriverBoardHandler.fpp. The fallback for a rejected
//! parameter is always the fpp default so a bad PRM_SET cannot change flight
//! behaviour beyond what the dictionary says.
constexpr U16 DEFAULT_PULSE_DURATION_MS = 2000;
constexpr U8 DEFAULT_PULSE_DUTY_PCT = 50;
constexpr U8 DEFAULT_PULSE_CHANNEL_MASK = 0x07;
constexpr U16 DEFAULT_LINK_TIMEOUT_MS = 1000;
constexpr U8 DEFAULT_HK_INTERVAL_S = 1;

//! Parameter ranges, plan 03-design.md 3.3.
constexpr U16 MIN_LINK_TIMEOUT_MS = 100;
constexpr U16 MAX_LINK_TIMEOUT_MS = 10000;
constexpr U8 MIN_PULSE_CHANNEL_MASK = 0x01;

//! FrameRejected reason codes beyond the parser's (1 SYNC, 2 LENGTH, 3 CRC).
constexpr U8 REJECT_UNKNOWN_TYPE = 4;
constexpr U8 REJECT_BAD_PAYLOAD = 5;

//! Map a board-reported HK state byte onto the DriverState channel.
DriverState::T boardStateToDriverState(U8 boardState) {
    switch (static_cast<Proto::BoardState>(boardState)) {
        case Proto::BoardState::DISARMED:
            return DriverState::DISARMED;
        case Proto::BoardState::ARMED:
            return DriverState::ARMED;
        case Proto::BoardState::PULSING:
            return DriverState::PULSING;
        case Proto::BoardState::FAULT:
            return DriverState::FAULT;
        default:
            return DriverState::UNKNOWN;
    }
}

bool paramReadable(const Fw::ParamValid& valid) {
    return (valid != Fw::ParamValid::INVALID) && (valid != Fw::ParamValid::UNINIT);
}

}  // namespace

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

DriverBoardHandler ::DriverBoardHandler(const char* const compName)
    : DriverBoardHandlerComponentBase(compName),
      m_parser(),
      m_link(),
      m_hkInterval(),
      m_pulseDurationMs(DEFAULT_PULSE_DURATION_MS),
      m_pulseDutyPct(DEFAULT_PULSE_DUTY_PCT),
      m_pulseChannelMask(DEFAULT_PULSE_CHANNEL_MASK),
      m_linkTimeoutMs(DEFAULT_LINK_TIMEOUT_MS),
      m_hkIntervalS(DEFAULT_HK_INTERVAL_S),
      m_txSeq(0),
      m_firmwareVersion(0),
      m_droppedFrames(0),
      m_samplesReceived(0),
      m_pulsesCommanded(0),
      m_faultFlags(0),
      m_boardUptimeMs(0),
      m_driverState(DriverState::DISARMED),
      m_tlmFramesReceived(0),
      m_tlmFramesRejected(0),
      m_lastParserRejected(0),
      m_paramsApplied(false) {}

DriverBoardHandler ::~DriverBoardHandler() {}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

void DriverBoardHandler ::uartRecv_handler(FwIndexType portNum,
                                           Fw::Buffer& buffer,
                                           const Drv::ByteStreamStatus& status) {
    (void)portNum;
    // Guarded: the lock is already held. Parse only a buffer the driver
    // filled; every buffer, whatever its status or content, goes back.
    if ((status == Drv::ByteStreamStatus::OP_OK) && buffer.isValid()) {
        const U32 receivedMs = this->nowMs();
        const U8* const data = buffer.getData();
        const FwSizeType size = buffer.getSize();
        for (FwSizeType i = 0; i < size; i++) {
            if (this->m_parser.feed(data[i])) {
                this->handleFrame(this->m_parser.frame(), receivedMs);
            }
        }
        this->publishCounters();
    }
    // Same thread that holds the driver's own mutex (its schedIn is guarded
    // and calls $recv from inside), so this re-enters that mutex recursively
    // exactly as Svc.FrameAccumulator does on the uplink; no inversion.
    this->uartRecvReturn_out(0, buffer);
}

void DriverBoardHandler ::uartReady_handler(FwIndexType portNum) {
    (void)portNum;
    U8 tx[Proto::MAX_FRAME];
    FwSizeType txLen = 0;

    this->lock();
    const DriverBoardLink::HostFrame first = this->m_link.driverReady();
    txLen = this->buildHostFrame(first, this->nowMs(), tx);
    this->unLock();

    this->sendFrame(tx, txLen);
}

void DriverBoardHandler ::run_handler(FwIndexType portNum, U32 context) {
    (void)portNum;
    (void)context;
    // Both reads happen with the lock released: getMode is another
    // component's port and getTime goes through the time port.
    const bool safeMode = this->pollSafeMode();
    const U32 now = this->nowMs();
    U8 tx[Proto::MAX_FRAME];
    FwSizeType txLen = 0;

    this->lock();
    if (!this->m_paramsApplied) {
        // Parameters loaded from PrmDb at boot do not call parameterUpdated;
        // read them once here so a saved value survives a reboot, and write
        // the static channels once so the packet has a baseline.
        this->m_paramsApplied = true;
        this->applyAllParameters();
        this->tlmWrite_LinkState(LinkState::DOWN);
        this->tlmWrite_DriverState(this->m_driverState);
    }

    DriverBoardLink::TickInput in;
    in.nowMs = now;
    in.linkTimeoutMs = this->m_linkTimeoutMs;
    in.safeMode = safeMode;
    // The cadence starts with the first tick after the driver is ready, so the
    // first frame after DISARM is an HK_REQUEST (RunInterval's first due()).
    in.hkDue = this->m_link.isDriverReady() ? this->m_hkInterval.due(this->m_hkIntervalS) : false;

    const DriverBoardLink::TickResult result = this->m_link.tick(in);

    if (result.linkLost) {
        this->log_WARNING_HI_LinkLost(result.silentMs);
        this->tlmWrite_LinkState(LinkState::DOWN);
        this->tlmWrite_LinkTimeouts(this->m_link.linkTimeouts());
    }
    if (result.disarm == DriverBoardLink::DisarmCause::LINK_LOST) {
        this->log_ACTIVITY_HI_Disarmed(DisarmReason::LINK_LOST);
        this->setDriverState(DriverState::DISARMED);
    } else if (result.disarm == DriverBoardLink::DisarmCause::SAFE_MODE) {
        this->log_ACTIVITY_HI_Disarmed(DisarmReason::SAFE_MODE);
        this->setDriverState(DriverState::DISARMED);
    }
    txLen = this->buildHostFrame(result.frame, now, tx);
    this->unLock();

    this->sendFrame(tx, txLen);
}

// ----------------------------------------------------------------------
// Handler implementations for commands
// ----------------------------------------------------------------------

void DriverBoardHandler ::ARM_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    const bool safeMode = this->pollSafeMode();
    U8 tx[Proto::MAX_FRAME];
    FwSizeType txLen = 0;
    bool refused = false;
    RefuseReason::T reason = RefuseReason::LINK_DOWN;

    this->lock();
    if (this->m_link.link() != DriverBoardLink::Link::UP) {
        refused = true;
        reason = RefuseReason::LINK_DOWN;
    } else if (safeMode) {
        refused = true;
        reason = RefuseReason::SAFE_MODE;
    } else {
        Proto::Arm arm;
        arm.magic = Proto::ARM_MAGIC;
        txLen = static_cast<FwSizeType>(Proto::encodeMessage(arm, this->m_txSeq++, tx, sizeof(tx)));
        this->m_link.armSent();
    }
    this->unLock();

    if (refused) {
        this->log_WARNING_LO_CommandRefused(static_cast<U8>(Proto::Type::ARM), reason);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    // OK means "ARM sent"; ARMED follows only on ACK(ARM, 0) in uartRecv.
    this->sendFrame(tx, txLen);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void DriverBoardHandler ::DISARM_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    U8 tx[Proto::MAX_FRAME];
    FwSizeType txLen = 0;

    this->lock();
    (void)this->m_link.disarm();
    this->log_ACTIVITY_HI_Disarmed(DisarmReason::COMMAND);
    this->setDriverState(DriverState::DISARMED);
    if (this->m_link.isDriverReady()) {
        txLen = this->buildHostFrame(DriverBoardLink::HostFrame::DISARM, this->nowMs(), tx);
    }
    this->unLock();

    // Local state went DISARMED whether or not a driver exists to tell.
    this->sendFrame(tx, txLen);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void DriverBoardHandler ::PULSE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U8 polarityMask) {
    if ((polarityMask & static_cast<U8>(~Proto::CHANNEL_MASK_BITS)) != 0) {
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    U8 tx[Proto::MAX_FRAME];
    FwSizeType txLen = 0;
    bool refused = false;

    this->lock();
    if (!this->m_link.isArmed()) {
        refused = true;
    } else {
        Proto::Pulse pulse;
        pulse.durationMs = this->m_pulseDurationMs;
        pulse.dutyPct = this->m_pulseDutyPct;
        pulse.channelMask = this->m_pulseChannelMask;
        pulse.polarityMask = polarityMask;
        txLen = static_cast<FwSizeType>(Proto::encodeMessage(pulse, this->m_txSeq++, tx, sizeof(tx)));
        if (this->m_pulsesCommanded < UINT16_MAX) {
            this->m_pulsesCommanded++;
        }
        this->tlmWrite_PulsesCommanded(this->m_pulsesCommanded);
        this->log_ACTIVITY_HI_PulseStarted(pulse.durationMs, pulse.dutyPct, pulse.channelMask, pulse.polarityMask);
    }
    this->unLock();

    if (refused) {
        this->log_WARNING_LO_CommandRefused(static_cast<U8>(Proto::Type::PULSE), RefuseReason::NOT_ARMED);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    this->sendFrame(tx, txLen);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void DriverBoardHandler ::ABORT_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    U8 tx[Proto::MAX_FRAME];
    FwSizeType txLen = 0;

    this->lock();
    (void)this->m_link.disarm();
    this->log_ACTIVITY_HI_Disarmed(DisarmReason::ABORT);
    this->setDriverState(DriverState::DISARMED);
    if (this->m_link.isDriverReady()) {
        Proto::Abort abort;
        txLen = static_cast<FwSizeType>(Proto::encodeMessage(abort, this->m_txSeq++, tx, sizeof(tx)));
    }
    this->unLock();

    this->sendFrame(tx, txLen);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void DriverBoardHandler ::PING_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    U8 tx[Proto::MAX_FRAME];
    FwSizeType txLen = 0;
    bool refused = false;

    this->lock();
    if (!this->m_link.isDriverReady()) {
        refused = true;
    } else {
        Proto::Ping ping;
        txLen = static_cast<FwSizeType>(Proto::encodeMessage(ping, this->m_txSeq++, tx, sizeof(tx)));
    }
    this->unLock();

    if (refused) {
        this->log_WARNING_LO_CommandRefused(static_cast<U8>(Proto::Type::PING), RefuseReason::DRIVER_NOT_READY);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    this->sendFrame(tx, txLen);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void DriverBoardHandler ::GET_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->lock();
    const LinkState::T link = (this->m_link.link() == DriverBoardLink::Link::UP) ? LinkState::UP : LinkState::DOWN;
    const DriverState::T state = this->m_driverState;
    const U8 flags = this->m_faultFlags;
    const U16 version = this->m_firmwareVersion;
    const U32 uptime = this->m_boardUptimeMs;
    this->unLock();

    this->log_ACTIVITY_LO_StatusReport(link, state, flags, version, uptime);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// ----------------------------------------------------------------------
// Parameter update hook
// ----------------------------------------------------------------------

void DriverBoardHandler ::parameterUpdated(FwPrmIdType id) {
    this->lock();
    this->applyParameter(id);
    this->unLock();
}

// ----------------------------------------------------------------------
// Private helpers
// ----------------------------------------------------------------------

U32 DriverBoardHandler ::nowMs() {
    const Fw::Time t = this->getTime();
    return (t.getSeconds() * 1000u) + (t.getUSeconds() / 1000u);
}

bool DriverBoardHandler ::pollSafeMode() {
    if (!this->isConnected_getMode_OutputPort(0)) {
        return false;
    }
    const Components::SystemMode mode = this->getMode_out(0);
    return mode == Components::SystemMode::SAFE_MODE;
}

void DriverBoardHandler ::applyParameter(FwPrmIdType id) {
    Fw::ParamValid valid = Fw::ParamValid::INVALID;
    switch (id) {
        case PARAMID_PULSE_DURATION_MS: {
            const U16 requested = this->paramGet_PULSE_DURATION_MS(valid);
            const bool ok = paramReadable(valid) && (requested >= Proto::PULSE_DURATION_MIN_MS) &&
                            (requested <= Proto::PULSE_DURATION_MAX_MS);
            if (!ok) {
                this->log_WARNING_LO_ParameterRejected(id);
            }
            this->m_pulseDurationMs = ok ? requested : DEFAULT_PULSE_DURATION_MS;
            this->tlmWrite_PulseDurationMs(this->m_pulseDurationMs);
        } break;
        case PARAMID_PULSE_DUTY_PCT: {
            const U8 requested = this->paramGet_PULSE_DUTY_PCT(valid);
            const bool ok = paramReadable(valid) && (requested <= Proto::PULSE_DUTY_MAX_PCT);
            if (!ok) {
                this->log_WARNING_LO_ParameterRejected(id);
            }
            this->m_pulseDutyPct = ok ? requested : DEFAULT_PULSE_DUTY_PCT;
            this->tlmWrite_PulseDutyPct(this->m_pulseDutyPct);
        } break;
        case PARAMID_PULSE_CHANNEL_MASK: {
            const U8 requested = this->paramGet_PULSE_CHANNEL_MASK(valid);
            const bool ok = paramReadable(valid) && (requested >= MIN_PULSE_CHANNEL_MASK) &&
                            (requested <= Proto::CHANNEL_MASK_BITS);
            if (!ok) {
                this->log_WARNING_LO_ParameterRejected(id);
            }
            this->m_pulseChannelMask = ok ? requested : DEFAULT_PULSE_CHANNEL_MASK;
        } break;
        case PARAMID_LINK_TIMEOUT_MS: {
            const U16 requested = this->paramGet_LINK_TIMEOUT_MS(valid);
            const bool ok =
                paramReadable(valid) && (requested >= MIN_LINK_TIMEOUT_MS) && (requested <= MAX_LINK_TIMEOUT_MS);
            if (!ok) {
                this->log_WARNING_LO_ParameterRejected(id);
            }
            this->m_linkTimeoutMs = ok ? requested : DEFAULT_LINK_TIMEOUT_MS;
            this->tlmWrite_LinkTimeoutMs(this->m_linkTimeoutMs);
        } break;
        case PARAMID_HK_INTERVAL_S: {
            const U8 requested = this->paramGet_HK_INTERVAL_S(valid);
            const bool readable = paramReadable(valid);
            // RunInterval::effective enforces the same 1..60 range as the fpp.
            const U8 effective = RunInterval::effective(requested, readable);
            if (!readable || (effective != requested)) {
                this->log_WARNING_LO_ParameterRejected(id);
            }
            this->m_hkIntervalS = effective;
            this->tlmWrite_HkIntervalS(this->m_hkIntervalS);
        } break;
        default:
            break;
    }
}

void DriverBoardHandler ::applyAllParameters() {
    this->applyParameter(PARAMID_PULSE_DURATION_MS);
    this->applyParameter(PARAMID_PULSE_DUTY_PCT);
    this->applyParameter(PARAMID_PULSE_CHANNEL_MASK);
    this->applyParameter(PARAMID_LINK_TIMEOUT_MS);
    this->applyParameter(PARAMID_HK_INTERVAL_S);
}

FwSizeType DriverBoardHandler ::buildHostFrame(DriverBoardLink::HostFrame which, U32 hostTickMs, U8* out) {
    size_t length = 0;
    switch (which) {
        case DriverBoardLink::HostFrame::DISARM: {
            Proto::Disarm msg;
            length = Proto::encodeMessage(msg, this->m_txSeq++, out, Proto::MAX_FRAME);
        } break;
        case DriverBoardLink::HostFrame::HK_REQUEST: {
            Proto::HkRequest msg;
            length = Proto::encodeMessage(msg, this->m_txSeq++, out, Proto::MAX_FRAME);
        } break;
        case DriverBoardLink::HostFrame::HEARTBEAT: {
            Proto::Heartbeat msg;
            msg.hostTickMs = hostTickMs;
            length = Proto::encodeMessage(msg, this->m_txSeq++, out, Proto::MAX_FRAME);
        } break;
        case DriverBoardLink::HostFrame::NONE:
        default:
            break;
    }
    return static_cast<FwSizeType>(length);
}

void DriverBoardHandler ::sendFrame(U8* frame, FwSizeType length) {
    if (length == 0) {
        return;
    }
    // $send is synchronous and the driver copies byte-by-byte, so the
    // caller's stack array is free again on return.
    Fw::Buffer buffer(frame, length);
    (void)this->uartSend_out(0, buffer);
}

void DriverBoardHandler ::handleFrame(const Proto::Frame& frame, U32 receivedMs) {
    if (!Proto::isKnownType(frame.type)) {
        this->dropFrame(REJECT_UNKNOWN_TYPE);
        return;
    }
    if ((frame.type & Proto::BOARD_TO_HOST_BIT) == 0) {
        // A host-type frame (a TX/RX loopback): counted by the parser as
        // received, not a board frame, nothing to do.
        return;
    }
    switch (static_cast<Proto::Type>(frame.type)) {
        case Proto::Type::ACK: {
            Proto::Ack ack;
            if (!Proto::unpack(frame, ack)) {
                this->dropFrame(REJECT_BAD_PAYLOAD);
                return;
            }
            this->noteValidFrame(receivedMs, this->m_firmwareVersion);
            if (ack.ackedType == static_cast<U8>(Proto::Type::ARM)) {
                if (this->m_link.isArmPending()) {
                    if (this->m_link.ackArm(ack.status == static_cast<U8>(Proto::AckStatus::OK))) {
                        this->log_ACTIVITY_HI_Armed();
                        this->setDriverState(DriverState::ARMED);
                    } else {
                        this->log_WARNING_LO_CommandRefused(static_cast<U8>(Proto::Type::ARM),
                                                            RefuseReason::BOARD_REFUSED);
                    }
                }
            } else if ((ack.ackedType == static_cast<U8>(Proto::Type::PULSE)) &&
                       (ack.status != static_cast<U8>(Proto::AckStatus::OK))) {
                this->log_WARNING_LO_PulseRefused(ack.status);
                this->log_WARNING_LO_CommandRefused(static_cast<U8>(Proto::Type::PULSE), RefuseReason::BOARD_REFUSED);
            }
        } break;
        case Proto::Type::HK: {
            Proto::Hk hk;
            if (!Proto::unpack(frame, hk)) {
                this->dropFrame(REJECT_BAD_PAYLOAD);
                return;
            }
            this->noteValidFrame(receivedMs, this->m_firmwareVersion);
            this->tlmWrite_CoilCurrent0(Proto::currentAmps(hk.currentMa[0]));
            this->tlmWrite_CoilCurrent1(Proto::currentAmps(hk.currentMa[1]));
            this->tlmWrite_CoilCurrent2(Proto::currentAmps(hk.currentMa[2]));
            this->tlmWrite_CoilTemperature0(Proto::temperatureC(hk.tempDeciC[0]));
            this->tlmWrite_CoilTemperature1(Proto::temperatureC(hk.tempDeciC[1]));
            this->tlmWrite_PwmDuty0(hk.dutyPct[0]);
            this->tlmWrite_PwmDuty1(hk.dutyPct[1]);
            this->tlmWrite_PwmDuty2(hk.dutyPct[2]);
            this->m_faultFlags = hk.faultFlags;
            this->tlmWrite_FaultFlags(this->m_faultFlags);
            this->m_boardUptimeMs = hk.uptimeMs;
            this->tlmWrite_BoardUptime(this->m_boardUptimeMs);
            this->m_driverState = boardStateToDriverState(hk.state);
            this->tlmWrite_DriverState(this->m_driverState);
            if ((hk.state == static_cast<U8>(Proto::BoardState::FAULT)) && this->m_link.disarm()) {
                this->log_ACTIVITY_HI_Disarmed(DisarmReason::BOARD_FAULT);
                this->m_link.requestDisarmFrame();
            }
        } break;
        case Proto::Type::PONG: {
            Proto::Pong pong;
            if (!Proto::unpack(frame, pong)) {
                this->dropFrame(REJECT_BAD_PAYLOAD);
                return;
            }
            this->m_firmwareVersion = pong.firmwareVersion;
            this->noteValidFrame(receivedMs, this->m_firmwareVersion);
            this->tlmWrite_FirmwareVersion(this->m_firmwareVersion);
            this->log_ACTIVITY_LO_PongReceived(pong.firmwareVersion, pong.protocolVersion);
        } break;
        case Proto::Type::SAMPLE: {
            Proto::Sample sample;
            if (!Proto::unpack(frame, sample)) {
                this->dropFrame(REJECT_BAD_PAYLOAD);
                return;
            }
            this->noteValidFrame(receivedMs, this->m_firmwareVersion);
            this->m_samplesReceived++;
            this->tlmWrite_SamplesReceived(this->m_samplesReceived);
            // The A9 consumer is passive on this same thread; unconnected
            // this cycle, so the sample is counted and dropped at zero cost.
            if (this->isConnected_sampleOut_OutputPort(0)) {
                this->sampleOut_out(0, sample.tMs, sample.currentMa[0], sample.currentMa[1], sample.currentMa[2],
                                    sample.dutyPct[0], sample.dutyPct[1], sample.dutyPct[2]);
            }
        } break;
        case Proto::Type::FAULT: {
            Proto::Fault fault;
            if (!Proto::unpack(frame, fault)) {
                this->dropFrame(REJECT_BAD_PAYLOAD);
                return;
            }
            this->noteValidFrame(receivedMs, this->m_firmwareVersion);
            this->log_WARNING_HI_BoardFault(fault.faultFlags, fault.value);
            this->m_faultFlags = fault.faultFlags;
            this->tlmWrite_FaultFlags(this->m_faultFlags);
            if ((fault.faultFlags != 0) && this->m_link.disarm()) {
                this->log_ACTIVITY_HI_Disarmed(DisarmReason::BOARD_FAULT);
                this->setDriverState(DriverState::DISARMED);
                this->m_link.requestDisarmFrame();
            }
        } break;
        default:
            // Known host-to-board types were filtered above.
            break;
    }
}

void DriverBoardHandler ::dropFrame(U8 reason) {
    this->m_droppedFrames++;
    this->log_WARNING_LO_FrameRejected(reason);
}

void DriverBoardHandler ::noteValidFrame(U32 receivedMs, U16 versionForEvent) {
    if (this->m_link.frameReceived(receivedMs)) {
        this->log_ACTIVITY_HI_LinkUp(versionForEvent);
        this->tlmWrite_LinkState(LinkState::UP);
    }
}

void DriverBoardHandler ::setDriverState(DriverState::T state) {
    this->m_driverState = state;
    this->tlmWrite_DriverState(state);
}

void DriverBoardHandler ::publishCounters() {
    const Proto::Stats& stats = this->m_parser.stats();
    if (stats.accepted != this->m_tlmFramesReceived) {
        this->m_tlmFramesReceived = stats.accepted;
        this->tlmWrite_FramesReceived(this->m_tlmFramesReceived);
    }
    const U32 rejectedTotal = stats.rejected + this->m_droppedFrames;
    const U16 rejected = (rejectedTotal > UINT16_MAX) ? static_cast<U16>(UINT16_MAX) : static_cast<U16>(rejectedTotal);
    if (rejected != this->m_tlmFramesRejected) {
        this->m_tlmFramesRejected = rejected;
        this->tlmWrite_FramesRejected(this->m_tlmFramesRejected);
    }
    // The parser's last-reject reason is logged here, once per stretch: the
    // counter only moves when a new stretch of undeliverable bytes ended.
    if (stats.rejected != this->m_lastParserRejected) {
        this->m_lastParserRejected = stats.rejected;
        this->log_WARNING_LO_FrameRejected(static_cast<U8>(stats.lastReject));
    }
}

}  // namespace Components
