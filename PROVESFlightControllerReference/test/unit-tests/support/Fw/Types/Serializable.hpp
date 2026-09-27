// ======================================================================
// \title  Fw/Types/Serializable.hpp (host-test stub)
// \brief  Minimal Fw::ExternalSerializeBuffer stand-in for host unit tests.
//
// Mirrors the subset StartupManager.cpp's read<>/write<> templates use
// (lib/fprime/Fw/Types/Serializable.hpp): construction over an external
// byte array, serializeFrom(const FwSizeType&), deserializeTo(FwSizeType&)
// and setBuffLen(). Encoding is big-endian, as F Prime serializes integers,
// so a test can craft the exact bytes a target writes. Only FwSizeType is
// supported: the boot count is the one value the component serializes.
// ======================================================================

#ifndef UnitTestSupport_Fw_Types_Serializable_HPP
#define UnitTestSupport_Fw_Types_Serializable_HPP

#include "../../FpTypesStub.hpp"

namespace Fw {

//! Mirrors lib/fprime/Fw/Types/Serializable.hpp:14-22 (unscoped enum; the
//! component names values as Fw::SerializeStatus::FW_SERIALIZE_OK).
enum SerializeStatus {
    FW_SERIALIZE_OK,
    FW_SERIALIZE_FORMAT_ERROR,
    FW_SERIALIZE_NO_ROOM_LEFT,
    FW_DESERIALIZE_BUFFER_EMPTY,
    FW_DESERIALIZE_FORMAT_ERROR,
    FW_DESERIALIZE_SIZE_MISMATCH,
    FW_DESERIALIZE_TYPE_MISMATCH
};

class ExternalSerializeBuffer {
  public:
    ExternalSerializeBuffer(U8* buffPtr, FwSizeType size)
        : m_buff(buffPtr), m_capacity(size), m_serLoc(0), m_deserLoc(0) {}

    //! Append value big-endian at the serialization cursor.
    SerializeStatus serializeFrom(const FwSizeType& value) {
        if (this->m_serLoc + sizeof(FwSizeType) > this->m_capacity) {
            return FW_SERIALIZE_NO_ROOM_LEFT;
        }
        for (FwSizeType i = 0; i < sizeof(FwSizeType); i++) {
            const FwSizeType shift = 8 * (sizeof(FwSizeType) - 1 - i);
            this->m_buff[this->m_serLoc + i] = static_cast<U8>((value >> shift) & 0xFFU);
        }
        this->m_serLoc += sizeof(FwSizeType);
        return FW_SERIALIZE_OK;
    }

    //! Read a big-endian value at the deserialization cursor.
    SerializeStatus deserializeTo(FwSizeType& value) {
        if (this->m_deserLoc + sizeof(FwSizeType) > this->m_serLoc) {
            return FW_DESERIALIZE_BUFFER_EMPTY;
        }
        FwSizeType out = 0;
        for (FwSizeType i = 0; i < sizeof(FwSizeType); i++) {
            out = (out << 8) | static_cast<FwSizeType>(this->m_buff[this->m_deserLoc + i]);
        }
        this->m_deserLoc += sizeof(FwSizeType);
        value = out;
        return FW_SERIALIZE_OK;
    }

    //! Declare how many bytes of the external buffer hold data (after a read).
    SerializeStatus setBuffLen(FwSizeType length) {
        if (length > this->m_capacity) {
            return FW_SERIALIZE_NO_ROOM_LEFT;
        }
        this->m_serLoc = length;
        this->m_deserLoc = 0;
        return FW_SERIALIZE_OK;
    }

    FwSizeType getBuffLength() const { return this->m_serLoc; }
    FwSizeType getBuffCapacity() const { return this->m_capacity; }

  private:
    U8* m_buff;
    FwSizeType m_capacity;
    FwSizeType m_serLoc;
    FwSizeType m_deserLoc;
};

}  // namespace Fw

#endif
