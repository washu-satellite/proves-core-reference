// ======================================================================
// \title  Fw/Types/String.hpp (host-test stub)
// \brief  Minimal Fw string stand-ins for host unit tests.
//
// Mirrors the shape components rely on: a common Fw::StringBase that port and
// event signatures take by const reference, with the concrete Fw::String,
// Fw::LogStringArg and Fw::ParamString all deriving from it, constructible
// and assignable from a C string, and readable through toChar().
// ======================================================================

#ifndef UnitTestSupport_Fw_Types_String_HPP
#define UnitTestSupport_Fw_Types_String_HPP

#include <string>

namespace Fw {

class StringBase {
  public:
    StringBase() {}
    // NOLINTNEXTLINE(runtime/explicit) -- implicit by design, mirrors generated code
    StringBase(const char* src) : m_str(src != nullptr ? src : "") {}
    virtual ~StringBase() {}

    StringBase& operator=(const char* src) {
        this->m_str = (src != nullptr ? src : "");
        return *this;
    }

    //! Mirrors Fw::StringBase::operator=(const StringBase&): assignment across
    //! the concrete string flavours (e.g. a port argument into a member).
    StringBase& operator=(const StringBase& src) {
        this->m_str = src.m_str;
        return *this;
    }

    const char* toChar() const { return this->m_str.c_str(); }
    const std::string& str() const { return this->m_str; }
    bool operator==(const char* other) const { return this->m_str == (other != nullptr ? other : ""); }
    bool operator==(const StringBase& other) const { return this->m_str == other.m_str; }

  protected:
    std::string m_str;
};

class String : public StringBase {
  public:
    String() {}
    String(const char* src) : StringBase(src) {}  // NOLINT(runtime/explicit) -- mirrors generated code
    //! Mirrors Fw::String(const StringBase&): copy across string flavours
    //! (StartupManager builds "<path>.tmp" from a ParamString this way).
    String(const StringBase& src) : StringBase(src.toChar()) {}  // NOLINT(runtime/explicit) -- mirrors generated code
    //! Mirrors Fw::StringBase::operator+=(const char*).
    String& operator+=(const char* src) {
        this->m_str += (src != nullptr ? src : "");
        return *this;
    }
    String& operator=(const char* src) {
        StringBase::operator=(src);
        return *this;
    }
    String& operator=(const StringBase& src) {
        StringBase::operator=(src);
        return *this;
    }
};

class LogStringArg : public StringBase {
  public:
    LogStringArg() {}
    LogStringArg(const char* src) : StringBase(src) {}  // NOLINT(runtime/explicit) -- mirrors generated code
    LogStringArg& operator=(const char* src) {
        StringBase::operator=(src);
        return *this;
    }
    LogStringArg& operator=(const StringBase& src) {
        StringBase::operator=(src);
        return *this;
    }
};

class ParamString : public StringBase {
  public:
    ParamString() {}
    ParamString(const char* src) : StringBase(src) {}  // NOLINT(runtime/explicit) -- mirrors generated code
    ParamString& operator=(const char* src) {
        StringBase::operator=(src);
        return *this;
    }
    ParamString& operator=(const StringBase& src) {
        StringBase::operator=(src);
        return *this;
    }
};

}  // namespace Fw

#endif
