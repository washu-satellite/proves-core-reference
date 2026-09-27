// ======================================================================
// \title  Fw/Time/Time.hpp (host-test stub)
// \brief  Minimal Fw::Time / TimeBase stand-ins for host unit tests.
//
// Mirrors the subset of lib/fprime/Fw/Time/Time.hpp and the generated
// TimeBase enum that StartupManager.cpp uses: the four-argument and
// three-argument constructors, set(), the field getters, static add() and
// operator<=. TimeBase values and the store-type widths come from
// lib/fprime/default/config/FpConfig.fpp:79,92,95-101 (TimeBase : U16,
// FwTimeContextStoreType = U8), which fixes the 11-byte serialized layout the
// component persists.
//
// No F Prime serialization is provided: the component packs its own explicit
// little-endian bytes, which is exactly the property the host tests check.
// ======================================================================

#ifndef UnitTestSupport_Fw_Time_Time_HPP
#define UnitTestSupport_Fw_Time_Time_HPP

#include "../../FpTypesStub.hpp"

//! Mirrors the generated TimeBase enum class (global namespace, as generated).
class TimeBase {
  public:
    typedef U16 SerialType;
    enum T { TB_NONE = 0, TB_PROC_TIME = 1, TB_WORKSTATION_TIME = 2, TB_SC_TIME = 3, TB_DONT_CARE = 65535 };
    TimeBase() : e(TB_NONE) {}
    TimeBase(T e1) : e(e1) {}               // NOLINT(runtime/explicit) -- mirrors generated code
    operator T() const { return this->e; }  // NOLINT(runtime/explicit) -- mirrors generated code
    bool operator==(T e1) const { return this->e == e1; }
    bool operator!=(T e1) const { return this->e != e1; }

    T e;
};

namespace Fw {

class Time {
  public:
    enum { SERIALIZED_SIZE = sizeof(FwTimeBaseStoreType) + sizeof(FwTimeContextStoreType) + sizeof(U32) + sizeof(U32) };

    Time() : m_timeBase(TimeBase::TB_NONE), m_context(0), m_seconds(0), m_useconds(0) {}
    Time(U32 seconds, U32 useconds)
        : m_timeBase(TimeBase::TB_NONE), m_context(0), m_seconds(seconds), m_useconds(useconds) {}
    Time(TimeBase timeBase, U32 seconds, U32 useconds)
        : m_timeBase(timeBase), m_context(0), m_seconds(seconds), m_useconds(useconds) {}
    Time(TimeBase timeBase, FwTimeContextStoreType context, U32 seconds, U32 useconds)
        : m_timeBase(timeBase), m_context(context), m_seconds(seconds), m_useconds(useconds) {}

    void set(U32 seconds, U32 useconds) {
        this->m_seconds = seconds;
        this->m_useconds = useconds;
    }
    void set(TimeBase timeBase, U32 seconds, U32 useconds) {
        this->m_timeBase = timeBase;
        this->m_context = 0;
        this->m_seconds = seconds;
        this->m_useconds = useconds;
    }
    void set(TimeBase timeBase, FwTimeContextStoreType context, U32 seconds, U32 useconds) {
        this->m_timeBase = timeBase;
        this->m_context = context;
        this->m_seconds = seconds;
        this->m_useconds = useconds;
    }

    U32 getSeconds() const { return this->m_seconds; }
    U32 getUSeconds() const { return this->m_useconds; }
    TimeBase getTimeBase() const { return this->m_timeBase; }
    FwTimeContextStoreType getContext() const { return this->m_context; }

    bool operator==(const Time& other) const {
        return this->m_seconds == other.m_seconds && this->m_useconds == other.m_useconds &&
               this->m_timeBase == other.m_timeBase.e && this->m_context == other.m_context;
    }
    bool operator!=(const Time& other) const { return !(*this == other); }
    bool operator<(const Time& other) const {
        if (this->m_seconds != other.m_seconds) {
            return this->m_seconds < other.m_seconds;
        }
        return this->m_useconds < other.m_useconds;
    }
    bool operator<=(const Time& other) const { return (*this < other) || (*this == other); }
    bool operator>(const Time& other) const { return other < *this; }
    bool operator>=(const Time& other) const { return !(*this < other); }

    //! Mirrors Fw::Time::add: field-wise addition with microsecond carry.
    static Time add(const Time& a, const Time& b) {
        U32 seconds = a.m_seconds + b.m_seconds;
        U32 useconds = a.m_useconds + b.m_useconds;
        if (useconds >= 1000000u) {
            seconds += 1;
            useconds -= 1000000u;
        }
        return Time(a.m_timeBase, a.m_context, seconds, useconds);
    }

  private:
    TimeBase m_timeBase;
    FwTimeContextStoreType m_context;
    U32 m_seconds;
    U32 m_useconds;
};

//! Mirrors the generated Fw::TimeValue struct (FpConfig.fpp TimeValue).
class TimeValue {
  public:
    TimeValue() : m_timeBase(TimeBase::TB_NONE), m_context(0), m_seconds(0), m_useconds(0) {}
    TimeValue(TimeBase timeBase, FwTimeContextStoreType context, U32 seconds, U32 useconds)
        : m_timeBase(timeBase), m_context(context), m_seconds(seconds), m_useconds(useconds) {}
    TimeBase get_timeBase() const { return this->m_timeBase; }
    FwTimeContextStoreType get_timeContext() const { return this->m_context; }
    U32 get_seconds() const { return this->m_seconds; }
    U32 get_useconds() const { return this->m_useconds; }

  private:
    TimeBase m_timeBase;
    FwTimeContextStoreType m_context;
    U32 m_seconds;
    U32 m_useconds;
};

//! Mirrors the generated Fw::TimeIntervalValue struct.
class TimeIntervalValue {
  public:
    TimeIntervalValue() : m_seconds(0), m_useconds(0) {}
    TimeIntervalValue(U32 seconds, U32 useconds) : m_seconds(seconds), m_useconds(useconds) {}
    U32 get_seconds() const { return this->m_seconds; }
    U32 get_useconds() const { return this->m_useconds; }

  private:
    U32 m_seconds;
    U32 m_useconds;
};

}  // namespace Fw

#endif
