// ======================================================================
// \title  test_Crc16.cpp
// \brief  Host tests for the shared header-only CRC-16/CCITT-FALSE.
//
// Crc16::ccitt was extracted from TcFrameCorrectorCodec on its second use
// (the driver-board wire protocol, Cycle E). These tests pin the algorithm
// (the published CCITT-FALSE check value), the empty-buffer convention the
// corrector relies on (0xFFFF), and that the corrector's public crc16Ccitt
// still computes the same value on the same bytes, so the extraction cannot
// have changed what the corrector sees. No requirement ID is claimed: there is
// no Crc16 group in the requirements tables.
// ======================================================================

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

#include "PROVESFlightControllerReference/Components/Crc16/Crc16.hpp"
#include "PROVESFlightControllerReference/Components/TcFrameCorrector/TcFrameCorrectorCodec.hpp"

namespace {

//! Longest frame the corrector checksums (TcFrameCorrectorCodec.hpp MAX_FRAME_BYTES).
constexpr uint32_t LORA_MAX_BYTES = 252;

//! \brief CRC-16/CCITT-FALSE, bit-serial: a different formulation from the
//! byte-wise loop in Crc16.hpp so a shared mistake cannot hide.
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

//! Fill a buffer with a deterministic pseudo-random pattern (LCG), seeded so
//! that every byte value appears and no run is periodic over 252 bytes.
void fillPattern(uint8_t* buf, uint32_t len, uint32_t seed) {
    uint32_t x = seed;
    for (uint32_t i = 0; i < len; i++) {
        x = x * 1664525u + 1013904223u;
        buf[i] = static_cast<uint8_t>(x >> 24);
    }
}

TEST(Crc16, CheckValueFor123456789Is0x29B1) {
    const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT_EQ(Crc16::ccitt(check, sizeof(check)), 0x29B1u);
}

TEST(Crc16, EmptyBufferReturnsSeed0xFFFF) {
    const uint8_t one = 0x00;
    EXPECT_EQ(Crc16::ccitt(&one, 0), 0xFFFFu);
    // A null pointer is treated as empty, whatever length is claimed: the
    // corrector documents that convention on crc16Ccitt.
    EXPECT_EQ(Crc16::ccitt(nullptr, 0), 0xFFFFu);
    EXPECT_EQ(Crc16::ccitt(nullptr, 17), 0xFFFFu);
}

TEST(Crc16, EqualsTcFrameCorrectorCrc16CcittOnFiveBuffers) {
    // Buffers of assorted sizes and content, the last one at the LoRa maximum.
    const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    const uint8_t zero = 0x00;
    const uint8_t ones = 0xFF;
    const uint8_t header[] = {0x20, 0x44, 0x00, 0x06, 0x01, 0xAB, 0xCD};
    uint8_t lora[LORA_MAX_BYTES];
    fillPattern(lora, LORA_MAX_BYTES, 0x5CA1A2u);

    struct Case {
        const uint8_t* data;
        uint32_t len;
    };
    const Case cases[] = {
        {check, sizeof(check)}, {&zero, 1}, {&ones, 1}, {header, sizeof(header)}, {lora, LORA_MAX_BYTES},
    };

    for (const Case& c : cases) {
        const uint16_t shared = Crc16::ccitt(c.data, c.len);
        EXPECT_EQ(shared, Components::TcFrameCorrection::crc16Ccitt(c.data, c.len)) << "len " << c.len;
        EXPECT_EQ(shared, referenceCrc(c.data, c.len)) << "len " << c.len;
    }
}

TEST(Crc16, AllZeroAndAllOnesAtLoraMaximumAgreeWithReference) {
    uint8_t buf[LORA_MAX_BYTES];
    std::memset(buf, 0x00, sizeof(buf));
    EXPECT_EQ(Crc16::ccitt(buf, sizeof(buf)), referenceCrc(buf, sizeof(buf)));
    EXPECT_EQ(Crc16::ccitt(buf, sizeof(buf)), Components::TcFrameCorrection::crc16Ccitt(buf, sizeof(buf)));
    std::memset(buf, 0xFF, sizeof(buf));
    EXPECT_EQ(Crc16::ccitt(buf, sizeof(buf)), referenceCrc(buf, sizeof(buf)));
    EXPECT_EQ(Crc16::ccitt(buf, sizeof(buf)), Components::TcFrameCorrection::crc16Ccitt(buf, sizeof(buf)));
}

}  // namespace
