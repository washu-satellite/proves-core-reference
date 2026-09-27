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
typedef uint16_t U16;
typedef uint32_t U32;
typedef uint64_t U64;
typedef int8_t I8;
typedef int16_t I16;
typedef int32_t I32;
typedef int64_t I64;
typedef float F32;
typedef double F64;
typedef int32_t FwIndexType;
typedef uint32_t FwOpcodeType;
//! Mirrors lib/fprime/default/config/FpConfig.fpp:42,72 (FwPrmIdType = FwIdType = U32).
typedef uint32_t FwPrmIdType;
typedef uint64_t FwSizeType;
typedef int32_t FwEnumStoreType;
//! Mirrors lib/fprime/default/config/FpConfig.fpp:79,92.
typedef uint8_t FwTimeContextStoreType;
typedef uint16_t FwTimeBaseStoreType;

//! Mirrors lib/fprime/default/config/FpConfig.fpp (FwAssertArgType = U32 on this platform).
typedef uint32_t FwAssertArgType;

//! Mirrors lib/fprime/Fw/Types/BasicTypes.h:91.
#ifndef FW_MAX
#define FW_MAX(a, b) (((a) > (b)) ? (a) : (b))  //!< MAX macro
#endif

namespace Fw {

//! Mirrors the generated Fw::CmdResponse enum class shape: values are
//! addressed as Fw::CmdResponse::OK etc. and passed by value.
class CmdResponse {
  public:
    enum T { OK, INVALID_OPCODE, VALIDATION_ERROR, FORMAT_ERROR, EXECUTION_ERROR, BUSY };
    CmdResponse() : m_value(OK) {}
    CmdResponse(T value) : m_value(value) {}  // NOLINT(runtime/explicit) -- implicit by design, mirrors generated code
    bool operator==(const CmdResponse& other) const { return this->m_value == other.m_value; }
    bool operator==(T value) const { return this->m_value == value; }
    T value() const { return this->m_value; }

  private:
    T m_value;
};

//! Mirrors the generated Fw::Success enum class: values addressed as
//! Fw::Success::SUCCESS / ::FAILURE and compared by value. Drv port handlers
//! take this by reference as an out-parameter (see Tmp112Manager.fpp:2).
class Success {
  public:
    enum T { FAILURE = 0, SUCCESS = 1 };
    Success() : m_value(FAILURE) {}
    Success(T value) : m_value(value) {}  // NOLINT(runtime/explicit) -- implicit by design, mirrors generated code
    bool operator==(const Success& other) const { return this->m_value == other.m_value; }
    bool operator==(T value) const { return this->m_value == value; }
    bool operator!=(const Success& other) const { return !(*this == other); }
    bool operator!=(T value) const { return !(*this == value); }
    T value() const { return this->m_value; }

  private:
    T m_value;
};

//! Mirrors the generated Fw::Logic enum class (lib/fprime/Fw/Types/Types.fpp:38-41,
//! LOW = 0, HIGH = 1), the argument type of the GPIO write port.
class Logic {
  public:
    enum T { LOW = 0, HIGH = 1 };
    Logic() : e(LOW) {}
    Logic(T e1) : e(e1) {}                  // NOLINT(runtime/explicit) -- mirrors generated code
    operator T() const { return this->e; }  // NOLINT(runtime/explicit) -- enables switch/case
    bool operator==(T e1) const { return this->e == e1; }
    bool operator!=(T e1) const { return this->e != e1; }
    T e;
};

//! Mirrors the generated Fw::ParamValid enum class, returned by reference from
//! every paramGet_* accessor.
class ParamValid {
  public:
    enum T { UNINIT = 0, VALID = 1, INVALID = 2, DEFAULT = 3 };
    ParamValid() : m_value(UNINIT) {}
    ParamValid(T value) : m_value(value) {}  // NOLINT(runtime/explicit) -- mirrors generated code
    bool operator==(const ParamValid& other) const { return this->m_value == other.m_value; }
    bool operator==(T value) const { return this->m_value == value; }
    bool operator!=(const ParamValid& other) const { return !(*this == other); }
    T value() const { return this->m_value; }

  private:
    T m_value;
};

}  // namespace Fw

namespace Svc {

//! Mirrors the generated Svc::SeqArgs struct (lib/fprime/Svc/Seq/Seq.fpp:2-5):
//! a size and a fixed argument buffer, default {size = 0, buffer = 0}. The
//! sequencer ports pass it by const reference; components under test only
//! forward it, so the host shape carries the fields and nothing else.
struct SeqArgs {
    static constexpr FwSizeType SequenceArgumentsMaxSize = 128;
    FwSizeType size = 0;
    U8 buffer[SequenceArgumentsMaxSize] = {0};
    FwSizeType get_size() const { return this->size; }
};

}  // namespace Svc

namespace Drv {

//! Mirrors the generated Drv::ByteStreamStatus enum class
//! (lib/fprime/Drv/ByteStreamDriverModel/ByteStreamDriverModel.fpp:4-9), the
//! status argument of Drv.ByteStreamData and the return of Drv.ByteStreamSend.
class ByteStreamStatus {
  public:
    enum T { OP_OK = 0, SEND_RETRY = 1, RECV_NO_DATA = 2, OTHER_ERROR = 3 };
    ByteStreamStatus() : e(OP_OK) {}
    ByteStreamStatus(T e1) : e(e1) {}       // NOLINT(runtime/explicit) -- mirrors generated code
    operator T() const { return this->e; }  // NOLINT(runtime/explicit) -- enables switch/case
    bool operator==(T e1) const { return this->e == e1; }
    bool operator!=(T e1) const { return this->e != e1; }
    T e;
};

//! Mirrors the generated Drv::GpioStatus enum class
//! (lib/fprime/Drv/Ports/GpioDriverPorts.fpp:2-7), the return of Drv.GpioWrite
//! and Drv.GpioRead.
class GpioStatus {
  public:
    enum T { OP_OK = 0, NOT_OPENED = 1, INVALID_MODE = 2, UNKNOWN_ERROR = 3 };
    GpioStatus() : e(OP_OK) {}
    GpioStatus(T e1) : e(e1) {}             // NOLINT(runtime/explicit) -- mirrors generated code
    operator T() const { return this->e; }  // NOLINT(runtime/explicit) -- enables switch/case
    bool operator==(T e1) const { return this->e == e1; }
    bool operator!=(T e1) const { return this->e != e1; }
    T e;
};

}  // namespace Drv

#endif
