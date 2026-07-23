// ======================================================================
// \title  FpTypesStub.hpp
// \brief  Minimal F Prime primitive-type stand-ins for host unit tests.
//
// This directory provides just enough of the F Prime surface to compile real
// component .cpp files on the host without the framework, per the no-F-Prime
// rule in test/unit-tests/README.md. Types mirror lib/fprime definitions.
// ======================================================================

#ifndef UnitTestSupport_FpTypesStub_HPP
#define UnitTestSupport_FpTypesStub_HPP

#include <cstdint>

typedef uint8_t U8;
typedef uint32_t U32;
typedef int32_t FwIndexType;
typedef uint32_t FwOpcodeType;
typedef uint64_t FwSizeType;

namespace Fw {

//! Mirrors the generated Fw::CmdResponse enum class shape: values are
//! addressed as Fw::CmdResponse::OK etc. and passed by value.
class CmdResponse {
  public:
    enum T { OK, INVALID_OPCODE, VALIDATION_ERROR, FORMAT_ERROR, EXECUTION_ERROR, BUSY };
    CmdResponse() : m_value(OK) {}
    CmdResponse(T value) : m_value(value) {}
    bool operator==(const CmdResponse& other) const { return this->m_value == other.m_value; }
    bool operator==(T value) const { return this->m_value == value; }
    T value() const { return this->m_value; }

  private:
    T m_value;
};

}  // namespace Fw

#endif
