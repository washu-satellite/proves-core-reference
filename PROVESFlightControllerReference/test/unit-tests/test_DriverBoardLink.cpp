// ======================================================================
// \title  test_DriverBoardLink.cpp
// \brief  Host unit tests for the pure DriverBoardLink state machine.
//
// Level: Unit. F Prime free: DriverBoardLink includes only <cstdint>.
//
// Claims no requirement (plan 01-scope.md, review amendment 7): the
// observables of DriverBoardHandler-2, -5 and -6 are events and frames, which
// only the component test sees. This file exists for transition coverage
// (plan 03-design.md 3.5) with injected time and mode.
//
// Oracle: the transition diagram in 03-design.md 3.5 and the timing rules in
// 02-protocol.md ("Host supervision"): silence strictly greater than the
// timeout, measured on the tick; DISARMED forced on link loss and safe mode;
// exactly one host frame per tick, DISARM taking the slot when pending.
// ======================================================================

#include <gtest/gtest.h>

#include <cstdint>

#include "PROVESFlightControllerReference/Components/DriverBoardHandler/DriverBoardLink.hpp"

namespace {

using Components::DriverBoardLink;
using Link = DriverBoardLink::Link;
using HostFrame = DriverBoardLink::HostFrame;
using DisarmCause = DriverBoardLink::DisarmCause;

constexpr uint16_t TIMEOUT_MS = 1000;

DriverBoardLink::TickInput tickAt(uint32_t nowMs, bool hkDue = true, bool safeMode = false) {
    DriverBoardLink::TickInput in;
    in.nowMs = nowMs;
    in.linkTimeoutMs = TIMEOUT_MS;
    in.safeMode = safeMode;
    in.hkDue = hkDue;
    return in;
}

//! Bring a fresh link to UP and ARMED at t = 1000 ms.
DriverBoardLink armedLink() {
    DriverBoardLink link;
    link.driverReady();
    link.frameReceived(1000);
    link.armSent();
    EXPECT_TRUE(link.ackArm(true));
    return link;
}

TEST(DriverBoardLink, BootsDownDisarmedAndSendsNothingBeforeReady) {
    DriverBoardLink link;
    EXPECT_EQ(Link::DOWN, link.link());
    EXPECT_FALSE(link.isArmed());
    EXPECT_FALSE(link.isDriverReady());
    EXPECT_EQ(0u, link.linkTimeouts());

    const DriverBoardLink::TickResult r = link.tick(tickAt(1000));
    EXPECT_EQ(HostFrame::NONE, r.frame);
    EXPECT_FALSE(r.linkLost);
    EXPECT_EQ(DisarmCause::NONE, r.disarm);
}

TEST(DriverBoardLink, ReadyAnswersDisarmThenOneFramePerTick) {
    DriverBoardLink link;
    EXPECT_EQ(HostFrame::DISARM, link.driverReady());
    EXPECT_TRUE(link.isDriverReady());
    EXPECT_EQ(HostFrame::HK_REQUEST, link.tick(tickAt(1000, true)).frame);
    EXPECT_EQ(HostFrame::HEARTBEAT, link.tick(tickAt(2000, false)).frame);
    EXPECT_EQ(HostFrame::HEARTBEAT, link.tick(tickAt(3000, false)).frame);
    EXPECT_EQ(HostFrame::HK_REQUEST, link.tick(tickAt(4000, true)).frame);
    EXPECT_EQ(Link::DOWN, link.link());
}

TEST(DriverBoardLink, FirstValidFrameRaisesLinkOnce) {
    DriverBoardLink link;
    link.driverReady();
    EXPECT_TRUE(link.frameReceived(1000));
    EXPECT_EQ(Link::UP, link.link());
    EXPECT_EQ(1000u, link.lastValidFrameMs());
    EXPECT_FALSE(link.frameReceived(1500));
    EXPECT_EQ(1500u, link.lastValidFrameMs());
}

TEST(DriverBoardLink, SilenceStrictlyOverTimeoutDropsLinkExactlyOnce) {
    DriverBoardLink link;
    link.driverReady();
    link.frameReceived(1000);

    DriverBoardLink::TickResult r = link.tick(tickAt(2000));  // silent 1000, not > 1000
    EXPECT_FALSE(r.linkLost);
    EXPECT_EQ(Link::UP, link.link());

    r = link.tick(tickAt(3000));  // silent 2000
    EXPECT_TRUE(r.linkLost);
    EXPECT_EQ(2000u, r.silentMs);
    EXPECT_EQ(Link::DOWN, link.link());
    EXPECT_EQ(1u, link.linkTimeouts());
    EXPECT_EQ(DisarmCause::NONE, r.disarm);  // was not armed
    EXPECT_EQ(HostFrame::HK_REQUEST, r.frame);

    r = link.tick(tickAt(4000));
    EXPECT_FALSE(r.linkLost);
    EXPECT_EQ(1u, link.linkTimeouts());
}

TEST(DriverBoardLink, LinkLossForcesDisarmed) {
    DriverBoardLink link = armedLink();
    EXPECT_TRUE(link.isArmed());
    const DriverBoardLink::TickResult r = link.tick(tickAt(3000));
    EXPECT_TRUE(r.linkLost);
    EXPECT_EQ(DisarmCause::LINK_LOST, r.disarm);
    EXPECT_FALSE(link.isArmed());
    EXPECT_FALSE(link.isArmPending());
}

TEST(DriverBoardLink, SafeModeForcesDisarmedAndSendsDisarm) {
    DriverBoardLink link = armedLink();
    link.frameReceived(1900);
    const DriverBoardLink::TickResult r = link.tick(tickAt(2000, true, true));
    EXPECT_FALSE(r.linkLost);
    EXPECT_EQ(DisarmCause::SAFE_MODE, r.disarm);
    EXPECT_EQ(HostFrame::DISARM, r.frame);
    EXPECT_FALSE(link.isArmed());

    // Already disarmed: safe mode on the next tick is silent and the
    // heartbeat cadence resumes.
    link.frameReceived(2900);
    const DriverBoardLink::TickResult again = link.tick(tickAt(3000, false, true));
    EXPECT_EQ(DisarmCause::NONE, again.disarm);
    EXPECT_EQ(HostFrame::HEARTBEAT, again.frame);
}

TEST(DriverBoardLink, ArmCompletesOnlyOnExpectedAckOk) {
    DriverBoardLink link;
    link.driverReady();
    link.frameReceived(1000);

    EXPECT_FALSE(link.ackArm(true));  // unsolicited ACK never arms
    EXPECT_FALSE(link.isArmed());

    link.armSent();
    EXPECT_TRUE(link.isArmPending());
    EXPECT_FALSE(link.ackArm(false));  // board refused
    EXPECT_FALSE(link.isArmed());
    EXPECT_FALSE(link.isArmPending());

    link.armSent();
    EXPECT_TRUE(link.ackArm(true));
    EXPECT_TRUE(link.isArmed());
    EXPECT_FALSE(link.isArmPending());
}

TEST(DriverBoardLink, DisarmReportsPreviousStateAndClearsPendingArm) {
    DriverBoardLink link = armedLink();
    EXPECT_TRUE(link.disarm());
    EXPECT_FALSE(link.disarm());
    link.armSent();
    EXPECT_FALSE(link.disarm());
    EXPECT_FALSE(link.isArmPending());
}

TEST(DriverBoardLink, RequestedDisarmFrameTakesTheNextTickSlotOnce) {
    DriverBoardLink link;
    link.driverReady();
    link.requestDisarmFrame();
    EXPECT_EQ(HostFrame::DISARM, link.tick(tickAt(1000, true)).frame);
    EXPECT_EQ(HostFrame::HK_REQUEST, link.tick(tickAt(2000, true)).frame);
}

TEST(DriverBoardLink, MillisecondCounterWrapDoesNotDropTheLink) {
    DriverBoardLink link;
    link.driverReady();
    link.frameReceived(UINT32_MAX - 200u);
    const DriverBoardLink::TickResult r = link.tick(tickAt(300u));  // 501 ms later across the wrap
    EXPECT_FALSE(r.linkLost);
    EXPECT_EQ(Link::UP, link.link());
    const DriverBoardLink::TickResult later = link.tick(tickAt(1300u));  // 1501 ms later
    EXPECT_TRUE(later.linkLost);
    EXPECT_EQ(1501u, later.silentMs);
}

}  // namespace
