// ======================================================================
// \title  DriverBoardLink.hpp
// \brief  Pure link and arming state machine for the driver-board handler.
//
// This header intentionally contains NO F Prime / Svc / Zephyr includes so it
// can be compiled into host (gtest) unit tests. It uses only <cstdint>.
//
// Two coupled machines (plan 03-design.md 3.5):
//
//                 valid board frame                  silence > linkTimeoutMs
//   LINK_DOWN ─────────────────────► LINK_UP ─────────────────────────► LINK_DOWN
//                                                                       (DISARMED forced)
//
//   DISARMED ──ARM sent + ACK(OK)──► ARMED
//   ARMED ──disarm(): DISARM/ABORT/SAFE_MODE/LINK_DOWN/board fault──► DISARMED
//
// The handler feeds it: driverReady() when the UART driver is configured,
// frameReceived(nowMs) for every fully decoded board frame, tick() once per
// 1 Hz tick with the current time, the safe-mode poll and the housekeeping
// cadence. tick() answers with the transitions that happened and the one host
// frame to send this tick. It never sends anything itself and holds no
// pointer to the handler, which keeps it deterministic on the host.
//
// Time is a uint32_t millisecond counter; differences use unsigned wrap
// arithmetic, so the 49.7-day rollover is harmless.
// ======================================================================

#ifndef Components_DriverBoardHandler_DriverBoardLink_HPP
#define Components_DriverBoardHandler_DriverBoardLink_HPP

#include <cstdint>

namespace Components {

class DriverBoardLink {
  public:
    //! Link supervision state (mirrors the fpp LinkState enum values).
    enum class Link : uint8_t { DOWN = 0, UP = 1 };

    //! The host frame the handler must send after a tick or a ready signal.
    enum class HostFrame : uint8_t { NONE = 0, DISARM = 1, HK_REQUEST = 2, HEARTBEAT = 3 };

    //! Why tick() left ARMED, if it did.
    enum class DisarmCause : uint8_t { NONE = 0, LINK_LOST = 1, SAFE_MODE = 2 };

    //! Inputs to one 1 Hz tick.
    struct TickInput {
        uint32_t nowMs;          //!< current time, ms
        uint16_t linkTimeoutMs;  //!< effective LINK_TIMEOUT_MS
        bool safeMode;           //!< ModeManager reports SAFE_MODE
        bool hkDue;              //!< RunInterval::due(HK_INTERVAL_S) for this tick
    };

    //! What one tick decided.
    struct TickResult {
        HostFrame frame;     //!< the one host frame to send (NONE before the driver is ready)
        DisarmCause disarm;  //!< set when this tick left ARMED
        bool linkLost;       //!< the link went UP -> DOWN on this tick
        uint32_t silentMs;   //!< silence that caused linkLost, ms
    };

    DriverBoardLink();

    //! The UART driver is configured. Returns the frame to send now (DISARM).
    HostFrame driverReady();

    //! True after driverReady().
    bool isDriverReady() const;

    //! A fully decoded board frame arrived at nowMs. Returns true when the link
    //! went DOWN -> UP on this frame.
    bool frameReceived(uint32_t nowMs);

    //! One 1 Hz tick: link supervision, safe-mode disarm, frame choice.
    TickResult tick(const TickInput& in);

    //! An ARM frame was sent; the next ACK(ARM) decides.
    void armSent();

    //! True between armSent() and the ACK (or a disarm / link loss).
    bool isArmPending() const;

    //! ACK(ARM) arrived. Returns true when the handler is now ARMED (the ACK
    //! was expected and its status was OK). An unsolicited ACK never arms.
    bool ackArm(bool statusOk);

    //! Leave ARMED for any reason. Returns true if the handler was ARMED.
    bool disarm();

    //! Ask the next tick to send DISARM instead of its heartbeat.
    void requestDisarmFrame();

    bool isArmed() const;
    Link link() const;
    uint32_t lastValidFrameMs() const;
    uint16_t linkTimeouts() const;

  private:
    bool m_driverReady;
    Link m_link;
    bool m_armed;
    bool m_armPending;
    bool m_disarmFramePending;
    uint32_t m_lastValidFrameMs;
    uint16_t m_linkTimeouts;
};

}  // namespace Components

#endif
