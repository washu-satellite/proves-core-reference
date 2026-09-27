// ======================================================================
// \title  test_DriverBoardHandler_Component.cpp
// \brief  Host unit tests for the DriverBoardHandler component.
//
// Level: Unit. Runs on the host against the recorder stub in
// support/PROVESFlightControllerReference/Components/DriverBoardHandler/ and
// the fake STM32 in support/DriverBoardFake.hpp; no F Prime or Zephyr code
// is linked (test/unit-tests/README.md).
//
// Requirements verified: DriverBoardHandler-1 .. -9.
//
// Oracle: the wire spec (Components/DriverBoardProtocol/docs/sdd.md, the
// copy of plan 02-protocol.md), the interface tables in plan 03-design.md
// 3.3 (parameter defaults and ranges, command semantics, channel
// conversions) and the transitions in 3.5. Host frames are decoded with the
// real Parser; board replies come from the fake, which speaks the spec.
// Nothing is read back from the code under test.
// ======================================================================

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "DriverBoardFake.hpp"
#include "PROVESFlightControllerReference/Components/DriverBoardHandler/DriverBoardHandler.hpp"

namespace {

namespace Proto = Components::DriverBoardProtocol;
using Components::DisarmReason;
using Components::DriverBoardHandler;
using Components::DriverBoardHandlerComponentBase;
using Components::DriverState;
using Components::LinkState;
using Components::RefuseReason;
using Components::SystemMode;
using UnitTestSupport::DriverBoardFake;

// Opcodes are whatever the dispatcher passes; the stub echoes them back.
constexpr FwOpcodeType OPC_ARM = 0x1001;
constexpr FwOpcodeType OPC_DISARM = 0x1002;
constexpr FwOpcodeType OPC_PULSE = 0x1003;
constexpr FwOpcodeType OPC_ABORT = 0x1004;
constexpr FwOpcodeType OPC_PING = 0x1005;
constexpr FwOpcodeType OPC_GET_STATUS = 0x1006;

// Parameter defaults, DriverBoardHandler.fpp / plan 3.3.
constexpr U16 DEFAULT_PULSE_DURATION_MS = 2000;
constexpr U8 DEFAULT_PULSE_DUTY_PCT = 50;
constexpr U8 DEFAULT_PULSE_CHANNEL_MASK = 0x07;
constexpr U16 DEFAULT_LINK_TIMEOUT_MS = 1000;
constexpr U8 DEFAULT_HK_INTERVAL_S = 1;

// Parameter ids as plain values: gtest takes its operands by reference, and
// the stub's static constexpr members have no out-of-line definition (C++14).
constexpr FwPrmIdType ID_PULSE_DURATION_MS = DriverBoardHandlerComponentBase::PARAMID_PULSE_DURATION_MS;
constexpr FwPrmIdType ID_PULSE_DUTY_PCT = DriverBoardHandlerComponentBase::PARAMID_PULSE_DUTY_PCT;
constexpr FwPrmIdType ID_PULSE_CHANNEL_MASK = DriverBoardHandlerComponentBase::PARAMID_PULSE_CHANNEL_MASK;
constexpr FwPrmIdType ID_LINK_TIMEOUT_MS = DriverBoardHandlerComponentBase::PARAMID_LINK_TIMEOUT_MS;
constexpr FwPrmIdType ID_HK_INTERVAL_S = DriverBoardHandlerComponentBase::PARAMID_HK_INTERVAL_S;

// Wire TYPE bytes, 02-protocol.md.
constexpr U8 TYPE_HEARTBEAT = 0x01;
constexpr U8 TYPE_HK_REQUEST = 0x02;
constexpr U8 TYPE_ARM = 0x03;
constexpr U8 TYPE_DISARM = 0x04;
constexpr U8 TYPE_PULSE = 0x05;
constexpr U8 TYPE_ABORT = 0x06;
constexpr U8 TYPE_PING = 0x07;

//! Decode every host transmission in order with the real parser.
std::vector<Proto::Frame> decodeAll(const std::vector<std::vector<U8>>& sends, size_t from = 0) {
    std::vector<Proto::Frame> frames;
    Proto::Parser parser;
    for (size_t i = from; i < sends.size(); i++) {
        for (size_t j = 0; j < sends[i].size(); j++) {
            if (parser.feed(sends[i][j])) {
                frames.push_back(parser.frame());
            }
        }
    }
    return frames;
}

//! The handler, the fake board and a clock, driven the way the topology
//! drives them: run once per second, every uartSend fed to the board, every
//! board reply pushed back through the guarded uartRecv.
struct Rig {
    DriverBoardHandler handler;
    DriverBoardFake board;
    U32 nowMs;
    size_t consumed;
    U32 cmdSeq;

    Rig() : handler("driverBoardHandler"), board(), nowMs(0), consumed(0), cmdSeq(0) { this->setTime(0); }

    //! The handlers are private overrides; tests reach them through the base
    //! reference exactly as the generated port code does.
    DriverBoardHandlerComponentBase& base() { return this->handler; }

    void setTime(U32 ms) {
        this->nowMs = ms;
        this->handler.now = Fw::Time(ms / 1000u, (ms % 1000u) * 1000u);
    }

    //! Hand every not-yet-seen host frame to the board, then deliver replies.
    void pump() {
        while (this->consumed < this->handler.uartSendCalls.size()) {
            this->board.onHostBytes(this->handler.uartSendCalls[this->consumed], this->nowMs);
            this->consumed++;
        }
        this->board.deliver(this->handler);
    }

    void ready() {
        this->base().uartReady_handler(0);
        this->pump();
    }

    //! Ready with the board not answering: a present board acknowledges the
    //! boot DISARM (spec: DISARM -> ACK), which already raises the link.
    void readySilent() {
        const bool was = this->board.silent;
        this->board.silent = true;
        this->ready();
        this->board.silent = was;
    }

    //! One 1 Hz tick, one second later.
    void tick() {
        this->setTime(this->nowMs + 1000u);
        this->base().run_handler(0, 0);
        this->pump();
        this->board.tick(this->nowMs);
        this->board.deliver(this->handler);
    }

    Fw::CmdResponse::T lastResponse() const { return this->handler.cmdResponses.back().response.value(); }

    Fw::CmdResponse::T arm() {
        this->base().ARM_cmdHandler(OPC_ARM, this->cmdSeq++);
        this->pump();
        return this->lastResponse();
    }
    Fw::CmdResponse::T disarm() {
        this->base().DISARM_cmdHandler(OPC_DISARM, this->cmdSeq++);
        this->pump();
        return this->lastResponse();
    }
    Fw::CmdResponse::T pulse(U8 polarityMask) {
        this->base().PULSE_cmdHandler(OPC_PULSE, this->cmdSeq++, polarityMask);
        this->pump();
        return this->lastResponse();
    }
    Fw::CmdResponse::T abort() {
        this->base().ABORT_cmdHandler(OPC_ABORT, this->cmdSeq++);
        this->pump();
        return this->lastResponse();
    }
    Fw::CmdResponse::T ping() {
        this->base().PING_cmdHandler(OPC_PING, this->cmdSeq++);
        this->pump();
        return this->lastResponse();
    }
    Fw::CmdResponse::T getStatus() {
        this->base().GET_STATUS_cmdHandler(OPC_GET_STATUS, this->cmdSeq++);
        return this->lastResponse();
    }

    //! Ready, one tick (the HK reply raises the link), ARM acknowledged.
    void bringUpArmed() {
        this->ready();
        this->tick();
        ASSERT_EQ(Fw::CmdResponse::OK, this->arm());
        ASSERT_EQ(1u, this->handler.eventsArmed);
    }
};

//! Base-reference view of a bare handler (see Rig::base).
DriverBoardHandlerComponentBase& asBase(DriverBoardHandler& h) {
    return h;
}

//! The locking rule every test ends with: no uartSend/getMode under the
//! lock, balanced lock/unLock, and the lock was actually used.
void expectLockDiscipline(const DriverBoardHandlerComponentBase& h) {
    EXPECT_FALSE(h.actionWhileLocked);
    EXPECT_FALSE(h.lockUnderflow);
    EXPECT_EQ(0, h.lockDepth);
    EXPECT_GT(h.lockCalls, 0u);
}

// ----------------------------------------------------------------------
// DriverBoardHandler-1
// ----------------------------------------------------------------------

TEST(DriverBoardHandlerComponent, BootsDisarmedSendsDisarmThenOneFramePerTick) {
    RecordProperty("verifies", "DriverBoardHandler-1");
    {
        Rig rig;
        rig.board.silent = true;  // no board attached
        rig.ready();
        rig.tick();
        rig.tick();
        rig.tick();

        const std::vector<Proto::Frame> frames = decodeAll(rig.handler.uartSendCalls);
        ASSERT_EQ(4u, rig.handler.uartSendCalls.size());
        ASSERT_EQ(4u, frames.size());
        EXPECT_EQ(TYPE_DISARM, frames[0].type);
        EXPECT_EQ(TYPE_HK_REQUEST, frames[1].type);
        EXPECT_EQ(TYPE_HK_REQUEST, frames[2].type);
        EXPECT_EQ(TYPE_HK_REQUEST, frames[3].type);
        for (size_t i = 0; i < 4; i++) {
            EXPECT_EQ(7u, rig.handler.uartSendCalls[i].size()) << "frame " << i;
            EXPECT_EQ(0u, frames[i].len);
        }
        ASSERT_FALSE(rig.handler.tlmDriverState.empty());
        EXPECT_EQ(DriverState::DISARMED, rig.handler.tlmDriverState.back());
        ASSERT_FALSE(rig.handler.tlmLinkState.empty());
        EXPECT_EQ(LinkState::DOWN, rig.handler.tlmLinkState.back());
        EXPECT_EQ(0u, rig.handler.totalEvents());
        EXPECT_EQ(3u, rig.board.hkRequests);
        EXPECT_EQ(1u, rig.board.disarms);
        expectLockDiscipline(rig.handler);
    }
    {
        Rig rig;
        rig.board.silent = true;
        rig.handler.hkIntervalS = 3;
        rig.base().parameterUpdated(DriverBoardHandlerComponentBase::PARAMID_HK_INTERVAL_S);
        rig.ready();
        rig.tick();
        rig.tick();
        rig.tick();

        const std::vector<Proto::Frame> frames = decodeAll(rig.handler.uartSendCalls);
        ASSERT_EQ(4u, frames.size());
        EXPECT_EQ(TYPE_DISARM, frames[0].type);
        EXPECT_EQ(TYPE_HK_REQUEST, frames[1].type);
        EXPECT_EQ(TYPE_HEARTBEAT, frames[2].type);
        EXPECT_EQ(TYPE_HEARTBEAT, frames[3].type);
        EXPECT_EQ(11u, rig.handler.uartSendCalls[2].size());
        EXPECT_EQ(DriverState::DISARMED, rig.handler.tlmDriverState.back());
        EXPECT_EQ(0u, rig.handler.totalEvents());
        // Heartbeats carry the host tick in ms.
        ASSERT_EQ(2u, rig.board.heartbeatTicks.size());
        EXPECT_EQ(2000u, rig.board.heartbeatTicks[0]);
        EXPECT_EQ(3000u, rig.board.heartbeatTicks[1]);
        expectLockDiscipline(rig.handler);
    }
}

// ----------------------------------------------------------------------
// DriverBoardHandler-2
// ----------------------------------------------------------------------

TEST(DriverBoardHandlerComponent, LinkUpOnFirstFrameAndLostAfterTimeoutOnce) {
    RecordProperty("verifies", "DriverBoardHandler-2");
    Rig rig;
    rig.board.answerHk = false;  // only a PONG will raise the link
    rig.readySilent();
    rig.tick();  // t = 1000, HK_REQUEST unanswered
    EXPECT_TRUE(rig.handler.eventsLinkUp.empty());

    ASSERT_EQ(Fw::CmdResponse::OK, rig.ping());  // PONG at tick 1
    ASSERT_EQ(1u, rig.handler.eventsLinkUp.size());
    EXPECT_EQ(rig.board.firmwareVersion, rig.handler.eventsLinkUp[0]);
    ASSERT_FALSE(rig.handler.tlmLinkState.empty());
    EXPECT_EQ(LinkState::UP, rig.handler.tlmLinkState.back());
    ASSERT_EQ(1u, rig.handler.eventsPongReceived.size());
    EXPECT_EQ(rig.board.firmwareVersion, rig.handler.eventsPongReceived[0].fwVersion);
    EXPECT_EQ(Proto::PROTOCOL_VERSION, rig.handler.eventsPongReceived[0].protoVersion);

    // ceil(1000 / 1000) + 1 = 2 silent ticks
    rig.tick();  // t = 2000: silent 1000 ms, not over the timeout
    EXPECT_TRUE(rig.handler.eventsLinkLost.empty());
    rig.tick();  // t = 3000: silent 2000 ms
    ASSERT_EQ(1u, rig.handler.eventsLinkLost.size());
    EXPECT_EQ(2000u, rig.handler.eventsLinkLost[0]);
    EXPECT_EQ(LinkState::DOWN, rig.handler.tlmLinkState.back());
    ASSERT_EQ(1u, rig.handler.tlmLinkTimeouts.size());
    EXPECT_EQ(1u, rig.handler.tlmLinkTimeouts[0]);
    const U32 eventsAfterLoss = rig.handler.totalEvents();

    rig.tick();  // a second silent tick emits nothing more
    EXPECT_EQ(1u, rig.handler.eventsLinkLost.size());
    EXPECT_EQ(eventsAfterLoss, rig.handler.totalEvents());
    EXPECT_TRUE(rig.handler.eventsDisarmed.empty());  // was never armed
    expectLockDiscipline(rig.handler);
}

// ----------------------------------------------------------------------
// DriverBoardHandler-3
// ----------------------------------------------------------------------

TEST(DriverBoardHandlerComponent, ArmNeedsLinkUpAndNotSafeModeAndCompletesOnAck) {
    RecordProperty("verifies", "DriverBoardHandler-3");
    {
        Rig rig;
        rig.readySilent();

        // Link down
        EXPECT_EQ(Fw::CmdResponse::EXECUTION_ERROR, rig.arm());
        ASSERT_EQ(1u, rig.handler.eventsCommandRefused.size());
        EXPECT_EQ(TYPE_ARM, rig.handler.eventsCommandRefused[0].cmd);
        EXPECT_EQ(RefuseReason::LINK_DOWN, rig.handler.eventsCommandRefused[0].reason);
        EXPECT_EQ(0u, rig.board.arms);

        rig.tick();  // HK reply raises the link
        ASSERT_EQ(1u, rig.handler.eventsLinkUp.size());

        // Link up, SAFE_MODE
        rig.handler.mode = SystemMode::SAFE_MODE;
        EXPECT_EQ(Fw::CmdResponse::EXECUTION_ERROR, rig.arm());
        ASSERT_EQ(2u, rig.handler.eventsCommandRefused.size());
        EXPECT_EQ(RefuseReason::SAFE_MODE, rig.handler.eventsCommandRefused[1].reason);
        EXPECT_EQ(0u, rig.board.arms);

        // Link up, NORMAL: ARM sent, response OK means "sent"
        rig.handler.mode = SystemMode::NORMAL;
        const size_t sendsBefore = rig.handler.uartSendCalls.size();
        EXPECT_EQ(Fw::CmdResponse::OK, rig.arm());
        const std::vector<Proto::Frame> sent = decodeAll(rig.handler.uartSendCalls, sendsBefore);
        ASSERT_EQ(1u, sent.size());
        EXPECT_EQ(TYPE_ARM, sent[0].type);
        ASSERT_EQ(1u, sent[0].len);
        EXPECT_EQ(Proto::ARM_MAGIC, sent[0].payload[0]);
        EXPECT_EQ(1u, rig.board.arms);
        // ACK(ARM, OK) from the fake -> Armed, DriverState ARMED
        EXPECT_EQ(1u, rig.handler.eventsArmed);
        ASSERT_FALSE(rig.handler.tlmDriverState.empty());
        EXPECT_EQ(DriverState::ARMED, rig.handler.tlmDriverState.back());
        EXPECT_EQ(2u, rig.handler.eventsCommandRefused.size());
        expectLockDiscipline(rig.handler);
    }
    {
        // ACK(ARM, status != 0): refused by the board, state stays DISARMED
        Rig rig;
        rig.board.armStatus = static_cast<U8>(Proto::AckStatus::REFUSED_FAULT);
        rig.ready();
        rig.tick();
        EXPECT_EQ(Fw::CmdResponse::OK, rig.arm());  // the frame was sent
        EXPECT_EQ(1u, rig.board.arms);
        EXPECT_EQ(0u, rig.handler.eventsArmed);
        ASSERT_EQ(1u, rig.handler.eventsCommandRefused.size());
        EXPECT_EQ(TYPE_ARM, rig.handler.eventsCommandRefused[0].cmd);
        EXPECT_EQ(RefuseReason::BOARD_REFUSED, rig.handler.eventsCommandRefused[0].reason);
        EXPECT_EQ(DriverState::DISARMED, rig.handler.tlmDriverState.back());
        EXPECT_EQ(Fw::CmdResponse::EXECUTION_ERROR, rig.pulse(0));  // still not armed
        expectLockDiscipline(rig.handler);
    }
}

// ----------------------------------------------------------------------
// DriverBoardHandler-4
// ----------------------------------------------------------------------

TEST(DriverBoardHandlerComponent, PulseNeedsArmedAndUsesCurrentParameters) {
    RecordProperty("verifies", "DriverBoardHandler-4");
    Rig rig;
    rig.ready();
    rig.tick();

    // DISARMED
    EXPECT_EQ(Fw::CmdResponse::EXECUTION_ERROR, rig.pulse(0x02));
    ASSERT_EQ(1u, rig.handler.eventsCommandRefused.size());
    EXPECT_EQ(TYPE_PULSE, rig.handler.eventsCommandRefused[0].cmd);
    EXPECT_EQ(RefuseReason::NOT_ARMED, rig.handler.eventsCommandRefused[0].reason);
    EXPECT_EQ(0u, rig.board.pulses);
    EXPECT_TRUE(rig.handler.eventsPulseStarted.empty());

    // Non-default parameters so the frame provably carries them
    rig.handler.pulseDurationMs = 750;
    rig.handler.pulseDutyPct = 33;
    rig.handler.pulseChannelMask = 0x05;
    rig.base().parameterUpdated(DriverBoardHandlerComponentBase::PARAMID_PULSE_DURATION_MS);
    rig.base().parameterUpdated(DriverBoardHandlerComponentBase::PARAMID_PULSE_DUTY_PCT);
    rig.base().parameterUpdated(DriverBoardHandlerComponentBase::PARAMID_PULSE_CHANNEL_MASK);
    EXPECT_TRUE(rig.handler.eventsParameterRejected.empty());

    ASSERT_EQ(Fw::CmdResponse::OK, rig.arm());
    ASSERT_EQ(1u, rig.handler.eventsArmed);

    const size_t sendsBefore = rig.handler.uartSendCalls.size();
    EXPECT_EQ(Fw::CmdResponse::OK, rig.pulse(0x02));
    const std::vector<Proto::Frame> sent = decodeAll(rig.handler.uartSendCalls, sendsBefore);
    ASSERT_EQ(1u, sent.size());
    EXPECT_EQ(TYPE_PULSE, sent[0].type);
    ASSERT_EQ(1u, rig.board.pulsesReceived.size());
    EXPECT_EQ(750u, rig.board.pulsesReceived[0].durationMs);
    EXPECT_EQ(33u, rig.board.pulsesReceived[0].dutyPct);
    EXPECT_EQ(0x05u, rig.board.pulsesReceived[0].channelMask);
    EXPECT_EQ(0x02u, rig.board.pulsesReceived[0].polarityMask);
    ASSERT_EQ(1u, rig.handler.eventsPulseStarted.size());
    EXPECT_EQ(750u, rig.handler.eventsPulseStarted[0].durationMs);
    EXPECT_EQ(33u, rig.handler.eventsPulseStarted[0].dutyPct);
    EXPECT_EQ(0x05u, rig.handler.eventsPulseStarted[0].channelMask);
    EXPECT_EQ(0x02u, rig.handler.eventsPulseStarted[0].polarityMask);
    ASSERT_EQ(1u, rig.handler.tlmPulsesCommanded.size());
    EXPECT_EQ(1u, rig.handler.tlmPulsesCommanded[0]);
    EXPECT_TRUE(rig.handler.eventsPulseRefused.empty());
    expectLockDiscipline(rig.handler);
}

// ----------------------------------------------------------------------
// DriverBoardHandler-5
// ----------------------------------------------------------------------

TEST(DriverBoardHandlerComponent, LinkLossDisarms) {
    RecordProperty("verifies", "DriverBoardHandler-5");
    Rig rig;
    rig.bringUpArmed();  // last board frame at t = 1000
    EXPECT_EQ(DriverState::ARMED, rig.handler.tlmDriverState.back());

    rig.board.silent = true;  // board unplugged
    rig.tick();               // t = 2000: silent 1000 ms
    EXPECT_TRUE(rig.handler.eventsLinkLost.empty());
    EXPECT_TRUE(rig.handler.eventsDisarmed.empty());
    rig.tick();  // t = 3000: silent 2000 ms > 1000
    ASSERT_EQ(1u, rig.handler.eventsLinkLost.size());
    ASSERT_EQ(1u, rig.handler.eventsDisarmed.size());
    EXPECT_EQ(DisarmReason::LINK_LOST, rig.handler.eventsDisarmed[0]);
    EXPECT_EQ(DriverState::DISARMED, rig.handler.tlmDriverState.back());
    EXPECT_EQ(LinkState::DOWN, rig.handler.tlmLinkState.back());

    EXPECT_EQ(Fw::CmdResponse::EXECUTION_ERROR, rig.pulse(0));
    ASSERT_EQ(1u, rig.handler.eventsCommandRefused.size());
    EXPECT_EQ(RefuseReason::NOT_ARMED, rig.handler.eventsCommandRefused[0].reason);
    expectLockDiscipline(rig.handler);
}

// ----------------------------------------------------------------------
// DriverBoardHandler-6
// ----------------------------------------------------------------------

TEST(DriverBoardHandlerComponent, SafeModeDisarms) {
    RecordProperty("verifies", "DriverBoardHandler-6");
    Rig rig;
    rig.bringUpArmed();
    EXPECT_TRUE(rig.board.armed);

    rig.handler.mode = SystemMode::SAFE_MODE;
    const size_t sendsBefore = rig.handler.uartSendCalls.size();
    rig.tick();
    const std::vector<Proto::Frame> sent = decodeAll(rig.handler.uartSendCalls, sendsBefore);
    ASSERT_EQ(1u, sent.size());
    EXPECT_EQ(TYPE_DISARM, sent[0].type);
    ASSERT_EQ(1u, rig.handler.eventsDisarmed.size());
    EXPECT_EQ(DisarmReason::SAFE_MODE, rig.handler.eventsDisarmed[0]);
    EXPECT_EQ(DriverState::DISARMED, rig.handler.tlmDriverState.back());
    EXPECT_FALSE(rig.board.armed);
    EXPECT_GT(rig.handler.getModeCalls, 0u);

    // Still in safe mode next tick: nothing more, cadence resumes
    rig.tick();
    EXPECT_EQ(1u, rig.handler.eventsDisarmed.size());
    EXPECT_EQ(TYPE_HK_REQUEST, decodeAll(rig.handler.uartSendCalls, sendsBefore + 1)[0].type);
    expectLockDiscipline(rig.handler);
}

// ----------------------------------------------------------------------
// DriverBoardHandler-7
// ----------------------------------------------------------------------

TEST(DriverBoardHandlerComponent, HousekeepingFramesUpdateTelemetry) {
    RecordProperty("verifies", "DriverBoardHandler-7");
    Rig rig;
    rig.ready();
    rig.tick();  // baseline writes and a first HK

    rig.board.hk.currentMa[0] = 1500;
    rig.board.hk.currentMa[1] = -200;
    rig.board.hk.currentMa[2] = 0;
    rig.board.hk.tempDeciC[0] = 251;
    rig.board.hk.tempDeciC[1] = 300;
    rig.board.hk.dutyPct[0] = 50;
    rig.board.hk.dutyPct[1] = -50;
    rig.board.hk.dutyPct[2] = 0;
    rig.board.hk.uptimeMs = 123456;
    rig.board.stateOverride = static_cast<U8>(Proto::BoardState::PULSING);
    rig.board.faultFlags = 0;

    rig.handler.tlmCoilCurrent0.clear();
    rig.handler.tlmCoilCurrent1.clear();
    rig.handler.tlmCoilCurrent2.clear();
    rig.handler.tlmCoilTemperature0.clear();
    rig.handler.tlmCoilTemperature1.clear();
    rig.handler.tlmPwmDuty0.clear();
    rig.handler.tlmPwmDuty1.clear();
    rig.handler.tlmPwmDuty2.clear();
    rig.handler.tlmDriverState.clear();
    rig.handler.tlmFaultFlags.clear();
    rig.handler.tlmBoardUptime.clear();

    rig.tick();  // one HK_REQUEST, one 23-byte HK back

    ASSERT_EQ(1u, rig.handler.tlmCoilCurrent0.size());
    EXPECT_EQ(1.5f, rig.handler.tlmCoilCurrent0[0]);
    ASSERT_EQ(1u, rig.handler.tlmCoilCurrent1.size());
    EXPECT_EQ(-0.2f, rig.handler.tlmCoilCurrent1[0]);
    ASSERT_EQ(1u, rig.handler.tlmCoilCurrent2.size());
    EXPECT_EQ(0.0f, rig.handler.tlmCoilCurrent2[0]);
    ASSERT_EQ(1u, rig.handler.tlmCoilTemperature0.size());
    EXPECT_EQ(25.1f, rig.handler.tlmCoilTemperature0[0]);
    ASSERT_EQ(1u, rig.handler.tlmCoilTemperature1.size());
    EXPECT_EQ(30.0f, rig.handler.tlmCoilTemperature1[0]);
    ASSERT_EQ(1u, rig.handler.tlmPwmDuty0.size());
    EXPECT_EQ(50, rig.handler.tlmPwmDuty0[0]);
    ASSERT_EQ(1u, rig.handler.tlmPwmDuty1.size());
    EXPECT_EQ(-50, rig.handler.tlmPwmDuty1[0]);
    ASSERT_EQ(1u, rig.handler.tlmPwmDuty2.size());
    EXPECT_EQ(0, rig.handler.tlmPwmDuty2[0]);
    ASSERT_EQ(1u, rig.handler.tlmDriverState.size());
    EXPECT_EQ(DriverState::PULSING, rig.handler.tlmDriverState[0]);
    ASSERT_EQ(1u, rig.handler.tlmFaultFlags.size());
    EXPECT_EQ(0u, rig.handler.tlmFaultFlags[0]);
    ASSERT_EQ(1u, rig.handler.tlmBoardUptime.size());
    EXPECT_EQ(123456u, rig.handler.tlmBoardUptime[0]);
    EXPECT_TRUE(rig.handler.eventsFrameRejected.empty());
    expectLockDiscipline(rig.handler);
}

// ----------------------------------------------------------------------
// DriverBoardHandler-8
// ----------------------------------------------------------------------

TEST(DriverBoardHandlerComponent, ParameterValidationFallsBackToDefault) {
    RecordProperty("verifies", "DriverBoardHandler-8");
    using Base = DriverBoardHandlerComponentBase;

    // PULSE_DURATION_MS: 0 and 6000 are outside 50..5000
    for (const U16 bad : {static_cast<U16>(0), static_cast<U16>(6000)}) {
        DriverBoardHandler h("driverBoardHandler");
        h.pulseDurationMs = bad;
        asBase(h).parameterUpdated(Base::PARAMID_PULSE_DURATION_MS);
        ASSERT_EQ(1u, h.tlmPulseDurationMs.size()) << "value " << bad;
        EXPECT_EQ(DEFAULT_PULSE_DURATION_MS, h.tlmPulseDurationMs[0]) << "value " << bad;
        ASSERT_EQ(1u, h.eventsParameterRejected.size()) << "value " << bad;
        EXPECT_EQ(ID_PULSE_DURATION_MS, h.eventsParameterRejected[0]);
    }
    // PULSE_DUTY_PCT: 101 is outside 0..100
    {
        DriverBoardHandler h("driverBoardHandler");
        h.pulseDutyPct = 101;
        asBase(h).parameterUpdated(Base::PARAMID_PULSE_DUTY_PCT);
        ASSERT_EQ(1u, h.tlmPulseDutyPct.size());
        EXPECT_EQ(DEFAULT_PULSE_DUTY_PCT, h.tlmPulseDutyPct[0]);
        ASSERT_EQ(1u, h.eventsParameterRejected.size());
        EXPECT_EQ(ID_PULSE_DUTY_PCT, h.eventsParameterRejected[0]);
    }
    // PULSE_CHANNEL_MASK: 0 and 0x08 are outside 0x01..0x07; the effect is
    // only visible on the wire, so send a PULSE.
    for (const U8 bad : {static_cast<U8>(0), static_cast<U8>(0x08)}) {
        Rig rig;
        rig.bringUpArmed();
        rig.handler.pulseChannelMask = bad;
        rig.base().parameterUpdated(Base::PARAMID_PULSE_CHANNEL_MASK);
        ASSERT_EQ(1u, rig.handler.eventsParameterRejected.size()) << "value " << static_cast<int>(bad);
        EXPECT_EQ(ID_PULSE_CHANNEL_MASK, rig.handler.eventsParameterRejected[0]);
        ASSERT_EQ(Fw::CmdResponse::OK, rig.pulse(0));
        ASSERT_EQ(1u, rig.board.pulsesReceived.size());
        EXPECT_EQ(DEFAULT_PULSE_CHANNEL_MASK, rig.board.pulsesReceived[0].channelMask);
        EXPECT_EQ(DEFAULT_PULSE_DURATION_MS, rig.board.pulsesReceived[0].durationMs);
        EXPECT_EQ(DEFAULT_PULSE_DUTY_PCT, rig.board.pulsesReceived[0].dutyPct);
    }
    // LINK_TIMEOUT_MS: 50 is outside 100..10000
    {
        DriverBoardHandler h("driverBoardHandler");
        h.linkTimeoutMs = 50;
        asBase(h).parameterUpdated(Base::PARAMID_LINK_TIMEOUT_MS);
        ASSERT_EQ(1u, h.tlmLinkTimeoutMs.size());
        EXPECT_EQ(DEFAULT_LINK_TIMEOUT_MS, h.tlmLinkTimeoutMs[0]);
        ASSERT_EQ(1u, h.eventsParameterRejected.size());
        EXPECT_EQ(ID_LINK_TIMEOUT_MS, h.eventsParameterRejected[0]);
    }
    // HK_INTERVAL_S: 0 and 61 are outside 1..60
    for (const U8 bad : {static_cast<U8>(0), static_cast<U8>(61)}) {
        DriverBoardHandler h("driverBoardHandler");
        h.hkIntervalS = bad;
        asBase(h).parameterUpdated(Base::PARAMID_HK_INTERVAL_S);
        ASSERT_EQ(1u, h.tlmHkIntervalS.size()) << "value " << static_cast<int>(bad);
        EXPECT_EQ(DEFAULT_HK_INTERVAL_S, h.tlmHkIntervalS[0]) << "value " << static_cast<int>(bad);
        ASSERT_EQ(1u, h.eventsParameterRejected.size()) << "value " << static_cast<int>(bad);
        EXPECT_EQ(ID_HK_INTERVAL_S, h.eventsParameterRejected[0]);
    }
    // INVALID: an in-range value that could not be read falls back too
    {
        DriverBoardHandler h("driverBoardHandler");
        h.pulseDurationMs = 3000;
        h.paramValidity = Fw::ParamValid::INVALID;
        asBase(h).parameterUpdated(Base::PARAMID_PULSE_DURATION_MS);
        ASSERT_EQ(1u, h.tlmPulseDurationMs.size());
        EXPECT_EQ(DEFAULT_PULSE_DURATION_MS, h.tlmPulseDurationMs[0]);
        ASSERT_EQ(1u, h.eventsParameterRejected.size());
    }
    // Control: in-range values are accepted without an event
    {
        DriverBoardHandler h("driverBoardHandler");
        h.pulseDurationMs = 50;
        h.pulseDutyPct = 100;
        h.linkTimeoutMs = 10000;
        h.hkIntervalS = 60;
        asBase(h).parameterUpdated(Base::PARAMID_PULSE_DURATION_MS);
        asBase(h).parameterUpdated(Base::PARAMID_PULSE_DUTY_PCT);
        asBase(h).parameterUpdated(Base::PARAMID_LINK_TIMEOUT_MS);
        asBase(h).parameterUpdated(Base::PARAMID_HK_INTERVAL_S);
        EXPECT_TRUE(h.eventsParameterRejected.empty());
        EXPECT_EQ(50u, h.tlmPulseDurationMs.back());
        EXPECT_EQ(100u, h.tlmPulseDutyPct.back());
        EXPECT_EQ(10000u, h.tlmLinkTimeoutMs.back());
        EXPECT_EQ(60u, h.tlmHkIntervalS.back());
        expectLockDiscipline(h);
    }
}

// ----------------------------------------------------------------------
// DriverBoardHandler-9
// ----------------------------------------------------------------------

TEST(DriverBoardHandlerComponent, EveryReceivedBufferIsReturned) {
    RecordProperty("verifies", "DriverBoardHandler-9");
    DriverBoardHandler h("driverBoardHandler");

    // A valid PONG, garbage, a truncated frame, an unknown TYPE, an empty
    // buffer and a buffer the driver flagged as failed.
    U8 valid[Proto::MAX_FRAME];
    Proto::Pong pong;
    pong.firmwareVersion = 7;
    pong.protocolVersion = Proto::PROTOCOL_VERSION;
    pong.reserved = 0;
    const size_t validLen = Proto::encodeMessage(pong, 0, valid, sizeof(valid));
    ASSERT_GT(validLen, 0u);
    U8 garbage[5] = {0x01, 0x02, 0x5C, 0x03, 0x04};
    U8 truncated[4] = {valid[0], valid[1], valid[2], valid[3]};
    U8 unknownType[Proto::MAX_FRAME];
    const size_t unknownLen = Proto::encode(0xC3, 1, nullptr, 0, unknownType, sizeof(unknownType));
    ASSERT_GT(unknownLen, 0u);
    U8 empty[1] = {0};
    U8 failed[3] = {0x5C, 0xA1, 0x82};

    Fw::Buffer buffers[6] = {
        Fw::Buffer(valid, validLen),
        Fw::Buffer(garbage, sizeof(garbage)),
        Fw::Buffer(truncated, sizeof(truncated)),
        Fw::Buffer(unknownType, unknownLen),
        Fw::Buffer(empty, 0),
        Fw::Buffer(failed, sizeof(failed)),
    };
    const Drv::ByteStreamStatus::T statuses[6] = {
        Drv::ByteStreamStatus::OP_OK, Drv::ByteStreamStatus::OP_OK, Drv::ByteStreamStatus::OP_OK,
        Drv::ByteStreamStatus::OP_OK, Drv::ByteStreamStatus::OP_OK, Drv::ByteStreamStatus::OTHER_ERROR,
    };
    for (size_t i = 0; i < 6; i++) {
        h.uartRecv_guarded(0, buffers[i], statuses[i]);
    }

    ASSERT_EQ(6u, h.uartRecvReturnCalls.size());
    for (size_t i = 0; i < 6; i++) {
        EXPECT_EQ(buffers[i].getData(), h.uartRecvReturnCalls[i].data) << "buffer " << i;
        EXPECT_EQ(buffers[i].getSize(), h.uartRecvReturnCalls[i].size) << "buffer " << i;
    }
    // Side observations: the valid PONG raised the link, the unknown TYPE
    // was counted, nothing else was sent or acted on.
    EXPECT_EQ(1u, h.eventsLinkUp.size());
    EXPECT_EQ(1u, h.eventsPongReceived.size());
    EXPECT_TRUE(h.uartSendCalls.empty());
    ASSERT_FALSE(h.tlmFramesRejected.empty());
    EXPECT_GE(h.tlmFramesRejected.back(), 1u);
    expectLockDiscipline(h);
}

// ----------------------------------------------------------------------
// Behaviour not tied to a requirement id (coverage of 03-design.md 3.3)
// ----------------------------------------------------------------------

TEST(DriverBoardHandlerComponent, UartRecvNeverSendsAndBoardFaultDisarmsNextTick) {
    Rig rig;
    rig.bringUpArmed();
    const size_t sendsBefore = rig.handler.uartSendCalls.size();

    Proto::Fault fault;
    fault.faultFlags = Proto::FAULT_OVERCURRENT;
    fault.value = -1234;
    rig.board.queue(fault);
    rig.board.deliver(rig.handler);

    // Nothing was sent from inside uartRecv; the disarm is local and the
    // DISARM frame goes out on the next tick.
    EXPECT_EQ(sendsBefore, rig.handler.uartSendCalls.size());
    ASSERT_EQ(1u, rig.handler.eventsBoardFault.size());
    EXPECT_EQ(Proto::FAULT_OVERCURRENT, rig.handler.eventsBoardFault[0].flags);
    EXPECT_EQ(-1234, rig.handler.eventsBoardFault[0].value);
    ASSERT_EQ(1u, rig.handler.eventsDisarmed.size());
    EXPECT_EQ(DisarmReason::BOARD_FAULT, rig.handler.eventsDisarmed[0]);
    EXPECT_EQ(DriverState::DISARMED, rig.handler.tlmDriverState.back());
    EXPECT_EQ(Proto::FAULT_OVERCURRENT, rig.handler.tlmFaultFlags.back());

    rig.tick();
    const std::vector<Proto::Frame> sent = decodeAll(rig.handler.uartSendCalls, sendsBefore);
    ASSERT_EQ(1u, sent.size());
    EXPECT_EQ(TYPE_DISARM, sent[0].type);
    EXPECT_EQ(Fw::CmdResponse::EXECUTION_ERROR, rig.pulse(0));
    expectLockDiscipline(rig.handler);
}

TEST(DriverBoardHandlerComponent, BoardFailsafeAfterThreeSecondsOfHostSilenceReportsFault) {
    Rig rig;
    rig.bringUpArmed();
    // The host stops ticking (no run_handler); the board's own clock runs on.
    rig.board.tick(rig.nowMs + 3000);
    EXPECT_FALSE(rig.board.failsafeFired);
    rig.board.tick(rig.nowMs + 3001);
    EXPECT_TRUE(rig.board.failsafeFired);
    rig.board.deliver(rig.handler);
    ASSERT_EQ(1u, rig.handler.eventsBoardFault.size());
    EXPECT_EQ(Proto::FAULT_HOST_TIMEOUT, rig.handler.eventsBoardFault[0].flags);
    ASSERT_EQ(1u, rig.handler.eventsDisarmed.size());
    EXPECT_EQ(DisarmReason::BOARD_FAULT, rig.handler.eventsDisarmed[0]);
    expectLockDiscipline(rig.handler);
}

TEST(DriverBoardHandlerComponent, SamplesAreCountedAndForwardedOnlyWhenConnected) {
    Rig rig;
    rig.ready();
    Proto::Sample sample;
    sample.tMs = 4242;
    sample.currentMa[0] = 10;
    sample.currentMa[1] = -20;
    sample.currentMa[2] = 30;
    sample.dutyPct[0] = 1;
    sample.dutyPct[1] = -2;
    sample.dutyPct[2] = 3;

    rig.board.queue(sample);
    rig.board.deliver(rig.handler);  // sampleOut unconnected (this cycle's topology)
    ASSERT_EQ(1u, rig.handler.tlmSamplesReceived.size());
    EXPECT_EQ(1u, rig.handler.tlmSamplesReceived[0]);
    EXPECT_TRUE(rig.handler.sampleOutCalls.empty());
    EXPECT_EQ(1u, rig.handler.eventsLinkUp.size());  // a SAMPLE is a valid board frame

    rig.handler.sampleOutConnected = true;
    rig.board.queue(sample);
    rig.board.deliver(rig.handler);
    EXPECT_EQ(2u, rig.handler.tlmSamplesReceived.back());
    ASSERT_EQ(1u, rig.handler.sampleOutCalls.size());
    EXPECT_EQ(4242u, rig.handler.sampleOutCalls[0].boardTimeMs);
    EXPECT_EQ(-20, rig.handler.sampleOutCalls[0].currentMa[1]);
    EXPECT_EQ(-2, rig.handler.sampleOutCalls[0].dutyPct[1]);
    expectLockDiscipline(rig.handler);
}

TEST(DriverBoardHandlerComponent, LargeReplyIsDeliveredInDriverSizedChunksAndParsed) {
    Rig rig;
    rig.readySilent();
    // Ten SAMPLE frames = 200 bytes -> four buffers of <= 64 bytes.
    for (U32 i = 0; i < 10; i++) {
        Proto::Sample sample;
        sample.tMs = i;
        sample.currentMa[0] = 0;
        sample.currentMa[1] = 0;
        sample.currentMa[2] = 0;
        sample.dutyPct[0] = 0;
        sample.dutyPct[1] = 0;
        sample.dutyPct[2] = 0;
        rig.board.queue(sample);
    }
    EXPECT_EQ(200u, rig.board.pendingBytes());
    EXPECT_EQ(4u, rig.board.deliver(rig.handler));
    EXPECT_EQ(4u, rig.handler.uartRecvReturnCalls.size());
    EXPECT_EQ(10u, rig.handler.tlmSamplesReceived.back());
    EXPECT_EQ(10u, rig.handler.tlmFramesReceived.back());
    EXPECT_TRUE(rig.handler.tlmFramesRejected.empty());
    expectLockDiscipline(rig.handler);
}

TEST(DriverBoardHandlerComponent, UnknownTypeAndGarbageAreCountedAsRejected) {
    Rig rig;
    rig.readySilent();
    U8 unknownType[Proto::MAX_FRAME];
    const size_t n = Proto::encode(0xC3, 9, nullptr, 0, unknownType, sizeof(unknownType));
    rig.board.queueRaw(std::vector<U8>(unknownType, unknownType + n));
    rig.board.queueRaw({0xDE, 0xAD, 0xBE, 0xEF});
    rig.board.deliver(rig.handler);
    // One unknown TYPE (reason 4) plus one stretch of garbage (reason 1 SYNC).
    ASSERT_FALSE(rig.handler.tlmFramesRejected.empty());
    EXPECT_EQ(2u, rig.handler.tlmFramesRejected.back());
    ASSERT_EQ(2u, rig.handler.eventsFrameRejected.size());
    EXPECT_EQ(4u, rig.handler.eventsFrameRejected[0]);
    EXPECT_EQ(1u, rig.handler.eventsFrameRejected[1]);
    EXPECT_TRUE(rig.handler.eventsLinkUp.empty());
    expectLockDiscipline(rig.handler);
}

TEST(DriverBoardHandlerComponent, LoopbackOfHostFramesIsReceivedNotRejectedAndLinkStaysDown) {
    // HP-15 loopback smoke: TX1 jumpered to RX1, no board.
    Rig rig;
    rig.board.silent = true;
    rig.ready();
    rig.tick();
    // Echo everything the host sent back into uartRecv.
    for (size_t i = 0; i < rig.handler.uartSendCalls.size(); i++) {
        rig.board.queueRaw(rig.handler.uartSendCalls[i]);
    }
    rig.board.deliver(rig.handler);
    EXPECT_TRUE(rig.handler.tlmFramesRejected.empty());
    ASSERT_FALSE(rig.handler.tlmFramesReceived.empty());
    EXPECT_GE(rig.handler.tlmFramesReceived.back(), 2u);
    EXPECT_EQ(LinkState::DOWN, rig.handler.tlmLinkState.back());
    EXPECT_TRUE(rig.handler.eventsLinkUp.empty());
    expectLockDiscipline(rig.handler);
}

TEST(DriverBoardHandlerComponent, DisarmAbortPingAndStatusCommands) {
    Rig rig;
    EXPECT_EQ(Fw::CmdResponse::EXECUTION_ERROR, rig.ping());  // driver not ready
    ASSERT_EQ(1u, rig.handler.eventsCommandRefused.size());
    EXPECT_EQ(TYPE_PING, rig.handler.eventsCommandRefused[0].cmd);
    EXPECT_EQ(RefuseReason::DRIVER_NOT_READY, rig.handler.eventsCommandRefused[0].reason);

    rig.bringUpArmed();
    size_t before = rig.handler.uartSendCalls.size();
    EXPECT_EQ(Fw::CmdResponse::OK, rig.disarm());
    EXPECT_EQ(TYPE_DISARM, decodeAll(rig.handler.uartSendCalls, before)[0].type);
    ASSERT_EQ(1u, rig.handler.eventsDisarmed.size());
    EXPECT_EQ(DisarmReason::COMMAND, rig.handler.eventsDisarmed[0]);
    EXPECT_FALSE(rig.board.armed);

    ASSERT_EQ(Fw::CmdResponse::OK, rig.arm());
    before = rig.handler.uartSendCalls.size();
    EXPECT_EQ(Fw::CmdResponse::OK, rig.abort());
    EXPECT_EQ(TYPE_ABORT, decodeAll(rig.handler.uartSendCalls, before)[0].type);
    ASSERT_EQ(2u, rig.handler.eventsDisarmed.size());
    EXPECT_EQ(DisarmReason::ABORT, rig.handler.eventsDisarmed[1]);
    EXPECT_EQ(1u, rig.board.aborts);

    EXPECT_EQ(Fw::CmdResponse::OK, rig.ping());
    ASSERT_EQ(1u, rig.handler.eventsPongReceived.size());
    ASSERT_EQ(1u, rig.handler.tlmFirmwareVersion.size());
    EXPECT_EQ(rig.board.firmwareVersion, rig.handler.tlmFirmwareVersion[0]);

    EXPECT_EQ(Fw::CmdResponse::OK, rig.getStatus());
    ASSERT_EQ(1u, rig.handler.eventsStatusReport.size());
    EXPECT_EQ(LinkState::UP, rig.handler.eventsStatusReport[0].link);
    EXPECT_EQ(DriverState::DISARMED, rig.handler.eventsStatusReport[0].state);
    EXPECT_EQ(rig.board.firmwareVersion, rig.handler.eventsStatusReport[0].version);

    EXPECT_EQ(Fw::CmdResponse::VALIDATION_ERROR, rig.pulse(0x08));  // polarity bits 0-2 only
    expectLockDiscipline(rig.handler);
}

TEST(DriverBoardHandlerComponent, PulseRefusedByBoardIsReported) {
    Rig rig;
    rig.bringUpArmed();
    rig.board.armed = false;  // the board lost its arm (e.g. rebooted) without telling
    EXPECT_EQ(Fw::CmdResponse::OK, rig.pulse(0));
    ASSERT_EQ(1u, rig.handler.eventsPulseRefused.size());
    EXPECT_EQ(static_cast<U8>(Proto::AckStatus::REFUSED_NOT_ARMED), rig.handler.eventsPulseRefused[0]);
    ASSERT_EQ(1u, rig.handler.eventsCommandRefused.size());
    EXPECT_EQ(TYPE_PULSE, rig.handler.eventsCommandRefused[0].cmd);
    EXPECT_EQ(RefuseReason::BOARD_REFUSED, rig.handler.eventsCommandRefused[0].reason);
    expectLockDiscipline(rig.handler);
}

TEST(DriverBoardHandlerComponent, DefaultsMatchPlanAndAreTelemeteredOnFirstTick) {
    Rig rig;
    rig.board.silent = true;
    rig.ready();
    rig.tick();
    ASSERT_EQ(1u, rig.handler.tlmPulseDurationMs.size());
    EXPECT_EQ(DEFAULT_PULSE_DURATION_MS, rig.handler.tlmPulseDurationMs[0]);
    ASSERT_EQ(1u, rig.handler.tlmPulseDutyPct.size());
    EXPECT_EQ(DEFAULT_PULSE_DUTY_PCT, rig.handler.tlmPulseDutyPct[0]);
    ASSERT_EQ(1u, rig.handler.tlmLinkTimeoutMs.size());
    EXPECT_EQ(DEFAULT_LINK_TIMEOUT_MS, rig.handler.tlmLinkTimeoutMs[0]);
    ASSERT_EQ(1u, rig.handler.tlmHkIntervalS.size());
    EXPECT_EQ(DEFAULT_HK_INTERVAL_S, rig.handler.tlmHkIntervalS[0]);
    EXPECT_TRUE(rig.handler.eventsParameterRejected.empty());
    expectLockDiscipline(rig.handler);
}

}  // namespace
