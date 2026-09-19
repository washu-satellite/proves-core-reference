// ======================================================================
// \title  Crc16.hpp
// \brief  Header-only CRC-16/CCITT-FALSE shared by the TC frame corrector and
//         the driver-board wire protocol.
//
// This header intentionally contains NO F Prime / Svc / Zephyr includes so it
// can be compiled into host (gtest) unit tests. It uses only <cstdint>.
//
// Extracted from TcFrameCorrectorCodec.cpp on its second use (Cycle E). The
// corrector keeps its own syndrome walk (stepSyndrome and the constants it
// needs stay there); this file carries only the checksum itself.
//
// Parameters (the STM32 driver-board firmware carries a C copy of this loop):
//   polynomial 0x1021, init 0xFFFF, MSB-first, no input or output reflection,
//   xorout 0. Check value for the ASCII string "123456789" is 0x29B1. Matches
//   Svc::Ccsds::Utils::CRC16::compute (lib/fprime/Svc/Ccsds/Utils/CRC16.hpp),
//   which wraps the table-driven update_crc_ccitt in Utils/Hash/libcrc.
//
// Computed bitwise: no lookup table, no static objects, nothing allocated.
// ======================================================================

#ifndef Components_Crc16_Crc16_HPP
#define Components_Crc16_Crc16_HPP

#include <cstdint>

namespace Crc16 {

//! \brief CRC-16/CCITT-FALSE over a byte buffer.
//! \param data buffer to checksum (null is treated as empty)
//! \param len  number of bytes to checksum
//! \return the CRC-16 value; 0xFFFF for an empty buffer
inline uint16_t ccitt(const uint8_t* data, uint32_t len) {
    constexpr uint16_t POLYNOMIAL = 0x1021u;
    constexpr uint16_t SEED = 0xFFFFu;
    uint16_t crc = SEED;
    if (data == nullptr) {
        return crc;
    }
    for (uint32_t i = 0; i < len; i++) {
        crc = static_cast<uint16_t>(crc ^ (static_cast<uint16_t>(data[i]) << 8));
        for (uint32_t bit = 0; bit < 8; bit++) {
            crc = static_cast<uint16_t>((crc << 1) ^ ((crc & 0x8000u) != 0u ? POLYNOMIAL : 0u));
        }
    }
    return crc;
}

}  // namespace Crc16

#endif
