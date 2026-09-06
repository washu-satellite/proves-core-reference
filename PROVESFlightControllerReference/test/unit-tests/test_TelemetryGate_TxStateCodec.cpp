#include <gtest/gtest.h>

#include <cstdint>

#include "PROVESFlightControllerReference/Components/TelemetryGate/TxStateCodec.hpp"

using Components::decodeTxState;
using Components::encodeTxState;
using Components::TX_STATE_DISABLED;
using Components::TX_STATE_ENABLED;
using Components::TX_STATE_ENCODED_SIZE;
using Components::TxStateDecodeStatus;

namespace {

// Encode a state into a fresh vector-like buffer and return it as an array.
struct Blob {
    uint8_t bytes[TX_STATE_ENCODED_SIZE];
    uint32_t len;
};

Blob makeBlob(uint8_t state) {
    Blob b{};
    b.len = encodeTxState(state, b.bytes, TX_STATE_ENCODED_SIZE);
    return b;
}

}  // namespace

// ---- Roundtrip -------------------------------------------------------------

TEST(TxStateCodecTest, RoundtripEnabled) {
    Blob b = makeBlob(TX_STATE_ENABLED);
    ASSERT_EQ(b.len, TX_STATE_ENCODED_SIZE);

    uint8_t out = 0xEE;
    EXPECT_EQ(decodeTxState(b.bytes, b.len, &out), TxStateDecodeStatus::OK);
    EXPECT_EQ(out, TX_STATE_ENABLED);
}

TEST(TxStateCodecTest, RoundtripDisabled) {
    Blob b = makeBlob(TX_STATE_DISABLED);
    ASSERT_EQ(b.len, TX_STATE_ENCODED_SIZE);

    uint8_t out = 0xEE;
    EXPECT_EQ(decodeTxState(b.bytes, b.len, &out), TxStateDecodeStatus::OK);
    EXPECT_EQ(out, TX_STATE_DISABLED);
}

TEST(TxStateCodecTest, DecodeToleratesNullOutState) {
    Blob b = makeBlob(TX_STATE_DISABLED);
    EXPECT_EQ(decodeTxState(b.bytes, b.len, nullptr), TxStateDecodeStatus::OK);
}

// ---- Encode argument validation --------------------------------------------

TEST(TxStateCodecTest, EncodeRejectsUndefinedState) {
    uint8_t buf[TX_STATE_ENCODED_SIZE] = {0};
    EXPECT_EQ(encodeTxState(2, buf, TX_STATE_ENCODED_SIZE), 0u);
    EXPECT_EQ(encodeTxState(0xFF, buf, TX_STATE_ENCODED_SIZE), 0u);
}

TEST(TxStateCodecTest, EncodeRejectsTooSmallBuffer) {
    uint8_t buf[TX_STATE_ENCODED_SIZE] = {0};
    EXPECT_EQ(encodeTxState(TX_STATE_ENABLED, buf, TX_STATE_ENCODED_SIZE - 1), 0u);
}

TEST(TxStateCodecTest, EncodeRejectsNullBuffer) {
    EXPECT_EQ(encodeTxState(TX_STATE_ENABLED, nullptr, TX_STATE_ENCODED_SIZE), 0u);
}

// ---- Corruption detection --------------------------------------------------

TEST(TxStateCodecTest, CorruptMagicDetected) {
    Blob b = makeBlob(TX_STATE_ENABLED);
    b.bytes[0] ^= 0xFF;  // flip a magic byte
    uint8_t out = 0xEE;
    EXPECT_EQ(decodeTxState(b.bytes, b.len, &out), TxStateDecodeStatus::BAD_MAGIC);
}

TEST(TxStateCodecTest, CorruptStateByteDetected) {
    // Flipping the state field to the other valid value must still be rejected
    // because the integrity byte no longer matches.
    Blob b = makeBlob(TX_STATE_ENABLED);
    b.bytes[4] = TX_STATE_DISABLED;  // integrity still encodes ENABLED
    uint8_t out = 0xEE;
    EXPECT_NE(decodeTxState(b.bytes, b.len, &out), TxStateDecodeStatus::OK);
}

TEST(TxStateCodecTest, CorruptStateByteToUndefinedDetected) {
    Blob b = makeBlob(TX_STATE_ENABLED);
    b.bytes[4] = 0x7F;  // undefined state value
    uint8_t out = 0xEE;
    EXPECT_NE(decodeTxState(b.bytes, b.len, &out), TxStateDecodeStatus::OK);
}

TEST(TxStateCodecTest, CorruptIntegrityByteDetected) {
    Blob b = makeBlob(TX_STATE_DISABLED);
    b.bytes[5] ^= 0x01;  // flip integrity byte
    uint8_t out = 0xEE;
    EXPECT_EQ(decodeTxState(b.bytes, b.len, &out), TxStateDecodeStatus::BAD_INTEGRITY);
}

TEST(TxStateCodecTest, EverySingleByteFlipDetected) {
    // Exhaustive: every single-bit flip in a valid blob must be rejected.
    for (uint8_t state : {TX_STATE_ENABLED, TX_STATE_DISABLED}) {
        Blob good = makeBlob(state);
        for (uint32_t i = 0; i < TX_STATE_ENCODED_SIZE; ++i) {
            for (int bit = 0; bit < 8; ++bit) {
                Blob bad = good;
                bad.bytes[i] ^= static_cast<uint8_t>(1u << bit);
                uint8_t out = 0xEE;
                EXPECT_NE(decodeTxState(bad.bytes, bad.len, &out), TxStateDecodeStatus::OK)
                    << "single-bit flip at byte " << i << " bit " << bit << " not detected";
            }
        }
    }
}

// ---- Length / truncation ---------------------------------------------------

TEST(TxStateCodecTest, TruncatedBufferDetected) {
    Blob b = makeBlob(TX_STATE_ENABLED);
    uint8_t out = 0xEE;
    EXPECT_EQ(decodeTxState(b.bytes, TX_STATE_ENCODED_SIZE - 1, &out), TxStateDecodeStatus::BAD_LENGTH);
}

TEST(TxStateCodecTest, OversizedBufferDetected) {
    Blob b = makeBlob(TX_STATE_ENABLED);
    uint8_t out = 0xEE;
    EXPECT_EQ(decodeTxState(b.bytes, TX_STATE_ENCODED_SIZE + 1, &out), TxStateDecodeStatus::BAD_LENGTH);
}

TEST(TxStateCodecTest, ZeroLengthBufferDetected) {
    uint8_t dummy = 0;
    uint8_t out = 0xEE;
    EXPECT_EQ(decodeTxState(&dummy, 0, &out), TxStateDecodeStatus::BAD_LENGTH);
}

TEST(TxStateCodecTest, NullBufferDetected) {
    uint8_t out = 0xEE;
    EXPECT_EQ(decodeTxState(nullptr, TX_STATE_ENCODED_SIZE, &out), TxStateDecodeStatus::BAD_LENGTH);
}
