// ======================================================================
// \title  Fw/Buffer/Buffer.hpp (host-test stub)
// \brief  Minimal Fw::Buffer stand-in for host unit tests.
//
// Mirrors the subset of lib/fprime/Fw/Buffer/Buffer.hpp (:61 SizeType, :85
// constructor, :156-186 accessors) that a ByteStream client touches: a
// non-owning (pointer, size) pair. No serialization is provided. The layout
// (U8* + FwSizeType) is identical to the private copy inside the
// TcFrameCorrector recorder stub, which predates this header, so the two
// definitions never disagree inside one test binary.
// ======================================================================

#ifndef UnitTestSupport_Fw_Buffer_Buffer_HPP
#define UnitTestSupport_Fw_Buffer_Buffer_HPP

#include "../../FpTypesStub.hpp"

namespace Fw {

class Buffer {
  public:
    using SizeType = FwSizeType;

    Buffer() : m_data(nullptr), m_size(0) {}
    Buffer(U8* data, SizeType size, U32 context = 0) : m_data(data), m_size(size) { (void)context; }

    U8* getData() const { return this->m_data; }
    SizeType getSize() const { return this->m_size; }
    void setData(U8* data) { this->m_data = data; }
    void setSize(SizeType size) { this->m_size = size; }
    //! Mirrors Buffer.cpp:52-54: a valid buffer has data and a non-zero size.
    bool isValid() const { return (this->m_data != nullptr) && (this->m_size > 0); }

  private:
    U8* m_data;
    SizeType m_size;
};

}  // namespace Fw

#endif
