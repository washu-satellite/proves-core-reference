// ======================================================================
// \title  SegmentCodec.hpp
// \brief  F'-free byte codec for DataRecorder segment files.
//
// A segment is a 20-byte header followed by records. All multi-byte fields
// are little-endian (the Fw::Com bytes inside a record are F''s own
// big-endian serialization, stored as received).
//
// Header: magic "SEG1" | version u8 | stream u8 | boot count u16 |
//         open seconds u32 | open microseconds u32 | CRC-32 u32 over bytes 0..15
// Record: len u16 (1..MAX_PAYLOAD) | len bytes | CRC-32 u32 over len and bytes
//
// CRC-32 is Components::PersistedRecord::crc32 (reused, D-001). No state, no
// heap, no F' types: the host tests build this file on its own.
// ======================================================================

#ifndef Components_DataRecorder_SegmentCodec_HPP
#define Components_DataRecorder_SegmentCodec_HPP

#include <cstdint>

#include "config/FppConstantsAc.hpp"

namespace Components {
namespace SegmentCodec {

//! Bytes in a segment header.
constexpr uint32_t HEADER_SIZE = 20;
//! Bytes a record adds around its payload (len u16 + CRC-32).
constexpr uint32_t RECORD_OVERHEAD = 6;
//! Largest record payload: one Fw::ComBuffer.
constexpr uint32_t MAX_PAYLOAD = FW_COM_BUFFER_MAX_SIZE;
//! Segment header format version.
constexpr uint8_t VERSION = 1;

//! Result of a decode.
enum class Status : uint8_t {
    OK,           //!< Valid header or record.
    END,          //!< No bytes left: the segment ends cleanly here.
    TRUNCATED,    //!< Fewer bytes present than the structure needs.
    BAD_LENGTH,   //!< Record length 0 or above MAX_PAYLOAD.
    BAD_CRC,      //!< CRC-32 mismatch.
    BAD_MAGIC,    //!< Header magic is not "SEG1".
    BAD_VERSION,  //!< CRC-valid header with an unknown version.
};

//! Decoded segment header fields.
struct Header {
    uint8_t stream;
    uint16_t bootCount;
    uint32_t openSeconds;
    uint32_t openUseconds;
};

//! \brief Encode a segment header.
//! \return HEADER_SIZE, or 0 when outCap < HEADER_SIZE or out is null.
uint32_t encodeHeader(const Header& header, uint8_t* out, uint32_t outCap);

//! \brief Decode and validate a segment header.
//! Checks, in order: len < HEADER_SIZE (TRUNCATED), magic, CRC, version.
//! \param out written only when OK is returned
Status decodeHeader(const uint8_t* buf, uint32_t len, Header& out);

//! \brief Encode one record.
//! \return len + RECORD_OVERHEAD, or 0 for len 0, len > MAX_PAYLOAD, a null
//!         pointer or not enough room in out.
uint32_t encodeRecord(const uint8_t* payload, uint16_t len, uint8_t* out, uint32_t outCap);

//! \brief Decode one record at the start of buf.
//! Checks, in order: len == 0 (END), len < 2 (TRUNCATED), declared length 0 or
//! above MAX_PAYLOAD (BAD_LENGTH), len < declared + RECORD_OVERHEAD
//! (TRUNCATED), CRC (BAD_CRC).
//! \param payload    points into buf at the payload; set only when OK
//! \param payloadLen set only when OK
//! \param consumed   bytes of buf the record occupies; set only when OK
Status decodeRecord(const uint8_t* buf,
                    uint32_t len,
                    const uint8_t*& payload,
                    uint16_t& payloadLen,
                    uint32_t& consumed);

}  // namespace SegmentCodec
}  // namespace Components

#endif
