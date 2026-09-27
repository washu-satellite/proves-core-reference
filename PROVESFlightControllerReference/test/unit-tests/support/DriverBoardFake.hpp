// ======================================================================
// \title  DriverBoardFake.hpp (host-test support)
// \brief  A fake STM32 driver board that answers the wire protocol.
//
// The executable form of the board side of the spec
// (Components/DriverBoardProtocol/docs/sdd.md, "Wire protocol"): it parses
// host frames with the real Parser, keeps the board's own ARMED / fault
// state, answers ACK / HK (23 bytes) / PONG per message, fires the 3 s
// host-timeout failsafe, and pushes its reply bytes into the handler's
// guarded uartRecv in chunks of at most 64 bytes, which is what
// Zephyr.ZephyrUartDriver delivers per 50 Hz tick (SERIAL_BUFFER_SIZE).
//
// Knobs let a test make the board refuse ARM, stay silent (unplugged), skip
// HK answers, or override the HK state byte. Tests never read expected
// values from the handler; the fake is driven by the spec tables only.
//
// F Prime free: it sees the handler only through the recorder stub base.
// ======================================================================

#ifndef UnitTestSupport_DriverBoardFake_HPP
#define UnitTestSupport_DriverBoardFake_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

#include "FpTypesStub.hpp"
#include "Fw/Buffer/Buffer.hpp"
#include "PROVESFlightControllerReference/Components/DriverBoardHandler/DriverBoardHandlerComponentAc.hpp"
#include "PROVESFlightControllerReference/Components/DriverBoardProtocol/DriverBoardMessages.hpp"
#include "PROVESFlightControllerReference/Components/DriverBoardProtocol/DriverBoardProtocol.hpp"

namespace UnitTestSupport {

class DriverBoardFake {
  public:
    //! Board failsafe: no valid host frame for this long -> coils off,
    //! DISARMED, HOST_TIMEOUT flag, one FAULT frame (02 "Timing and failsafe").
    static constexpr U32 HOST_TIMEOUT_MS = 3000;
    //! Bytes per delivered buffer: ZephyrUartDriver SERIAL_BUFFER_SIZE.
    static constexpr size_t CHUNK_SIZE = 64;

    DriverBoardFake() : hk(), hostParser(), m_seq(0) {
        this->hk.currentMa[0] = 0;
        this->hk.currentMa[1] = 0;
        this->hk.currentMa[2] = 0;
        this->hk.tempDeciC[0] = 0;
        this->hk.tempDeciC[1] = 0;
        this->hk.dutyPct[0] = 0;
        this->hk.dutyPct[1] = 0;
        this->hk.dutyPct[2] = 0;
        this->hk.state = 0;
        this->hk.faultFlags = 0;
        this->hk.uptimeMs = 0;
        this->hk.boardTickMs = 0;
    }

    // ---- knobs ----
    U16 firmwareVersion = 0x0102;
    //! ACK status answered to ARM (0 OK; 2 REFUSED_FAULT makes the board refuse).
    U8 armStatus = 0;
    //! Answer HK_REQUEST with an HK frame.
    bool answerHk = true;
    //! Drop every reply: the board is unplugged or unpowered.
    bool silent = false;
    //! Non-zero: HK.state carries this byte instead of the fake's own state.
    U8 stateOverride = 0;

    // ---- board state, public so a test can set HK contents ----
    //! Values answered in HK; state and faultFlags are filled by the fake.
    Components::DriverBoardProtocol::Hk hk;
    bool armed = false;
    U8 faultFlags = 0;
    bool failsafeFired = false;
    bool anyHostFrame = false;
    U32 lastHostFrameMs = 0;

    // ---- what the board saw ----
    Components::DriverBoardProtocol::Parser hostParser;
    U32 heartbeats = 0;
    U32 hkRequests = 0;
    U32 arms = 0;
    U32 disarms = 0;
    U32 pulses = 0;
    U32 aborts = 0;
    U32 pings = 0;
    U32 others = 0;
    std::vector<U32> heartbeatTicks;
    std::vector<Components::DriverBoardProtocol::Pulse> pulsesReceived;

    //! Feed one host transmission (the bytes of one uartSend call).
    void onHostBytes(const std::vector<U8>& bytes, U32 nowMs) {
        for (size_t i = 0; i < bytes.size(); i++) {
            if (this->hostParser.feed(bytes[i])) {
                this->handleHostFrame(this->hostParser.frame(), nowMs);
            }
        }
    }

    //! Board clock tick: the host-timeout failsafe.
    void tick(U32 nowMs) {
        if (this->anyHostFrame && !this->failsafeFired && ((nowMs - this->lastHostFrameMs) > HOST_TIMEOUT_MS)) {
            this->failsafeFired = true;
            this->armed = false;
            this->faultFlags = static_cast<U8>(this->faultFlags | Components::DriverBoardProtocol::FAULT_HOST_TIMEOUT);
            Components::DriverBoardProtocol::Fault fault;
            fault.faultFlags = this->faultFlags;
            fault.value = 0;
            this->queue(fault);
        }
    }

    //! Queue one board -> host message for the next deliver().
    template <class M>
    void queue(const M& msg) {
        U8 frame[Components::DriverBoardProtocol::MAX_FRAME];
        const size_t n = Components::DriverBoardProtocol::encodeMessage(msg, this->m_seq++, frame, sizeof(frame));
        this->m_pending.insert(this->m_pending.end(), frame, frame + n);
    }

    //! Queue raw bytes (garbage, a truncated frame, an unknown TYPE).
    void queueRaw(const std::vector<U8>& bytes) {
        this->m_pending.insert(this->m_pending.end(), bytes.begin(), bytes.end());
    }

    size_t pendingBytes() const { return this->m_pending.size(); }

    //! Push every pending byte into the handler's guarded uartRecv in
    //! <= CHUNK_SIZE buffers, one distinct Fw::Buffer per chunk. Returns the
    //! number of buffers delivered.
    size_t deliver(Components::DriverBoardHandlerComponentBase& handler) {
        size_t delivered = 0;
        size_t offset = 0;
        while (offset < this->m_pending.size()) {
            const size_t n =
                ((this->m_pending.size() - offset) > CHUNK_SIZE) ? CHUNK_SIZE : (this->m_pending.size() - offset);
            this->m_delivered.push_back(
                std::vector<U8>(this->m_pending.begin() + static_cast<std::ptrdiff_t>(offset),
                                this->m_pending.begin() + static_cast<std::ptrdiff_t>(offset + n)));
            std::vector<U8>& chunk = this->m_delivered.back();
            Fw::Buffer buffer(chunk.data(), static_cast<Fw::Buffer::SizeType>(chunk.size()));
            handler.uartRecv_guarded(0, buffer, Drv::ByteStreamStatus::OP_OK);
            offset += n;
            delivered++;
        }
        this->m_pending.clear();
        return delivered;
    }

  private:
    void handleHostFrame(const Components::DriverBoardProtocol::Frame& frame, U32 nowMs) {
        namespace P = Components::DriverBoardProtocol;
        if (!P::isKnownType(frame.type) || ((frame.type & P::BOARD_TO_HOST_BIT) != 0)) {
            this->others++;
            return;
        }
        // Every valid host frame resets the failsafe timer.
        this->anyHostFrame = true;
        this->lastHostFrameMs = nowMs;
        this->failsafeFired = false;
        this->faultFlags = static_cast<U8>(this->faultFlags & static_cast<U8>(~P::FAULT_HOST_TIMEOUT));

        switch (static_cast<P::Type>(frame.type)) {
            case P::Type::HEARTBEAT: {
                P::Heartbeat hb;
                if (P::unpack(frame, hb)) {
                    this->heartbeats++;
                    this->heartbeatTicks.push_back(hb.hostTickMs);
                }
            } break;
            case P::Type::HK_REQUEST: {
                this->hkRequests++;
                if (this->answerHk) {
                    P::Hk reply = this->hk;
                    reply.faultFlags = this->faultFlags;
                    if (this->stateOverride != 0) {
                        reply.state = this->stateOverride;
                    } else if (this->faultFlags != 0) {
                        reply.state = static_cast<U8>(P::BoardState::FAULT);
                    } else {
                        reply.state = static_cast<U8>(this->armed ? P::BoardState::ARMED : P::BoardState::DISARMED);
                    }
                    this->reply(reply);
                }
            } break;
            case P::Type::ARM: {
                this->arms++;
                P::Arm arm;
                P::Ack ack;
                ack.ackedType = frame.type;
                if (!P::unpack(frame, arm) || (arm.magic != P::ARM_MAGIC)) {
                    ack.status = static_cast<U8>(P::AckStatus::BAD_ARG);
                } else {
                    ack.status = this->armStatus;
                    if (this->armStatus == static_cast<U8>(P::AckStatus::OK)) {
                        this->armed = true;
                    }
                }
                this->reply(ack);
            } break;
            case P::Type::DISARM: {
                this->disarms++;
                this->armed = false;
                P::Ack ack;
                ack.ackedType = frame.type;
                ack.status = static_cast<U8>(P::AckStatus::OK);
                this->reply(ack);
            } break;
            case P::Type::PULSE: {
                this->pulses++;
                P::Pulse pulse;
                P::Ack ack;
                ack.ackedType = frame.type;
                if (!P::unpack(frame, pulse)) {
                    ack.status = static_cast<U8>(P::AckStatus::BAD_ARG);
                } else if (!this->armed) {
                    ack.status = static_cast<U8>(P::AckStatus::REFUSED_NOT_ARMED);
                } else if (this->faultFlags != 0) {
                    ack.status = static_cast<U8>(P::AckStatus::REFUSED_FAULT);
                } else if ((pulse.durationMs < P::PULSE_DURATION_MIN_MS) ||
                           (pulse.durationMs > P::PULSE_DURATION_MAX_MS) || (pulse.dutyPct > P::PULSE_DUTY_MAX_PCT) ||
                           ((pulse.channelMask & static_cast<U8>(~P::CHANNEL_MASK_BITS)) != 0) ||
                           ((pulse.polarityMask & static_cast<U8>(~P::CHANNEL_MASK_BITS)) != 0)) {
                    ack.status = static_cast<U8>(P::AckStatus::BAD_ARG);
                } else {
                    ack.status = static_cast<U8>(P::AckStatus::OK);
                    this->pulsesReceived.push_back(pulse);
                }
                this->reply(ack);
            } break;
            case P::Type::ABORT: {
                this->aborts++;
                this->armed = false;
                P::Ack ack;
                ack.ackedType = frame.type;
                ack.status = static_cast<U8>(P::AckStatus::OK);
                this->reply(ack);
            } break;
            case P::Type::PING: {
                this->pings++;
                P::Pong pong;
                pong.firmwareVersion = this->firmwareVersion;
                pong.protocolVersion = P::PROTOCOL_VERSION;
                pong.reserved = 0;
                this->reply(pong);
            } break;
            case P::Type::TIME_SYNC:
            case P::Type::STREAM_START:
            case P::Type::STREAM_STOP: {
                this->others++;
                P::Ack ack;
                ack.ackedType = frame.type;
                ack.status = static_cast<U8>(P::AckStatus::OK);
                this->reply(ack);
            } break;
            default:
                this->others++;
                break;
        }
    }

    //! Queue a reply unless the board is silent.
    template <class M>
    void reply(const M& msg) {
        if (!this->silent) {
            this->queue(msg);
        }
    }

    U8 m_seq;
    std::vector<U8> m_pending;
    //! Storage for delivered chunks, so recorded buffer pointers stay valid.
    std::vector<std::vector<U8>> m_delivered;
};

}  // namespace UnitTestSupport

#endif
