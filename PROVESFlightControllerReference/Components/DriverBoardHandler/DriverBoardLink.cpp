// ======================================================================
// \title  DriverBoardLink.cpp
// \brief  Pure link and arming state machine for the driver-board handler.
// ======================================================================

#include "PROVESFlightControllerReference/Components/DriverBoardHandler/DriverBoardLink.hpp"

namespace Components {

DriverBoardLink::DriverBoardLink()
    : m_driverReady(false),
      m_link(Link::DOWN),
      m_armed(false),
      m_armPending(false),
      m_disarmFramePending(false),
      m_lastValidFrameMs(0),
      m_linkTimeouts(0) {}

DriverBoardLink::HostFrame DriverBoardLink::driverReady() {
    this->m_driverReady = true;
    return HostFrame::DISARM;
}

bool DriverBoardLink::isDriverReady() const {
    return this->m_driverReady;
}

bool DriverBoardLink::frameReceived(uint32_t nowMs) {
    this->m_lastValidFrameMs = nowMs;
    if (this->m_link == Link::UP) {
        return false;
    }
    this->m_link = Link::UP;
    return true;
}

DriverBoardLink::TickResult DriverBoardLink::tick(const TickInput& in) {
    TickResult result;
    result.frame = HostFrame::NONE;
    result.disarm = DisarmCause::NONE;
    result.linkLost = false;
    result.silentMs = 0;

    if (!this->m_driverReady) {
        return result;
    }

    // Link supervision first: silence is measured on this tick only, with
    // strict "greater than" so a frame that arrived exactly one timeout ago
    // still counts. Resolution is one tick (03-design.md 3.5).
    if (this->m_link == Link::UP) {
        const uint32_t silentMs = in.nowMs - this->m_lastValidFrameMs;
        if (silentMs > in.linkTimeoutMs) {
            this->m_link = Link::DOWN;
            this->m_armPending = false;
            if (this->m_linkTimeouts < UINT16_MAX) {
                this->m_linkTimeouts++;
            }
            result.linkLost = true;
            result.silentMs = silentMs;
            if (this->disarm()) {
                result.disarm = DisarmCause::LINK_LOST;
            }
        }
    }

    // Safe mode: the board is being depowered by ModeManager; record the
    // disarm and tell it so anyway, for the record and in case power stays.
    if (in.safeMode && this->disarm()) {
        result.disarm = DisarmCause::SAFE_MODE;
        this->m_disarmFramePending = true;
    }

    // Exactly one host frame per tick: a pending DISARM takes the slot,
    // otherwise HK_REQUEST when the cadence is due, else HEARTBEAT.
    if (this->m_disarmFramePending) {
        this->m_disarmFramePending = false;
        result.frame = HostFrame::DISARM;
    } else if (in.hkDue) {
        result.frame = HostFrame::HK_REQUEST;
    } else {
        result.frame = HostFrame::HEARTBEAT;
    }
    return result;
}

void DriverBoardLink::armSent() {
    this->m_armPending = true;
}

bool DriverBoardLink::isArmPending() const {
    return this->m_armPending;
}

bool DriverBoardLink::ackArm(bool statusOk) {
    if (!this->m_armPending) {
        return false;
    }
    this->m_armPending = false;
    if (statusOk && (this->m_link == Link::UP)) {
        this->m_armed = true;
        return true;
    }
    return false;
}

bool DriverBoardLink::disarm() {
    this->m_armPending = false;
    const bool wasArmed = this->m_armed;
    this->m_armed = false;
    return wasArmed;
}

void DriverBoardLink::requestDisarmFrame() {
    this->m_disarmFramePending = true;
}

bool DriverBoardLink::isArmed() const {
    return this->m_armed;
}

DriverBoardLink::Link DriverBoardLink::link() const {
    return this->m_link;
}

uint32_t DriverBoardLink::lastValidFrameMs() const {
    return this->m_lastValidFrameMs;
}

uint16_t DriverBoardLink::linkTimeouts() const {
    return this->m_linkTimeouts;
}

}  // namespace Components
