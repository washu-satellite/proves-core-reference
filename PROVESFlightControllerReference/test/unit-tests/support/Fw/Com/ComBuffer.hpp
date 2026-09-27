// ======================================================================
// \title  Fw/Com/ComBuffer.hpp (host-test stub)
// \brief  Minimal Fw::ComBuffer stand-in for host unit tests (Cycle M).
//
// On the target Fw::ComBuffer is LinearBufferTemplate<FW_COM_BUFFER_MAX_SIZE>
// (lib/fprime/Fw/Com/ComBuffer.hpp), FW_COM_BUFFER_MAX_SIZE = 227 at this tree
// (FpConstants.fpp:22). This stub carries the four calls the DataRecorder
// input handlers need: setBuff, getBuffAddr, getSize, getCapacity.
// ======================================================================

#ifndef UnitTestSupport_Fw_Com_ComBuffer_HPP
#define UnitTestSupport_Fw_Com_ComBuffer_HPP

#include <cstring>

#include "../../FpTypesStub.hpp"
#include "../Types/Serializable.hpp"

namespace Fw {

class ComBuffer {
  public:
    //! FW_COM_BUFFER_MAX_SIZE at this tree.
    static constexpr FwSizeType CAPACITY = 227;

    ComBuffer() : m_size(0) { std::memset(this->m_data, 0, sizeof(this->m_data)); }
    ComBuffer(const U8* src, FwSizeType length) : m_size(0) {
        std::memset(this->m_data, 0, sizeof(this->m_data));
        (void)this->setBuff(src, length);
    }

    //! Copy length bytes in; FW_SERIALIZE_NO_ROOM_LEFT (and nothing copied) above capacity.
    SerializeStatus setBuff(const U8* src, FwSizeType length) {
        if (length > CAPACITY) {
            return FW_SERIALIZE_NO_ROOM_LEFT;
        }
        if (length > 0 && src != nullptr) {
            std::memcpy(this->m_data, src, static_cast<size_t>(length));
        }
        this->m_size = length;
        return FW_SERIALIZE_OK;
    }

    U8* getBuffAddr() { return this->m_data; }
    const U8* getBuffAddr() const { return this->m_data; }
    FwSizeType getSize() const { return this->m_size; }
    FwSizeType getCapacity() const { return CAPACITY; }

  private:
    U8 m_data[CAPACITY];
    FwSizeType m_size;
};

}  // namespace Fw

#endif
