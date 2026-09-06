// ======================================================================
// \title  Fw/Time/Time.hpp (host-test stub)
// \brief  Stand-in for the F Prime time header for host unit tests.
//
// Components under test include this header only for Fw::TimeIntervalValue,
// the FPP dictionary struct declared in lib/fprime/Fw/Time/Time.fpp (seconds
// and useconds, both U32). The generated serializable also carries
// serialization support that host tests never exercise, so only the accessors
// used by component logic are mirrored here.
// ======================================================================

#ifndef UnitTestSupport_Fw_Time_Time_HPP
#define UnitTestSupport_Fw_Time_Time_HPP

#include "../../FpTypesStub.hpp"

namespace Fw {

//! Mirrors the generated FPP struct Fw.TimeIntervalValue.
class TimeIntervalValue {
  public:
    TimeIntervalValue() : m_seconds(0), m_useconds(0) {}
    TimeIntervalValue(U32 seconds, U32 useconds) : m_seconds(seconds), m_useconds(useconds) {}

    U32 get_seconds() const { return this->m_seconds; }
    U32 get_useconds() const { return this->m_useconds; }
    void set_seconds(U32 seconds) { this->m_seconds = seconds; }
    void set_useconds(U32 useconds) { this->m_useconds = useconds; }

  private:
    U32 m_seconds;
    U32 m_useconds;
};

}  // namespace Fw

#endif
