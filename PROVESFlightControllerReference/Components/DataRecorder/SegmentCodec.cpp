// ======================================================================
// \title  SegmentCodec.cpp
// \brief  F'-free byte codec for DataRecorder segment files.
// ======================================================================

#include "PROVESFlightControllerReference/Components/DataRecorder/SegmentCodec.hpp"

#include "PROVESFlightControllerReference/Components/PersistedRecord/PersistedRecordCodec.hpp"

namespace Components {
namespace SegmentCodec {

namespace {

//! Segment header magic "SEG1" (kept out of the header: C++14 ODR).
const uint8_t SEGMENT_MAGIC[4] = {0x53, 0x45, 0x47, 0x31};

//! Offset of the header CRC (it covers bytes 0..15).
constexpr uint32_t HEADER_CRC_OFFSET = 16;

void putLe16(uint8_t* out, uint16_t v) {
    out[0] = static_cast<uint8_t>(v & 0xFFu);
    out[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
}

void putLe32(uint8_t* out, uint32_t v) {
    for (uint32_t i = 0; i < 4; i++) {
        out[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFFu);
    }
}

uint16_t getLe16(const uint8_t* in) {
    return static_cast<uint16_t>(static_cast<uint16_t>(in[0]) | (static_cast<uint16_t>(in[1]) << 8));
}

uint32_t getLe32(const uint8_t* in) {
    uint32_t v = 0;
    for (uint32_t i = 0; i < 4; i++) {
        v |= static_cast<uint32_t>(in[i]) << (8 * i);
    }
    return v;
}

}  // namespace

uint32_t encodeHeader(const Header& header, uint8_t* out, uint32_t outCap) {
    if (out == nullptr || outCap < HEADER_SIZE) {
        return 0;
    }
    for (uint32_t i = 0; i < 4; i++) {
        out[i] = SEGMENT_MAGIC[i];
    }
    out[4] = VERSION;
    out[5] = header.stream;
    putLe16(&out[6], header.bootCount);
    putLe32(&out[8], header.openSeconds);
    putLe32(&out[12], header.openUseconds);
    putLe32(&out[HEADER_CRC_OFFSET], PersistedRecord::crc32(out, HEADER_CRC_OFFSET));
    return HEADER_SIZE;
}

Status decodeHeader(const uint8_t* buf, uint32_t len, Header& out) {
    if (buf == nullptr || len < HEADER_SIZE) {
        return Status::TRUNCATED;
    }
    for (uint32_t i = 0; i < 4; i++) {
        if (buf[i] != SEGMENT_MAGIC[i]) {
            return Status::BAD_MAGIC;
        }
    }
    if (PersistedRecord::crc32(buf, HEADER_CRC_OFFSET) != getLe32(&buf[HEADER_CRC_OFFSET])) {
        return Status::BAD_CRC;
    }
    if (buf[4] != VERSION) {
        return Status::BAD_VERSION;
    }
    out.stream = buf[5];
    out.bootCount = getLe16(&buf[6]);
    out.openSeconds = getLe32(&buf[8]);
    out.openUseconds = getLe32(&buf[12]);
    return Status::OK;
}

uint32_t encodeRecord(const uint8_t* payload, uint16_t len, uint8_t* out, uint32_t outCap) {
    if (payload == nullptr || out == nullptr || len == 0 || len > MAX_PAYLOAD) {
        return 0;
    }
    const uint32_t total = static_cast<uint32_t>(len) + RECORD_OVERHEAD;
    if (outCap < total) {
        return 0;
    }
    putLe16(out, len);
    for (uint32_t i = 0; i < len; i++) {
        out[2 + i] = payload[i];
    }
    putLe32(&out[2 + len], PersistedRecord::crc32(out, 2u + len));
    return total;
}

Status decodeRecord(const uint8_t* buf,
                    uint32_t len,
                    const uint8_t*& payload,
                    uint16_t& payloadLen,
                    uint32_t& consumed) {
    if (len == 0) {
        return Status::END;
    }
    if (buf == nullptr || len < 2) {
        return Status::TRUNCATED;
    }
    const uint16_t declared = getLe16(buf);
    if (declared == 0 || declared > MAX_PAYLOAD) {
        return Status::BAD_LENGTH;
    }
    const uint32_t total = static_cast<uint32_t>(declared) + RECORD_OVERHEAD;
    if (len < total) {
        return Status::TRUNCATED;
    }
    if (PersistedRecord::crc32(buf, 2u + declared) != getLe32(&buf[2 + declared])) {
        return Status::BAD_CRC;
    }
    payload = &buf[2];
    payloadLen = declared;
    consumed = total;
    return Status::OK;
}

}  // namespace SegmentCodec
}  // namespace Components
