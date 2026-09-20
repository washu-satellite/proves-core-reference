// ======================================================================
// \title  test_TcFrameCorrector_Codec.cpp
// \brief  Host tests for the pure single-bit TC frame corrector codec.
//
// The codec is F Prime free, so these tests link it directly. Two independent
// checks guard the syndrome algebra: an independent bit-serial CRC (a
// different formulation from the codec's byte-wise one) builds every frame,
// and a brute-force oracle that re-runs the CRC over every candidate flip is
// required to agree with the codec's O(8*len) syndrome walk. The brute force
// exists ONLY here — the flight code must never search candidates by CRC,
// because it runs in the Zephyr system workqueue.
// ======================================================================

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "PROVESFlightControllerReference/Components/TcFrameCorrector/TcFrameCorrectorCodec.hpp"

namespace {

namespace TFC = Components::TcFrameCorrection;

using TFC::Result;

//! The flight token: bypass flag (bit 13) OR spacecraft ID 0x0044.
constexpr uint16_t FLIGHT_TOKEN = 0x2044;

//! \brief CRC-16/CCITT-FALSE, bit-serial. Deliberately a different formulation
//! from the codec's byte-wise implementation so a shared mistake cannot hide.
uint16_t referenceCrc(const uint8_t* data, uint32_t len) {
    uint16_t crc = 0xFFFF;
    for (uint32_t i = 0; i < len; i++) {
        for (int b = 7; b >= 0; b--) {
            const bool inputBit = ((data[i] >> b) & 1u) != 0u;
            const bool msb = (crc & 0x8000u) != 0u;
            crc = static_cast<uint16_t>(crc << 1);
            if (msb != inputBit) {
                crc = static_cast<uint16_t>(crc ^ 0x1021u);
            }
        }
    }
    return crc;
}

//! \brief Build a well-formed CCSDS TC transfer frame of exactly len bytes.
//!
//! flagsAndScId = token, vcIdAndLength = (len - 1) with virtual channel 0,
//! frameSequenceNum = 0, deterministic pseudo-random payload, FECF from the
//! independent reference CRC.
std::vector<uint8_t> makeFrame(uint32_t len, uint16_t token = FLIGHT_TOKEN, uint32_t seed = 1) {
    std::vector<uint8_t> frame(len, 0);
    const uint32_t dataLen = len - 2;
    frame[0] = static_cast<uint8_t>(token >> 8);
    frame[1] = static_cast<uint8_t>(token & 0xFFu);
    const uint16_t vcIdAndLength = static_cast<uint16_t>((len - 1) & 0x03FFu);
    frame[2] = static_cast<uint8_t>(vcIdAndLength >> 8);
    frame[3] = static_cast<uint8_t>(vcIdAndLength & 0xFFu);
    frame[4] = 0;
    uint32_t state = seed;
    for (uint32_t i = 5; i < dataLen; i++) {
        state = (state * 1103515245u) + 12345u;
        frame[i] = static_cast<uint8_t>((state >> 16) & 0xFFu);
    }
    const uint16_t crc = referenceCrc(frame.data(), dataLen);
    frame[dataLen] = static_cast<uint8_t>(crc >> 8);
    frame[dataLen + 1] = static_cast<uint8_t>(crc & 0xFFu);
    return frame;
}

//! Flip one bit, counted MSB-first from the start of the buffer.
void flip(std::vector<uint8_t>& buf, uint32_t bitPos) {
    buf[bitPos / 8] = static_cast<uint8_t>(buf[bitPos / 8] ^ (0x80u >> (bitPos % 8)));
}

//! \brief Test oracle: try every single-bit flip and count how many make the
//! FECF check out. Costs a full CRC pass per candidate; never used in flight.
std::vector<uint32_t> bruteForceCandidates(const std::vector<uint8_t>& frame) {
    std::vector<uint32_t> hits;
    const uint32_t dataLen = static_cast<uint32_t>(frame.size()) - 2;
    for (uint32_t bit = 0; bit < 8u * frame.size(); bit++) {
        std::vector<uint8_t> candidate = frame;
        flip(candidate, bit);
        const uint16_t fecf =
            static_cast<uint16_t>((static_cast<uint16_t>(candidate[dataLen]) << 8) | candidate[dataLen + 1]);
        if (referenceCrc(candidate.data(), dataLen) == fecf) {
            hits.push_back(bit);
        }
    }
    return hits;
}

TEST(TcFrameCorrectorCodec, Crc16KnownVector) {
    RecordProperty("verifies", "TcFrameCorrector-1");
    const char* vector = "123456789";
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(vector);
    // CRC-16/CCITT-FALSE check value, as produced by Svc::Ccsds::Utils::CRC16.
    EXPECT_EQ(TFC::crc16Ccitt(bytes, 9), 0x29B1u);
    // The two independent formulations must agree, here and on real frames.
    EXPECT_EQ(referenceCrc(bytes, 9), 0x29B1u);
    for (uint32_t len : {7u, 16u, 24u, 32u, 64u, 252u}) {
        const std::vector<uint8_t> frame = makeFrame(len);
        EXPECT_EQ(TFC::crc16Ccitt(frame.data(), len - 2), referenceCrc(frame.data(), len - 2)) << "len " << len;
    }
}

TEST(TcFrameCorrectorCodec, ValidFrameUntouched) {
    RecordProperty("verifies", "TcFrameCorrector-1");
    for (uint32_t len : {7u, 16u, 64u, 252u}) {
        std::vector<uint8_t> frame = makeFrame(len);
        const std::vector<uint8_t> original = frame;
        uint16_t bitIndex = 0xFFFF;
        EXPECT_EQ(TFC::correctSingleBit(frame.data(), len, FLIGHT_TOKEN, bitIndex), Result::VALID) << "len " << len;
        EXPECT_EQ(frame, original) << "len " << len;
    }
}

TEST(TcFrameCorrectorCodec, EverySingleBitFlipIsRestored) {
    RecordProperty("verifies", "TcFrameCorrector-1,CH-L2-20");
    for (uint32_t len : {7u, 16u, 64u, 252u}) {
        const std::vector<uint8_t> original = makeFrame(len);
        for (uint32_t bit = 0; bit < 8u * len; bit++) {
            std::vector<uint8_t> frame = original;
            flip(frame, bit);
            uint16_t bitIndex = 0xFFFF;
            ASSERT_EQ(TFC::correctSingleBit(frame.data(), len, FLIGHT_TOKEN, bitIndex), Result::CORRECTED)
                << "len " << len << " bit " << bit;
            EXPECT_EQ(bitIndex, bit) << "len " << len << " bit " << bit;
            EXPECT_EQ(frame, original) << "len " << len << " bit " << bit;
        }
    }
}

TEST(TcFrameCorrectorCodec, BruteForceOracleAgreesWithSyndrome) {
    RecordProperty("verifies", "TcFrameCorrector-1,CH-L2-20");
    constexpr uint32_t LEN = 32;
    const std::vector<uint8_t> original = makeFrame(LEN);
    for (uint32_t bit = 0; bit < 8u * LEN; bit++) {
        std::vector<uint8_t> corrupted = original;
        flip(corrupted, bit);

        // The oracle re-runs the CRC over every candidate flip.
        const std::vector<uint32_t> hits = bruteForceCandidates(corrupted);
        ASSERT_EQ(hits.size(), 1u) << "bit " << bit;
        EXPECT_EQ(hits[0], bit) << "bit " << bit;

        // The codec's syndrome walk must reach the same conclusion.
        std::vector<uint8_t> frame = corrupted;
        uint16_t bitIndex = 0xFFFF;
        ASSERT_EQ(TFC::correctSingleBit(frame.data(), LEN, FLIGHT_TOKEN, bitIndex), Result::CORRECTED) << "bit " << bit;
        EXPECT_EQ(static_cast<uint32_t>(bitIndex), hits[0]) << "bit " << bit;
    }
}

TEST(TcFrameCorrectorCodec, EveryTwoBitErrorIsRejectedUnchanged) {
    RecordProperty("verifies", "TcFrameCorrector-2,CH-L2-20");
    constexpr uint32_t LEN = 24;
    const std::vector<uint8_t> original = makeFrame(LEN);
    const uint32_t bits = 8u * LEN;
    for (uint32_t i = 0; i < bits; i++) {
        for (uint32_t j = i + 1; j < bits; j++) {
            std::vector<uint8_t> frame = original;
            flip(frame, i);
            flip(frame, j);
            const std::vector<uint8_t> corrupted = frame;
            uint16_t bitIndex = 0xFFFF;
            ASSERT_EQ(TFC::correctSingleBit(frame.data(), LEN, FLIGHT_TOKEN, bitIndex), Result::UNCORRECTABLE)
                << "bits " << i << "," << j;
            ASSERT_EQ(frame, corrupted) << "bits " << i << "," << j;
        }
    }
}

TEST(TcFrameCorrectorCodec, TooShortTooLongAndPaddedFramesPassThroughUnchanged) {
    RecordProperty("verifies", "TcFrameCorrector-5");
    uint16_t bitIndex = 0xFFFF;

    // Below MIN_FRAME_BYTES: not a frame, never touched.
    std::vector<uint8_t> tooShort(TFC::MIN_FRAME_BYTES - 1, 0xA5);
    const std::vector<uint8_t> tooShortOriginal = tooShort;
    EXPECT_EQ(TFC::correctSingleBit(tooShort.data(), static_cast<uint32_t>(tooShort.size()), FLIGHT_TOKEN, bitIndex),
              Result::PASS_THROUGH);
    EXPECT_EQ(tooShort, tooShortOriginal);

    // Above MAX_FRAME_BYTES: beyond the LoRa packet cap, never touched.
    std::vector<uint8_t> tooLong(TFC::MAX_FRAME_BYTES + 1, 0x5A);
    const std::vector<uint8_t> tooLongOriginal = tooLong;
    EXPECT_EQ(TFC::correctSingleBit(tooLong.data(), static_cast<uint32_t>(tooLong.size()), FLIGHT_TOKEN, bitIndex),
              Result::PASS_THROUGH);
    EXPECT_EQ(tooLong, tooLongOriginal);

    // A valid 20-byte frame followed by one pad byte: the buffer is 21 bytes but
    // the frame declares 20, so the post-check rejects any speculative flip and
    // the bytes come back exactly as they went in.
    std::vector<uint8_t> padded = makeFrame(20);
    padded.push_back(0x00);
    const std::vector<uint8_t> paddedOriginal = padded;
    const Result paddedResult =
        TFC::correctSingleBit(padded.data(), static_cast<uint32_t>(padded.size()), FLIGHT_TOKEN, bitIndex);
    EXPECT_NE(paddedResult, Result::CORRECTED);
    EXPECT_EQ(padded, paddedOriginal);
}

TEST(TcFrameCorrectorCodec, WrongTokenAfterCorrectionIsRestored) {
    RecordProperty("verifies", "TcFrameCorrector-5");
    constexpr uint32_t LEN = 20;
    // A CRC-consistent frame carrying a token that is not this spacecraft's.
    const std::vector<uint8_t> original = makeFrame(LEN, 0x2045);
    std::vector<uint8_t> frame = original;
    const uint32_t payloadBit = 8u * 10u + 3u;
    flip(frame, payloadBit);
    const std::vector<uint8_t> corrupted = frame;

    uint16_t bitIndex = 0xFFFF;
    // The syndrome does point at payloadBit, but the post-check sees the wrong
    // token, so the speculative flip is undone and nothing is claimed.
    EXPECT_EQ(TFC::correctSingleBit(frame.data(), LEN, FLIGHT_TOKEN, bitIndex), Result::UNCORRECTABLE);
    EXPECT_EQ(frame, corrupted);

    // The same frame with the matching token is corrected normally, proving the
    // rejection above came from the token check and nothing else.
    const std::vector<uint8_t> matching = makeFrame(LEN, FLIGHT_TOKEN);
    std::vector<uint8_t> matchingCorrupted = matching;
    flip(matchingCorrupted, payloadBit);
    EXPECT_EQ(TFC::correctSingleBit(matchingCorrupted.data(), LEN, FLIGHT_TOKEN, bitIndex), Result::CORRECTED);
    EXPECT_EQ(static_cast<uint32_t>(bitIndex), payloadBit);
    EXPECT_EQ(matchingCorrupted, matching);
}

}  // namespace
