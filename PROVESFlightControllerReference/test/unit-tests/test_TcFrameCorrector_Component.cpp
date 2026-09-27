// ======================================================================
// \title  test_TcFrameCorrector_Component.cpp
// \brief  Component-level host tests for TcFrameCorrector.
//
// Compiles the real TcFrameCorrector.cpp against the stubs in support/
// (TcFrameCorrectorComponentAc.hpp records outgoing port calls, telemetry and
// events; the FppConstantsAc.hpp stubs supply the frame token) and drives the
// actual handlers. Covers what the codec test cannot: the parameter gate, the
// byte-identity of the disabled pass-through, the event/telemetry effects, and
// the buffer-ownership invariant in both directions.
// ======================================================================

#include <gtest/gtest.h>

#include <vector>

#include "PROVESFlightControllerReference/Components/TcFrameCorrector/TcFrameCorrector.hpp"
#include "PROVESFlightControllerReference/Components/TcFrameCorrector/TcFrameCorrectorCodec.hpp"

namespace {

namespace TFC = Components::TcFrameCorrection;

using Components::TcFrameCorrector;
using Components::TcFrameCorrectorComponentBase;

constexpr uint16_t FLIGHT_TOKEN = 0x2044;

//! Build a well-formed CCSDS TC transfer frame of exactly len bytes. The codec
//! CRC is trusted here because test_TcFrameCorrector_Codec.cpp checks it
//! against an independent bit-serial implementation and a known vector.
std::vector<U8> makeFrame(uint32_t len) {
    std::vector<U8> frame(len, 0);
    const uint32_t dataLen = len - 2;
    frame[0] = static_cast<U8>(FLIGHT_TOKEN >> 8);
    frame[1] = static_cast<U8>(FLIGHT_TOKEN & 0xFFu);
    const U16 vcIdAndLength = static_cast<U16>((len - 1) & 0x03FFu);
    frame[2] = static_cast<U8>(vcIdAndLength >> 8);
    frame[3] = static_cast<U8>(vcIdAndLength & 0xFFu);
    frame[4] = 0;
    uint32_t state = 7;
    for (uint32_t i = 5; i < dataLen; i++) {
        state = (state * 1103515245u) + 12345u;
        frame[i] = static_cast<U8>((state >> 16) & 0xFFu);
    }
    const U16 crc = TFC::crc16Ccitt(frame.data(), dataLen);
    frame[dataLen] = static_cast<U8>(crc >> 8);
    frame[dataLen + 1] = static_cast<U8>(crc & 0xFFu);
    return frame;
}

void flip(std::vector<U8>& buf, uint32_t bitPos) {
    buf[bitPos / 8] = static_cast<U8>(buf[bitPos / 8] ^ (0x80u >> (bitPos % 8)));
}

//! A distinctive context so a test can prove the very same values travelled
//! through, not a default-constructed replacement.
ComCfg::FrameContext makeContext() {
    ComCfg::FrameContext context;
    context.comQueueIndex = 2;
    context.apid = 0x0007;
    context.sequenceCount = 1234;
    context.vcId = 3;
    context.authenticated = true;
    return context;
}

class TcFrameCorrectorComponentTest : public ::testing::Test {
  protected:
    //! Drive a packet through the component's real dataIn handler.
    static void uplink(TcFrameCorrector& corrector, Fw::Buffer& buffer, const ComCfg::FrameContext& context) {
        static_cast<TcFrameCorrectorComponentBase&>(corrector).dataIn_handler(0, buffer, context);
    }

    //! Hand a buffer back through the component's real dataReturnIn handler.
    static void returnBuffer(TcFrameCorrector& corrector, Fw::Buffer& buffer, const ComCfg::FrameContext& context) {
        static_cast<TcFrameCorrectorComponentBase&>(corrector).dataReturnIn_handler(0, buffer, context);
    }
};

TEST_F(TcFrameCorrectorComponentTest, DisabledIsByteIdenticalPassThrough) {
    RecordProperty("verifies", "TcFrameCorrector-3");
    TcFrameCorrector corrector("corrector");
    corrector.correctionEnabled = false;

    std::vector<U8> bytes = makeFrame(64);
    flip(bytes, 100);  // a corrupt frame: still must not be touched
    const std::vector<U8> corrupted = bytes;
    const ComCfg::FrameContext context = makeContext();

    Fw::Buffer buffer(bytes.data(), bytes.size());
    uplink(corrector, buffer, context);

    // The same buffer object, forwarded once, with not one byte altered.
    ASSERT_EQ(corrector.dataOutCalls.size(), 1u);
    EXPECT_EQ(corrector.dataOutCalls[0].data, bytes.data());
    EXPECT_EQ(corrector.dataOutCalls[0].size, static_cast<FwSizeType>(bytes.size()));
    EXPECT_TRUE(corrector.dataOutCalls[0].context == context);
    EXPECT_EQ(bytes, corrupted);

    // Nothing is reported while disabled.
    EXPECT_TRUE(corrector.eventsFrameCorrected.empty());
    EXPECT_TRUE(corrector.eventsFrameUncorrectable.empty());
    EXPECT_TRUE(corrector.tlmCorrectedFrames.empty());
    EXPECT_TRUE(corrector.tlmUncorrectableFrames.empty());
    EXPECT_TRUE(corrector.dataReturnOutCalls.empty());
}

TEST_F(TcFrameCorrectorComponentTest, ParamInvalidBehavesAsDisabled) {
    RecordProperty("verifies", "TcFrameCorrector-3");
    TcFrameCorrector corrector("corrector");
    // Parameter reads back as enabled but the read itself is not trustworthy.
    corrector.correctionEnabled = true;
    corrector.paramValidity = Fw::ParamValid::INVALID;

    std::vector<U8> bytes = makeFrame(32);
    flip(bytes, 55);
    const std::vector<U8> corrupted = bytes;

    Fw::Buffer buffer(bytes.data(), bytes.size());
    uplink(corrector, buffer, makeContext());

    ASSERT_EQ(corrector.dataOutCalls.size(), 1u);
    EXPECT_EQ(bytes, corrupted);
    EXPECT_TRUE(corrector.eventsFrameCorrected.empty());
    EXPECT_TRUE(corrector.tlmCorrectedFrames.empty());
}

TEST_F(TcFrameCorrectorComponentTest, EnabledCorrectsAndForwards) {
    RecordProperty("verifies", "TcFrameCorrector-1,CH-L2-20");
    TcFrameCorrector corrector("corrector");
    corrector.correctionEnabled = true;

    const std::vector<U8> original = makeFrame(64);
    std::vector<U8> bytes = original;
    constexpr uint32_t FLIPPED_BIT = 137;
    flip(bytes, FLIPPED_BIT);
    const ComCfg::FrameContext context = makeContext();

    Fw::Buffer buffer(bytes.data(), bytes.size());
    uplink(corrector, buffer, context);

    // Repaired in place, in the very buffer that came in, and forwarded once.
    EXPECT_EQ(bytes, original);
    ASSERT_EQ(corrector.dataOutCalls.size(), 1u);
    EXPECT_EQ(corrector.dataOutCalls[0].data, bytes.data());
    EXPECT_TRUE(corrector.dataOutCalls[0].context == context);

    ASSERT_EQ(corrector.eventsFrameCorrected.size(), 1u);
    EXPECT_EQ(corrector.eventsFrameCorrected[0].bitIndex, FLIPPED_BIT);
    EXPECT_EQ(corrector.eventsFrameCorrected[0].frameLength, 64u);
    ASSERT_EQ(corrector.tlmCorrectedFrames.size(), 1u);
    EXPECT_EQ(corrector.tlmCorrectedFrames[0], 1u);
    EXPECT_TRUE(corrector.eventsFrameUncorrectable.empty());

    // A second correction advances the counter rather than restating it.
    std::vector<U8> second = original;
    flip(second, 200);
    Fw::Buffer secondBuffer(second.data(), second.size());
    uplink(corrector, secondBuffer, context);
    ASSERT_EQ(corrector.tlmCorrectedFrames.size(), 2u);
    EXPECT_EQ(corrector.tlmCorrectedFrames[1], 2u);
}

TEST_F(TcFrameCorrectorComponentTest, ValidFrameIsForwardedSilently) {
    RecordProperty("verifies", "TcFrameCorrector-1");
    TcFrameCorrector corrector("corrector");
    corrector.correctionEnabled = true;

    const std::vector<U8> original = makeFrame(48);
    std::vector<U8> bytes = original;
    Fw::Buffer buffer(bytes.data(), bytes.size());
    uplink(corrector, buffer, makeContext());

    EXPECT_EQ(bytes, original);
    ASSERT_EQ(corrector.dataOutCalls.size(), 1u);
    EXPECT_TRUE(corrector.eventsFrameCorrected.empty());
    EXPECT_TRUE(corrector.eventsFrameUncorrectable.empty());
    EXPECT_TRUE(corrector.tlmCorrectedFrames.empty());
    EXPECT_TRUE(corrector.tlmUncorrectableFrames.empty());
}

TEST_F(TcFrameCorrectorComponentTest, UncorrectableForwardedUnchanged) {
    RecordProperty("verifies", "TcFrameCorrector-2,TcFrameCorrector-5");
    TcFrameCorrector corrector("corrector");
    corrector.correctionEnabled = true;

    std::vector<U8> bytes = makeFrame(64);
    flip(bytes, 17);
    flip(bytes, 300);  // two-bit error: never correctable
    const std::vector<U8> corrupted = bytes;
    const ComCfg::FrameContext context = makeContext();

    Fw::Buffer buffer(bytes.data(), bytes.size());
    uplink(corrector, buffer, context);

    // Forwarded unchanged so the frame accumulator drops it exactly as today.
    EXPECT_EQ(bytes, corrupted);
    ASSERT_EQ(corrector.dataOutCalls.size(), 1u);
    EXPECT_EQ(corrector.dataOutCalls[0].data, bytes.data());
    EXPECT_EQ(corrector.dataOutCalls[0].size, static_cast<FwSizeType>(bytes.size()));
    EXPECT_TRUE(corrector.dataOutCalls[0].context == context);

    ASSERT_EQ(corrector.eventsFrameUncorrectable.size(), 1u);
    EXPECT_EQ(corrector.eventsFrameUncorrectable[0], 64u);
    ASSERT_EQ(corrector.tlmUncorrectableFrames.size(), 1u);
    EXPECT_EQ(corrector.tlmUncorrectableFrames[0], 1u);
    EXPECT_TRUE(corrector.eventsFrameCorrected.empty());
}

TEST_F(TcFrameCorrectorComponentTest, ReturnPathForwardsSameBufferAndContext) {
    RecordProperty("verifies", "TcFrameCorrector-4");
    TcFrameCorrector corrector("corrector");
    std::vector<U8> bytes = makeFrame(32);
    const std::vector<U8> original = bytes;
    const ComCfg::FrameContext context = makeContext();

    Fw::Buffer buffer(bytes.data(), bytes.size());
    returnBuffer(corrector, buffer, context);

    ASSERT_EQ(corrector.dataReturnOutCalls.size(), 1u);
    EXPECT_EQ(corrector.dataReturnOutCalls[0].data, bytes.data());
    EXPECT_EQ(corrector.dataReturnOutCalls[0].size, static_cast<FwSizeType>(bytes.size()));
    EXPECT_TRUE(corrector.dataReturnOutCalls[0].context == context);
    EXPECT_EQ(bytes, original);
    // The return path must never leak a buffer onto the forward path.
    EXPECT_TRUE(corrector.dataOutCalls.empty());
}

TEST_F(TcFrameCorrectorComponentTest, OwnershipIsOneInOneOutOnBothDirections) {
    RecordProperty("verifies", "TcFrameCorrector-4");
    // The three states that matter: disabled pass-through, a corrected frame,
    // and an uncorrectable frame. In every one, each buffer handed to dataIn
    // leaves exactly once on dataOut, and each buffer handed to dataReturnIn
    // leaves exactly once on dataReturnOut, carrying the same context.
    struct Case {
        const char* name;
        bool enabled;
        int flips;  // 0 = valid, 1 = correctable, 2 = uncorrectable
    };
    const Case cases[] = {{"disabled", false, 1}, {"corrected", true, 1}, {"uncorrectable", true, 2}};

    for (const Case& testCase : cases) {
        TcFrameCorrector corrector("corrector");
        corrector.correctionEnabled = testCase.enabled;
        const ComCfg::FrameContext context = makeContext();

        std::vector<U8> forward = makeFrame(64);
        if (testCase.flips >= 1) {
            flip(forward, 41);
        }
        if (testCase.flips >= 2) {
            flip(forward, 411);
        }
        std::vector<U8> back = makeFrame(24);

        Fw::Buffer forwardBuffer(forward.data(), forward.size());
        Fw::Buffer returnedBuffer(back.data(), back.size());
        uplink(corrector, forwardBuffer, context);
        returnBuffer(corrector, returnedBuffer, context);

        ASSERT_EQ(corrector.dataOutCalls.size(), 1u) << testCase.name;
        EXPECT_EQ(corrector.dataOutCalls[0].data, forward.data()) << testCase.name;
        EXPECT_EQ(corrector.dataOutCalls[0].size, static_cast<FwSizeType>(forward.size())) << testCase.name;
        EXPECT_TRUE(corrector.dataOutCalls[0].context == context) << testCase.name;

        ASSERT_EQ(corrector.dataReturnOutCalls.size(), 1u) << testCase.name;
        EXPECT_EQ(corrector.dataReturnOutCalls[0].data, back.data()) << testCase.name;
        EXPECT_EQ(corrector.dataReturnOutCalls[0].size, static_cast<FwSizeType>(back.size())) << testCase.name;
        EXPECT_TRUE(corrector.dataReturnOutCalls[0].context == context) << testCase.name;
    }
}

}  // namespace
