// ======================================================================
// \title  TcFrameCorrectorCodec.cpp
// \brief  Pure-logic single-bit corrector for CCSDS TC transfer frames.
//         No F Prime includes (host-testable).
// ======================================================================

#include "PROVESFlightControllerReference/Components/TcFrameCorrector/TcFrameCorrectorCodec.hpp"

namespace Components {
namespace TcFrameCorrection {

namespace {

//! CRC-16/CCITT generator polynomial, used bitwise so no table is held in
//! flash or RAM.
constexpr uint16_t CRC16_POLYNOMIAL = 0x1021u;
//! CRC-16/CCITT-FALSE initial register value.
constexpr uint16_t CRC16_SEED = 0xFFFFu;

//! Syndrome produced by flipping the LAST bit of the checksummed region. Every
//! earlier bit's syndrome is this value multiplied by x^k modulo the generator,
//! which is the recurrence stepped in the walk below.
constexpr uint16_t LAST_DATA_BIT_SYNDROME = CRC16_POLYNOMIAL;

//! Read a big-endian 16-bit field (the byte order of every CCSDS TC field).
inline uint16_t readBE16(const uint8_t* src) {
    return static_cast<uint16_t>((static_cast<uint16_t>(src[0]) << 8) | static_cast<uint16_t>(src[1]));
}

//! Flip one bit, counted MSB-first from the start of the buffer.
inline void flipBit(uint8_t* buf, uint32_t bitPos) {
    buf[bitPos / 8] = static_cast<uint8_t>(buf[bitPos / 8] ^ (0x80u >> (bitPos % 8)));
}

//! Multiply a syndrome by x modulo the CRC-CCITT generator: the step that walks
//! from one bit position to the one before it.
inline uint16_t stepSyndrome(uint16_t s) {
    return static_cast<uint16_t>((s << 1) ^ ((s & 0x8000u) != 0u ? CRC16_POLYNOMIAL : 0u));
}

//! \brief Confirm a repaired buffer really is the TC frame we expect.
//!
//! A three-or-more-bit error can land on a single-bit syndrome and would
//! otherwise be "corrected" into garbage. Checking the invariant header token
//! and the self-declared length rejects almost all of those; the residue is
//! caught downstream by the Authenticate HMAC.
bool postCheck(const uint8_t* frame, uint32_t len, uint32_t dataLen, uint16_t expectedToken) {
    if (readBE16(frame) != expectedToken) {
        return false;
    }
    // vcIdAndLength carries (frame length - 1) in its low 10 bits (TcDeframer.cpp:61).
    const uint32_t declaredLen = static_cast<uint32_t>(readBE16(frame + 2) & FRAME_LENGTH_MASK) + 1u;
    if (declaredLen != len) {
        return false;
    }
    return crc16Ccitt(frame, dataLen) == readBE16(frame + dataLen);
}

}  // namespace

uint16_t crc16Ccitt(const uint8_t* data, uint32_t len) {
    uint16_t crc = CRC16_SEED;
    if (data == nullptr) {
        return crc;
    }
    for (uint32_t i = 0; i < len; i++) {
        crc = static_cast<uint16_t>(crc ^ (static_cast<uint16_t>(data[i]) << 8));
        for (uint32_t bit = 0; bit < 8; bit++) {
            crc = stepSyndrome(crc);
        }
    }
    return crc;
}

Result correctSingleBit(uint8_t* frame, uint32_t len, uint16_t expectedToken, uint16_t& bitIndexOut) {
    // 1. Anything that cannot be a LoRa-borne TC frame is left strictly alone.
    if (frame == nullptr || len < MIN_FRAME_BYTES || len > MAX_FRAME_BYTES) {
        return Result::PASS_THROUGH;
    }

    const uint32_t dataLen = len - TC_TRAILER_SIZE;
    const uint32_t dataBits = 8u * dataLen;

    // 2. Syndrome. Zero means the FECF already agrees with the data.
    const uint16_t syndrome = static_cast<uint16_t>(crc16Ccitt(frame, dataLen) ^ readBE16(frame + dataLen));
    if (syndrome == 0u) {
        return Result::VALID;
    }

    // 3. A power-of-two syndrome is a bit of the FECF itself (and, by the
    //    being disjoint from every data-bit syndrome, nothing else). No post-check is needed:
    //    the data bytes were never in doubt.
    if ((syndrome & static_cast<uint16_t>(syndrome - 1u)) == 0u) {
        uint32_t k = 0;
        while ((syndrome >> k) != 1u) {
            k++;
        }
        const uint32_t bitPos = dataBits + (15u - k);
        flipBit(frame, bitPos);
        bitIndexOut = static_cast<uint16_t>(bitPos);
        return Result::CORRECTED;
    }

    // 4. Walk the data-bit syndromes backwards from the last bit. This is the
    //    whole search: O(8*len) 16-bit shifts and NO CRC pass inside the loop.
    //    Because every single-bit syndrome is distinct, the first match is the
    //    only candidate there will ever be.
    uint16_t s = LAST_DATA_BIT_SYNDROME;
    uint32_t candidateBit = dataBits;  // sentinel: dataBits means "no match"
    for (uint32_t d = 0; d < dataBits; d++) {
        if (s == syndrome) {
            candidateBit = dataBits - 1u - d;
            break;
        }
        s = stepSyndrome(s);
    }

    // 5. No single-bit error explains this syndrome (two or more bits flipped).
    if (candidateBit == dataBits) {
        return Result::UNCORRECTABLE;
    }

    // 6. One candidate, verified exactly once — never a search over candidates.
    flipBit(frame, candidateBit);
    if (postCheck(frame, len, dataLen, expectedToken)) {
        bitIndexOut = static_cast<uint16_t>(candidateBit);
        return Result::CORRECTED;
    }

    // Post-check failed: this was not a single-bit error after all. Undo the
    // speculative flip so the caller forwards the original bytes.
    flipBit(frame, candidateBit);
    return Result::UNCORRECTABLE;
}

}  // namespace TcFrameCorrection
}  // namespace Components
