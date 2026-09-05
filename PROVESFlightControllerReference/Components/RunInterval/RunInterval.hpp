// ======================================================================
// \title  RunInterval.hpp
// \brief  Header-only tick decimator shared by the 1 Hz telemetry sources.
//
// This header intentionally contains NO F Prime / Svc / Zephyr includes so it
// can be compiled into host (gtest) unit tests. It uses only <cstdint>.
//
// A component that is scheduled on a rate group calls due() once per tick and
// executes its body only when due() returns true. The first call ever returns
// true, so the first tick after boot always runs, exactly as an ungated
// handler does today; with an interval of 1 every tick runs, which makes the
// default configuration bit-identical to the ungated behaviour.
//
// No dynamic allocation: the whole state is one flag and one counter.
// ======================================================================

#ifndef Components_RunInterval_RunInterval_HPP
#define Components_RunInterval_RunInterval_HPP

#include <cstdint>

namespace Components {

//! Smallest, and default, collection interval in seconds (= every 1 Hz tick).
constexpr uint8_t DEFAULT_INTERVAL_S = 1;
//! Largest collection interval a parameter may request, in seconds.
constexpr uint8_t MAX_INTERVAL_S = 60;

//! Decimates a periodic tick down to one execution every N ticks.
class RunInterval {
  public:
    RunInterval() : m_started(false), m_ticksSinceRun(0) {}

    //! Advance one tick and report whether this tick should execute.
    //!
    //! The first call ever returns true. Afterwards this returns true when at
    //! least intervalTicks ticks have elapsed since the last true result. An
    //! intervalTicks of 0 is treated as 1 (run every tick), so a caller that
    //! has not validated its parameter still behaves as it does today.
    //!
    //! A decreased interval takes effect on the next tick; an increased
    //! interval extends the gap currently in progress.
    bool due(uint8_t intervalTicks) {
        const uint8_t interval = (intervalTicks == 0) ? DEFAULT_INTERVAL_S : intervalTicks;
        if (!this->m_started) {
            this->m_started = true;
            this->m_ticksSinceRun = 0;
            return true;
        }
        if (this->m_ticksSinceRun < UINT8_MAX) {
            this->m_ticksSinceRun = static_cast<uint8_t>(this->m_ticksSinceRun + 1);
        }
        if (this->m_ticksSinceRun >= interval) {
            this->m_ticksSinceRun = 0;
            return true;
        }
        return false;
    }

    //! Map a parameter reading onto the interval that will actually be used.
    //!
    //! Returns requested when it is valid and within [1, 60]; otherwise
    //! returns the 1 s default. Callers treat a changed return value as the
    //! signal to report a rejection.
    static uint8_t effective(uint8_t requested, bool valid) {
        if (valid && (requested >= DEFAULT_INTERVAL_S) && (requested <= MAX_INTERVAL_S)) {
            return requested;
        }
        return DEFAULT_INTERVAL_S;
    }

  private:
    //! False until the first due() call, which always executes.
    bool m_started;
    //! Ticks elapsed since the last executed tick.
    uint8_t m_ticksSinceRun;
};

}  // namespace Components

#endif
