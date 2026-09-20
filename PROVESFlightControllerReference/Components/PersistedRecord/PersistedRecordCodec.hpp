// ======================================================================
// \title  PersistedRecordCodec.hpp
// \brief  Pure-logic encode/decode for self-validating persisted records.
//
// This header intentionally contains NO F Prime / Svc / Zephyr includes so it
// can be linked into host (gtest) unit tests. It uses only <cstdint>,
// <cstddef> and <cstring>.
//
// Record layout (all multi-byte fields little-endian):
//   offset  size  field
//   0       4     magic     record-type identifier, supplied by the consumer
//   4       1     version   FORMAT_VERSION at encode time
//   5       2     length    payload length in bytes
//   7       n     payload   consumer-defined state
//   7+n     4     crc       CRC-32 over all preceding bytes
// ======================================================================

#ifndef Components_PersistedRecord_PersistedRecordCodec_HPP
#define Components_PersistedRecord_PersistedRecordCodec_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Components {
namespace PersistedRecord {

//! Format version written by encode() and required by decode().
constexpr uint8_t FORMAT_VERSION = 1;

//! Size of the record-type magic prefix.
constexpr uint32_t MAGIC_SIZE = 4;
//! Size of magic + version + length.
constexpr uint32_t HEADER_SIZE = 7;
//! Size of the trailing CRC-32 field.
constexpr uint32_t CRC_SIZE = 4;
//! Total non-payload bytes in a record.
constexpr uint32_t OVERHEAD = HEADER_SIZE + CRC_SIZE;

//! Largest payload any consumer may persist. Bounds every stack buffer on the
//! encode/decode/load/store paths (PersistedRecord-7).
constexpr uint32_t MAX_PAYLOAD_SIZE = 64;
//! Largest possible encoded record; a compile-time constant by construction.
constexpr uint32_t MAX_RECORD_SIZE = OVERHEAD + MAX_PAYLOAD_SIZE;

//! Result of a codec or file operation. Codec calls return only the first
//! group; the file layer in PersistedRecordFile.hpp adds the I/O statuses.
enum class Status : uint8_t {
    OK,                //!< Valid record; payload and length populated.
    TRUNCATED,         //!< Fewer bytes present than the header declares.
    BAD_MAGIC,         //!< Magic prefix does not match the expected record type.
    BAD_LENGTH,        //!< Declared length is oversize, inconsistent, or exceeds caller capacity.
    BAD_CRC,           //!< CRC-32 mismatch (any bit flip in magic, version, length or payload).
    BAD_VERSION,       //!< CRC-valid record carrying an unrecognized format version.
    MISSING,           //!< Backing file is absent (first boot); not a corruption.
    INVALID_ARGUMENT,  //!< Null pointer or oversize payload supplied by the caller.
    OPEN_ERROR,        //!< File exists but could not be opened.
    READ_ERROR,        //!< Read failed partway through.
    WRITE_ERROR,       //!< Write failed or was short.
    SYNC_ERROR,        //!< Flush to media failed.
    RENAME_ERROR       //!< Rename of the temporary file over the target failed.
};

//! \brief CRC-32 (IEEE 802.3), reflected, polynomial 0xEDB88320, init and
//!        xorout 0xFFFFFFFF. Computed bitwise: no lookup table, no static objects.
//! \param data buffer to checksum (null is treated as empty)
//! \param len  number of bytes to checksum
//! \return the CRC-32 value; 0 for an empty buffer
uint32_t crc32(const uint8_t* data, uint32_t len);

//! \brief Encode a payload into a complete record.
//! \param magic      MAGIC_SIZE-byte record-type identifier
//! \param payload    payload bytes (may be null only when payloadLen is 0)
//! \param payloadLen payload length, must be <= MAX_PAYLOAD_SIZE
//! \param out        destination buffer
//! \param outLen     size of the destination buffer
//! \return number of bytes written (OVERHEAD + payloadLen), or 0 on invalid
//!         arguments, oversize payload, or a destination buffer too small.
//!         A successful encode always writes FORMAT_VERSION.
uint32_t encode(const uint8_t* magic, const uint8_t* payload, uint16_t payloadLen, uint8_t* out, uint32_t outLen);

//! \brief Decode and validate a record.
//!
//! Validation order is fixed so each failure class maps to one status:
//! length floor, magic, declared length, CRC, version, caller capacity. The
//! version check runs after the CRC so a BAD_VERSION record is known to be
//! otherwise intact and safe to migrate from.
//!
//! \param magic         MAGIC_SIZE-byte record-type identifier expected
//! \param buf           encoded record
//! \param bufLen        number of valid bytes in buf
//! \param payloadOut    receives the payload; written ONLY when OK is returned
//! \param payloadCap    capacity of payloadOut in bytes
//! \param payloadLenOut receives the payload length; written ONLY when OK
//! \return Status::OK when the record is valid, otherwise the failure reason.
Status decode(const uint8_t* magic,
              const uint8_t* buf,
              uint32_t bufLen,
              uint8_t* payloadOut,
              uint16_t payloadCap,
              uint16_t& payloadLenOut);

}  // namespace PersistedRecord
}  // namespace Components

#endif
