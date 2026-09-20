// ======================================================================
// \title  PersistedRecordCodec.cpp
// \brief  Pure-logic encode/decode for self-validating persisted records.
//         No F Prime includes (host-testable).
// ======================================================================

#include "PROVESFlightControllerReference/Components/PersistedRecord/PersistedRecordCodec.hpp"

namespace Components {
namespace PersistedRecord {

namespace {

//! Reflected CRC-32 polynomial (IEEE 802.3), used bitwise so no table is held
//! in flash or RAM.
constexpr uint32_t CRC32_POLYNOMIAL = 0xEDB88320u;
constexpr uint32_t CRC32_SEED = 0xFFFFFFFFu;

//! Offset of the format-version byte within a record.
constexpr uint32_t VERSION_OFFSET = MAGIC_SIZE;
//! Offset of the little-endian payload-length field within a record.
constexpr uint32_t LENGTH_OFFSET = MAGIC_SIZE + 1;

inline void putU16LE(uint8_t* dest, uint16_t value) {
    dest[0] = static_cast<uint8_t>(value & 0xFFu);
    dest[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
}

inline uint16_t getU16LE(const uint8_t* src) {
    return static_cast<uint16_t>(static_cast<uint16_t>(src[0]) | (static_cast<uint16_t>(src[1]) << 8));
}

inline void putU32LE(uint8_t* dest, uint32_t value) {
    dest[0] = static_cast<uint8_t>(value & 0xFFu);
    dest[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
    dest[2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
    dest[3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
}

inline uint32_t getU32LE(const uint8_t* src) {
    return static_cast<uint32_t>(src[0]) | (static_cast<uint32_t>(src[1]) << 8) |
           (static_cast<uint32_t>(src[2]) << 16) | (static_cast<uint32_t>(src[3]) << 24);
}

}  // namespace

uint32_t crc32(const uint8_t* data, uint32_t len) {
    uint32_t crc = CRC32_SEED;
    if (data != nullptr) {
        for (uint32_t index = 0; index < len; index++) {
            crc ^= static_cast<uint32_t>(data[index]);
            for (uint32_t bit = 0; bit < 8; bit++) {
                const uint32_t mask = static_cast<uint32_t>(-static_cast<int32_t>(crc & 1u));
                crc = (crc >> 1) ^ (CRC32_POLYNOMIAL & mask);
            }
        }
    }
    return crc ^ CRC32_SEED;
}

uint32_t encode(const uint8_t* magic, const uint8_t* payload, uint16_t payloadLen, uint8_t* out, uint32_t outLen) {
    if (magic == nullptr || out == nullptr) {
        return 0;
    }
    if (static_cast<uint32_t>(payloadLen) > MAX_PAYLOAD_SIZE) {
        return 0;
    }
    if (payloadLen > 0 && payload == nullptr) {
        return 0;
    }
    const uint32_t recordSize = OVERHEAD + static_cast<uint32_t>(payloadLen);
    if (outLen < recordSize) {
        return 0;
    }

    std::memcpy(out, magic, static_cast<size_t>(MAGIC_SIZE));
    out[VERSION_OFFSET] = FORMAT_VERSION;
    putU16LE(out + LENGTH_OFFSET, payloadLen);
    if (payloadLen > 0) {
        std::memcpy(out + HEADER_SIZE, payload, static_cast<size_t>(payloadLen));
    }
    putU32LE(out + HEADER_SIZE + payloadLen, crc32(out, HEADER_SIZE + static_cast<uint32_t>(payloadLen)));
    return recordSize;
}

Status decode(const uint8_t* magic,
              const uint8_t* buf,
              uint32_t bufLen,
              uint8_t* payloadOut,
              uint16_t payloadCap,
              uint16_t& payloadLenOut) {
    if (magic == nullptr || buf == nullptr) {
        return Status::INVALID_ARGUMENT;
    }
    // (1) A buffer shorter than the fixed overhead cannot hold any record.
    if (bufLen < OVERHEAD) {
        return Status::TRUNCATED;
    }
    // (2) Wrong record type: reject before trusting any other field.
    if (std::memcmp(buf, magic, static_cast<size_t>(MAGIC_SIZE)) != 0) {
        return Status::BAD_MAGIC;
    }
    // (3) The declared length must be in range and must account for exactly
    //     the bytes present; trailing bytes are as suspect as missing ones.
    const uint16_t declared = getU16LE(buf + LENGTH_OFFSET);
    if (static_cast<uint32_t>(declared) > MAX_PAYLOAD_SIZE) {
        return Status::BAD_LENGTH;
    }
    const uint32_t expected = OVERHEAD + static_cast<uint32_t>(declared);
    if (bufLen < expected) {
        return Status::TRUNCATED;
    }
    if (bufLen > expected) {
        return Status::BAD_LENGTH;
    }
    // (4) CRC over magic, version, length and payload.
    const uint32_t stored = getU32LE(buf + HEADER_SIZE + declared);
    if (stored != crc32(buf, HEADER_SIZE + static_cast<uint32_t>(declared))) {
        return Status::BAD_CRC;
    }
    // (5) Checked after the CRC, so a BAD_VERSION record is intact and a
    //     consumer may safely migrate from it.
    if (buf[VERSION_OFFSET] != FORMAT_VERSION) {
        return Status::BAD_VERSION;
    }
    // (6) Only now is the caller's buffer consulted; nothing has been written.
    if (declared > payloadCap) {
        return Status::BAD_LENGTH;
    }
    if (declared > 0) {
        if (payloadOut == nullptr) {
            return Status::INVALID_ARGUMENT;
        }
        std::memcpy(payloadOut, buf + HEADER_SIZE, static_cast<size_t>(declared));
    }
    payloadLenOut = declared;
    return Status::OK;
}

}  // namespace PersistedRecord
}  // namespace Components
