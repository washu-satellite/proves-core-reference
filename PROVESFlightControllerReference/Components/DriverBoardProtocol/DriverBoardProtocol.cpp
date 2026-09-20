// ======================================================================
// \title  DriverBoardProtocol.cpp
// \brief  Framing and byte-at-a-time parser for the driver-board link.
// ======================================================================

#include "PROVESFlightControllerReference/Components/DriverBoardProtocol/DriverBoardProtocol.hpp"

#include <cstring>

#include "PROVESFlightControllerReference/Components/Crc16/Crc16.hpp"

namespace Components {
namespace DriverBoardProtocol {

// ----------------------------------------------------------------------
// encode
// ----------------------------------------------------------------------

size_t encode(uint8_t type, uint8_t seq, const uint8_t* payload, uint8_t len, uint8_t* out, size_t outCapacity) {
    if (len > MAX_PAYLOAD || out == nullptr || (payload == nullptr && len > 0)) {
        return 0;
    }
    const size_t total = HEADER_SIZE + len + CRC_SIZE;
    if (outCapacity < total) {
        return 0;
    }
    out[0] = SYNC0;
    out[1] = SYNC1;
    out[OFFSET_TYPE] = type;
    out[OFFSET_SEQ] = seq;
    out[OFFSET_LEN] = len;
    for (size_t i = 0; i < len; i++) {
        out[OFFSET_PAYLOAD + i] = payload[i];
    }
    const uint16_t crc = Crc16::ccitt(&out[OFFSET_TYPE], static_cast<uint32_t>(3 + len));
    out[OFFSET_PAYLOAD + len] = static_cast<uint8_t>(crc >> 8);
    out[OFFSET_PAYLOAD + len + 1] = static_cast<uint8_t>(crc & 0xFFu);
    return total;
}

// ----------------------------------------------------------------------
// Parser
// ----------------------------------------------------------------------

Parser::Parser() : m_len(0), m_readyLen(0), m_discarding(false), m_haveSeq(false), m_lastSeq(0) {
    std::memset(m_buf, 0, sizeof(m_buf));
    resetStats();
}

bool Parser::feed(uint8_t byte) {
    if (m_readyLen != 0) {
        // The caller has had its chance to read the delivered frame; drop it.
        drop(m_readyLen);
        m_readyLen = 0;
    }
    if (m_len >= MAX_FRAME) {
        // Unreachable by construction: settle() only asks for more bytes while
        // the buffer is a strict prefix of one candidate, which is < MAX_FRAME.
        drop(1);
    }
    m_buf[m_len] = byte;
    m_len = static_cast<uint8_t>(m_len + 1);
    return settle();
}

bool Parser::settle() {
    for (;;) {
        if (m_len == 0) {
            return false;
        }
        if (m_buf[0] != SYNC0) {
            // Hunting: discard everything up to the next 0x5C as one stretch.
            size_t k = 1;
            while (k < m_len && m_buf[k] != SYNC0) {
                k++;
            }
            reject(Reject::SYNC);
            drop(k);
            continue;
        }
        if (m_len < SYNC_SIZE) {
            return false;
        }
        if (m_buf[1] != SYNC1) {
            reject(Reject::SYNC);
            drop(1);
            continue;
        }
        if (m_len < HEADER_SIZE) {
            return false;
        }
        const uint8_t len = m_buf[OFFSET_LEN];
        if (len > MAX_PAYLOAD) {
            reject(Reject::LENGTH);
            drop(1);
            continue;
        }
        const size_t total = HEADER_SIZE + len + CRC_SIZE;
        if (m_len < total) {
            return false;
        }
        const uint16_t computed = Crc16::ccitt(&m_buf[OFFSET_TYPE], static_cast<uint32_t>(3 + len));
        const uint16_t wire = static_cast<uint16_t>((static_cast<uint16_t>(m_buf[total - 2]) << 8) | m_buf[total - 1]);
        if (computed != wire) {
            reject(Reject::CRC);
            drop(1);
            continue;
        }
        accept(total);
        return true;
    }
}

void Parser::drop(size_t n) {
    if (n >= m_len) {
        m_len = 0;
        return;
    }
    std::memmove(m_buf, m_buf + n, m_len - n);
    m_len = static_cast<uint8_t>(m_len - n);
}

void Parser::reject(Reject why) {
    if (!m_discarding) {
        m_stats.rejected++;
        m_stats.lastReject = why;
        m_discarding = true;
    }
}

void Parser::accept(size_t total) {
    m_readyLen = static_cast<uint8_t>(total);
    m_discarding = false;
    m_stats.accepted++;
    const uint8_t seq = m_buf[OFFSET_SEQ];
    if (m_haveSeq && seq != static_cast<uint8_t>(m_lastSeq + 1)) {
        m_stats.seqGaps++;
    }
    m_haveSeq = true;
    m_lastSeq = seq;
}

Frame Parser::frame() const {
    Frame f;
    std::memset(&f, 0, sizeof(f));
    if (m_readyLen == 0) {
        return f;
    }
    f.type = m_buf[OFFSET_TYPE];
    f.seq = m_buf[OFFSET_SEQ];
    f.len = m_buf[OFFSET_LEN];
    for (size_t i = 0; i < f.len; i++) {
        f.payload[i] = m_buf[OFFSET_PAYLOAD + i];
    }
    return f;
}

const Stats& Parser::stats() const {
    return m_stats;
}

void Parser::reset() {
    m_len = 0;
    m_readyLen = 0;
    m_discarding = false;
    m_haveSeq = false;
    m_lastSeq = 0;
}

void Parser::resetStats() {
    m_stats.accepted = 0;
    m_stats.rejected = 0;
    m_stats.seqGaps = 0;
    m_stats.lastReject = Reject::NONE;
}

}  // namespace DriverBoardProtocol
}  // namespace Components
