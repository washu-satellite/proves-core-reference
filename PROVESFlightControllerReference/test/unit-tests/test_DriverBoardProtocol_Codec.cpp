// ======================================================================
// \title  test_DriverBoardProtocol_Codec.cpp
// \brief  Host tests for the driver-board wire codec (DriverBoardProtocol-1..6).
//
// Written from the wire spec (docs-site/dev-loop/cycles/cycle-e-plan/02-protocol.md)
// and the pass criteria in 01-scope.md. Every expected byte sequence below is
// hand-written from the spec tables, never derived from the codec, so the
// tests are an independent reading of the spec that the STM32 firmware can
// be diffed against.
//
// The "every" criteria of DriverBoardProtocol-3 are exhaustive: every byte
// position of a valid frame is corrupted to every other value, and every
// truncation length is tried; each must yield no frame, exactly one counted
// rejection, and delivery of the valid frame that follows.
// ======================================================================

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#include "PROVESFlightControllerReference/Components/Crc16/Crc16.hpp"
#include "PROVESFlightControllerReference/Components/DriverBoardProtocol/DriverBoardMessages.hpp"
#include "PROVESFlightControllerReference/Components/DriverBoardProtocol/DriverBoardProtocol.hpp"

// ----------------------------------------------------------------------
// Global allocation counter (DriverBoardProtocol-4: no heap). Replacing the
// global operator new is program-wide; only the windows measured below matter.
// ----------------------------------------------------------------------
static size_t g_allocations = 0;

void* operator new(std::size_t n) {
    ++g_allocations;
    void* p = std::malloc(n == 0 ? 1 : n);
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}
void* operator new[](std::size_t n) {
    ++g_allocations;
    void* p = std::malloc(n == 0 ? 1 : n);
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}

namespace {

namespace DBP = Components::DriverBoardProtocol;
using DBP::Frame;
using DBP::Parser;
using DBP::Reject;
using DBP::Type;

// ----------------------------------------------------------------------
// Spec-derived helpers
// ----------------------------------------------------------------------

//! Build a frame exactly as the spec's Frame table says, independently of encode().
std::vector<uint8_t> specFrame(uint8_t type, uint8_t seq, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> f;
    f.push_back(0x5C);
    f.push_back(0xA1);
    f.push_back(type);
    f.push_back(seq);
    f.push_back(static_cast<uint8_t>(payload.size()));
    f.insert(f.end(), payload.begin(), payload.end());
    const uint16_t crc = Crc16::ccitt(&f[2], static_cast<uint32_t>(3 + payload.size()));
    f.push_back(static_cast<uint8_t>(crc >> 8));  // big-endian on the wire
    f.push_back(static_cast<uint8_t>(crc & 0xFF));
    return f;
}

//! Feed bytes; return how many feed() calls returned true and the index of the last one.
struct FeedResult {
    uint32_t ready;
    int lastReadyIndex;
};

FeedResult feedAll(Parser& p, const uint8_t* bytes, size_t n) {
    FeedResult r = {0, -1};
    for (size_t i = 0; i < n; i++) {
        if (p.feed(bytes[i])) {
            r.ready++;
            r.lastReadyIndex = static_cast<int>(i);
        }
    }
    return r;
}

FeedResult feedAll(Parser& p, const std::vector<uint8_t>& v) {
    return feedAll(p, v.data(), v.size());
}

bool frameEquals(const Frame& f, uint8_t type, uint8_t seq, const std::vector<uint8_t>& payload) {
    if (f.type != type || f.seq != seq || f.len != payload.size()) {
        return false;
    }
    return payload.empty() || std::memcmp(f.payload, payload.data(), payload.size()) == 0;
}

//! A 23-byte HK-type frame none of whose bytes (outside the real SYNC) is 0x5C
//! or 0xA1, so that no single-byte change can manufacture a second SYNC pair
//! and "exactly one rejection" is well defined. The SEQ is searched so the
//! CRC bytes obey the same rule.
std::vector<uint8_t> cleanFrame() {
    std::vector<uint8_t> payload;
    for (uint8_t i = 0; i < DBP::HK_LEN; i++) {
        payload.push_back(static_cast<uint8_t>(0x10 + i));  // 0x10..0x26: no 0x5C, no 0xA1
    }
    for (int seq = 0; seq < 256; seq++) {
        std::vector<uint8_t> f = specFrame(static_cast<uint8_t>(Type::HK), static_cast<uint8_t>(seq), payload);
        bool clean = true;
        for (size_t i = 2; i < f.size(); i++) {
            if (f[i] == 0x5C || f[i] == 0xA1) {
                clean = false;
            }
        }
        if (clean) {
            return f;
        }
    }
    return std::vector<uint8_t>();
}

// ----------------------------------------------------------------------
// DriverBoardProtocol-1: encode layout, CRC, round trip of every message type
// ----------------------------------------------------------------------

TEST(DriverBoardProtocolCodec, EncodeProducesSpecLayoutWithCrcOverTypeToPayload) {
    RecordProperty("verifies", "DriverBoardProtocol-1");
    const std::vector<uint8_t> payload = {0xF4, 0x01, 0x4B, 0x05, 0x02};  // PULSE 500 ms, 75 %, mask 5, pol 2
    uint8_t out[DBP::MAX_FRAME];
    const size_t n = DBP::encode(static_cast<uint8_t>(Type::PULSE), 9, payload.data(),
                                 static_cast<uint8_t>(payload.size()), out, sizeof(out));
    ASSERT_EQ(n, 7u + payload.size());
    const std::vector<uint8_t> expected = specFrame(0x05, 9, payload);
    EXPECT_EQ(std::vector<uint8_t>(out, out + n), expected);
    EXPECT_EQ(out[0], 0x5C);
    EXPECT_EQ(out[1], 0xA1);
    EXPECT_EQ(out[2], 0x05);
    EXPECT_EQ(out[3], 9);
    EXPECT_EQ(out[4], 5);
    const uint16_t crc = Crc16::ccitt(&out[2], 3 + 5);
    EXPECT_EQ(out[n - 2], static_cast<uint8_t>(crc >> 8));
    EXPECT_EQ(out[n - 1], static_cast<uint8_t>(crc & 0xFF));
}

//! One representative value set per message type, with the payload bytes
//! written by hand from the spec's field lists (little-endian integers).
template <class M>
void expectMessageRoundTrip(const M& msg, Type type, const std::vector<uint8_t>& expectedPayload) {
    uint8_t out[DBP::MAX_FRAME];
    const size_t n = DBP::encodeMessage(msg, 0x42, out, sizeof(out));
    ASSERT_EQ(n, 7u + expectedPayload.size()) << "type " << static_cast<int>(type);
    const std::vector<uint8_t> expected = specFrame(static_cast<uint8_t>(type), 0x42, expectedPayload);
    EXPECT_EQ(std::vector<uint8_t>(out, out + n), expected) << "type " << static_cast<int>(type);

    Parser p;
    const FeedResult r = feedAll(p, out, n);
    ASSERT_EQ(r.ready, 1u);
    const Frame f = p.frame();
    EXPECT_TRUE(frameEquals(f, static_cast<uint8_t>(type), 0x42, expectedPayload));
    M back;
    std::memset(&back, 0xEE, sizeof(back));
    ASSERT_TRUE(DBP::unpack(f, back));
    uint8_t again[DBP::MAX_FRAME];
    const size_t m = DBP::encodeMessage(back, 0x42, again, sizeof(again));
    ASSERT_EQ(m, n);
    EXPECT_EQ(std::memcmp(out, again, n), 0) << "type " << static_cast<int>(type);
}

TEST(DriverBoardProtocolCodec, EveryMessageTypeRoundTripsByteIdentical) {
    RecordProperty("verifies", "DriverBoardProtocol-1");
    // host -> board
    {
        DBP::Heartbeat m = {0x04030201u};
        expectMessageRoundTrip(m, Type::HEARTBEAT, {0x01, 0x02, 0x03, 0x04});
    }
    {
        DBP::HkRequest m = {};
        expectMessageRoundTrip(m, Type::HK_REQUEST, {});
    }
    {
        DBP::Arm m = {DBP::ARM_MAGIC};
        expectMessageRoundTrip(m, Type::ARM, {0xA5});
    }
    {
        DBP::Disarm m = {};
        expectMessageRoundTrip(m, Type::DISARM, {});
    }
    {
        DBP::Pulse m = {500, 75, 0x05, 0x02};
        expectMessageRoundTrip(m, Type::PULSE, {0xF4, 0x01, 0x4B, 0x05, 0x02});
    }
    {
        DBP::Abort m = {};
        expectMessageRoundTrip(m, Type::ABORT, {});
    }
    {
        DBP::Ping m = {};
        expectMessageRoundTrip(m, Type::PING, {});
    }
    {
        DBP::TimeSync m = {1700000000u, 999999u};  // 0x6553F100, 0x000F423F
        expectMessageRoundTrip(m, Type::TIME_SYNC, {0x00, 0xF1, 0x53, 0x65, 0x3F, 0x42, 0x0F, 0x00});
    }
    {
        DBP::StreamStart m = {50};
        expectMessageRoundTrip(m, Type::STREAM_START, {0x32});
    }
    {
        DBP::StreamStop m = {};
        expectMessageRoundTrip(m, Type::STREAM_STOP, {});
    }
    // board -> host
    {
        DBP::Ack m = {0x03, static_cast<uint8_t>(DBP::AckStatus::REFUSED_NOT_ARMED)};
        expectMessageRoundTrip(m, Type::ACK, {0x03, 0x01});
    }
    {
        DBP::Hk m = {};
        m.currentMa[0] = 1500;  // 0x05DC
        m.currentMa[1] = -200;  // 0xFF38
        m.currentMa[2] = 0;
        m.tempDeciC[0] = 251;  // 0x00FB
        m.tempDeciC[1] = 300;  // 0x012C
        m.dutyPct[0] = 50;     // 0x32
        m.dutyPct[1] = -50;    // 0xCE
        m.dutyPct[2] = 0;
        m.state = static_cast<uint8_t>(DBP::BoardState::ARMED);
        m.faultFlags = DBP::FAULT_OVERTEMP;
        m.uptimeMs = 0x11223344u;
        m.boardTickMs = 0xAABBCCDDu;
        expectMessageRoundTrip(m, Type::HK, {0xDC, 0x05, 0x38, 0xFF, 0x00, 0x00, 0xFB, 0x00, 0x2C, 0x01, 0x32, 0xCE,
                                             0x00, 0x02, 0x02, 0x44, 0x33, 0x22, 0x11, 0xDD, 0xCC, 0xBB, 0xAA});
    }
    {
        DBP::Pong m = {0x0102, DBP::PROTOCOL_VERSION, 0};
        expectMessageRoundTrip(m, Type::PONG, {0x02, 0x01, 0x01, 0x00});
    }
    {
        DBP::Sample m = {};
        m.tMs = 123456u;  // 0x0001E240
        m.currentMa[0] = -1;
        m.currentMa[1] = 2;
        m.currentMa[2] = -3;
        m.dutyPct[0] = 10;
        m.dutyPct[1] = -20;
        m.dutyPct[2] = 30;
        expectMessageRoundTrip(m, Type::SAMPLE,
                               {0x40, 0xE2, 0x01, 0x00, 0xFF, 0xFF, 0x02, 0x00, 0xFD, 0xFF, 0x0A, 0xEC, 0x1E});
    }
    {
        DBP::Fault m = {DBP::FAULT_HOST_TIMEOUT, -1234};  // 0xFB2E
        expectMessageRoundTrip(m, Type::FAULT, {0x04, 0x2E, 0xFB});
    }
}

TEST(DriverBoardProtocolCodec, EncodeRefusesOversizePayloadAndSmallBuffer) {
    uint8_t payload[DBP::MAX_PAYLOAD + 1] = {0};
    uint8_t out[DBP::MAX_FRAME];
    EXPECT_EQ(DBP::encode(0x01, 0, payload, DBP::MAX_PAYLOAD + 1, out, sizeof(out)), 0u);
    EXPECT_EQ(DBP::encode(0x01, 0, payload, DBP::MAX_PAYLOAD, out, DBP::MAX_FRAME - 1), 0u);
    EXPECT_EQ(DBP::encode(0x01, 0, payload, DBP::MAX_PAYLOAD, out, DBP::MAX_FRAME), DBP::MAX_FRAME);
    EXPECT_EQ(DBP::encode(0x01, 0, nullptr, 0, out, DBP::MIN_FRAME), DBP::MIN_FRAME);
    EXPECT_EQ(DBP::encode(0x01, 0, nullptr, 1, out, sizeof(out)), 0u);
}

// ----------------------------------------------------------------------
// DriverBoardProtocol-2: one frame per SYNC..CRC, ready on the last CRC byte
// ----------------------------------------------------------------------

TEST(DriverBoardProtocolCodec, FeedIsTrueExactlyOnceOnTheLastCrcByte) {
    RecordProperty("verifies", "DriverBoardProtocol-2");
    const std::vector<uint8_t> payload = {0x01, 0x02, 0x03, 0x04};
    const std::vector<uint8_t> f = specFrame(0x01, 7, payload);
    Parser p;
    const FeedResult r = feedAll(p, f);
    EXPECT_EQ(r.ready, 1u);
    EXPECT_EQ(r.lastReadyIndex, static_cast<int>(f.size() - 1));
    const Frame got = p.frame();
    EXPECT_EQ(got.type, 0x01);
    EXPECT_EQ(got.seq, 7);
    EXPECT_EQ(got.len, 4);
    EXPECT_EQ(std::memcmp(got.payload, payload.data(), payload.size()), 0);
    EXPECT_EQ(p.stats().accepted, 1u);
    EXPECT_EQ(p.stats().rejected, 0u);
}

TEST(DriverBoardProtocolCodec, BackToBackFramesAreEachDeliveredOnTheirLastByte) {
    RecordProperty("verifies", "DriverBoardProtocol-2");
    const std::vector<uint8_t> a = specFrame(static_cast<uint8_t>(Type::HK_REQUEST), 1, {});
    const std::vector<uint8_t> b = specFrame(static_cast<uint8_t>(Type::PONG), 2, {0x02, 0x01, 0x01, 0x00});
    std::vector<uint8_t> stream = a;
    stream.insert(stream.end(), b.begin(), b.end());
    Parser p;
    std::vector<int> readyAt;
    for (size_t i = 0; i < stream.size(); i++) {
        if (p.feed(stream[i])) {
            readyAt.push_back(static_cast<int>(i));
            if (readyAt.size() == 1) {
                EXPECT_TRUE(frameEquals(p.frame(), static_cast<uint8_t>(Type::HK_REQUEST), 1, {}));
            } else {
                EXPECT_TRUE(frameEquals(p.frame(), static_cast<uint8_t>(Type::PONG), 2, {0x02, 0x01, 0x01, 0x00}));
            }
        }
    }
    ASSERT_EQ(readyAt.size(), 2u);
    EXPECT_EQ(readyAt[0], static_cast<int>(a.size() - 1));
    EXPECT_EQ(readyAt[1], static_cast<int>(stream.size() - 1));
    EXPECT_EQ(p.stats().accepted, 2u);
    EXPECT_EQ(p.stats().rejected, 0u);
}

TEST(DriverBoardProtocolCodec, MaximumAndEmptyPayloadsParse) {
    RecordProperty("verifies", "DriverBoardProtocol-2");
    std::vector<uint8_t> big;
    for (uint8_t i = 0; i < DBP::MAX_PAYLOAD; i++) {
        big.push_back(static_cast<uint8_t>(0xC0 + i));
    }
    const std::vector<uint8_t> f = specFrame(0x7E, 200, big);
    ASSERT_EQ(f.size(), static_cast<size_t>(DBP::MAX_FRAME));
    Parser p;
    FeedResult r = feedAll(p, f);
    EXPECT_EQ(r.ready, 1u);
    EXPECT_TRUE(frameEquals(p.frame(), 0x7E, 200, big));
    const std::vector<uint8_t> e = specFrame(0x02, 201, {});
    r = feedAll(p, e);
    EXPECT_EQ(r.ready, 1u);
    EXPECT_TRUE(frameEquals(p.frame(), 0x02, 201, {}));
    EXPECT_EQ(p.stats().accepted, 2u);
}

// ----------------------------------------------------------------------
// DriverBoardProtocol-3: reject and resynchronise
// ----------------------------------------------------------------------

TEST(DriverBoardProtocolCodec, EverySingleByteCorruptionIsRejectedOnceAndTheNextFrameDelivered) {
    RecordProperty("verifies", "DriverBoardProtocol-3");
    const std::vector<uint8_t> valid = cleanFrame();
    ASSERT_FALSE(valid.empty());
    ASSERT_EQ(valid.size(), 7u + DBP::HK_LEN);
    uint32_t cases = 0;
    for (size_t pos = 0; pos < valid.size(); pos++) {
        for (int v = 0; v < 256; v++) {
            if (static_cast<uint8_t>(v) == valid[pos]) {
                continue;
            }
            std::vector<uint8_t> corrupt = valid;
            corrupt[pos] = static_cast<uint8_t>(v);
            Parser p;
            const FeedResult rc = feedAll(p, corrupt);
            ASSERT_EQ(rc.ready, 0u) << "pos " << pos << " value " << v;
            const FeedResult rv = feedAll(p, valid);
            ASSERT_EQ(rv.ready, 1u) << "pos " << pos << " value " << v;
            ASSERT_EQ(rv.lastReadyIndex, static_cast<int>(valid.size() - 1)) << "pos " << pos << " value " << v;
            ASSERT_TRUE(
                frameEquals(p.frame(), valid[2], valid[3], std::vector<uint8_t>(valid.begin() + 5, valid.end() - 2)))
                << "pos " << pos << " value " << v;
            ASSERT_EQ(p.stats().rejected, 1u) << "pos " << pos << " value " << v;
            ASSERT_EQ(p.stats().accepted, 1u) << "pos " << pos << " value " << v;
            ASSERT_NE(p.stats().lastReject, Reject::NONE) << "pos " << pos << " value " << v;
            cases++;
        }
    }
    EXPECT_EQ(cases, valid.size() * 255u);
}

TEST(DriverBoardProtocolCodec, EveryTruncationIsRejectedOnceAndTheNextFrameDelivered) {
    RecordProperty("verifies", "DriverBoardProtocol-3");
    const std::vector<uint8_t> valid = cleanFrame();
    ASSERT_FALSE(valid.empty());
    for (size_t keep = 1; keep < valid.size(); keep++) {
        const std::vector<uint8_t> truncated(valid.begin(), valid.begin() + static_cast<long>(keep));
        Parser p;
        const FeedResult rt = feedAll(p, truncated);
        ASSERT_EQ(rt.ready, 0u) << "keep " << keep;
        const FeedResult rv = feedAll(p, valid);
        ASSERT_EQ(rv.ready, 1u) << "keep " << keep;
        ASSERT_EQ(rv.lastReadyIndex, static_cast<int>(valid.size() - 1)) << "keep " << keep;
        ASSERT_TRUE(
            frameEquals(p.frame(), valid[2], valid[3], std::vector<uint8_t>(valid.begin() + 5, valid.end() - 2)))
            << "keep " << keep;
        ASSERT_EQ(p.stats().rejected, 1u) << "keep " << keep;
        ASSERT_EQ(p.stats().accepted, 1u) << "keep " << keep;
    }
}

TEST(DriverBoardProtocolCodec, CorruptCrcIsOneRejection) {
    RecordProperty("verifies", "DriverBoardProtocol-3");
    std::vector<uint8_t> f = specFrame(0x01, 3, {0x10, 0x20, 0x30, 0x40});
    f[f.size() - 1] ^= 0x01;
    Parser p;
    EXPECT_EQ(feedAll(p, f).ready, 0u);
    EXPECT_EQ(p.stats().rejected, 1u);
    EXPECT_EQ(p.stats().lastReject, Reject::CRC);
    EXPECT_EQ(p.stats().accepted, 0u);
    const std::vector<uint8_t> good = specFrame(0x01, 4, {0x10, 0x20, 0x30, 0x40});
    EXPECT_EQ(feedAll(p, good).ready, 1u);
    EXPECT_TRUE(frameEquals(p.frame(), 0x01, 4, {0x10, 0x20, 0x30, 0x40}));
    EXPECT_EQ(p.stats().rejected, 1u);
}

TEST(DriverBoardProtocolCodec, Len33IsRejectedAtTheLengthByte) {
    RecordProperty("verifies", "DriverBoardProtocol-3");
    // Hand-built frame with LEN = 33 and a CRC that would be right if 33 were legal.
    std::vector<uint8_t> f = {0x5C, 0xA1, 0x82, 0x01, 33};
    for (uint8_t i = 0; i < 33; i++) {
        f.push_back(static_cast<uint8_t>(0x30 + i));
    }
    const uint16_t crc = Crc16::ccitt(&f[2], 3 + 33);
    f.push_back(static_cast<uint8_t>(crc >> 8));
    f.push_back(static_cast<uint8_t>(crc & 0xFF));
    Parser p;
    uint32_t ready = 0;
    for (size_t i = 0; i < f.size(); i++) {
        ready += p.feed(f[i]) ? 1u : 0u;
        if (i == 4) {
            EXPECT_EQ(p.stats().rejected, 1u) << "LEN > 32 is rejected as soon as LEN is seen";
            EXPECT_EQ(p.stats().lastReject, Reject::LENGTH);
        }
    }
    EXPECT_EQ(ready, 0u);
    EXPECT_EQ(p.stats().rejected, 1u);
    const std::vector<uint8_t> good = specFrame(0x82, 2, {0x01});
    EXPECT_EQ(feedAll(p, good).ready, 1u);
    EXPECT_TRUE(frameEquals(p.frame(), 0x82, 2, {0x01}));
}

TEST(DriverBoardProtocolCodec, ArbitraryGarbageThenFrameIsDelivered) {
    RecordProperty("verifies", "DriverBoardProtocol-3");
    // Deterministic pseudo-random garbage that includes 0x5C, 0xA1 and 0x5C 0xA1 pairs.
    std::vector<uint8_t> garbage;
    uint32_t x = 0x12345678u;
    for (int i = 0; i < 400; i++) {
        x = x * 1664525u + 1013904223u;
        garbage.push_back(static_cast<uint8_t>(x >> 24));
    }
    garbage.push_back(0x5C);
    garbage.push_back(0xA1);
    garbage.push_back(0x82);
    garbage.push_back(0x00);
    garbage.push_back(0x02);
    garbage.push_back(0x5C);  // a false SYNC pair inside a false frame
    garbage.push_back(0xA1);
    garbage.push_back(0x00);
    garbage.push_back(0x00);
    Parser p;
    EXPECT_EQ(feedAll(p, garbage).ready, 0u);
    EXPECT_GE(p.stats().rejected, 1u);
    const uint32_t rejectedAfterGarbage = p.stats().rejected;
    const std::vector<uint8_t> good = specFrame(static_cast<uint8_t>(Type::PONG), 9, {0x02, 0x01, 0x01, 0x00});
    const FeedResult r = feedAll(p, good);
    EXPECT_EQ(r.ready, 1u);
    EXPECT_EQ(r.lastReadyIndex, static_cast<int>(good.size() - 1));
    EXPECT_TRUE(frameEquals(p.frame(), static_cast<uint8_t>(Type::PONG), 9, {0x02, 0x01, 0x01, 0x00}));
    EXPECT_EQ(p.stats().accepted, 1u);
    // Garbage swallowed into the good frame's candidate would have failed it; count is unchanged
    // by the good frame itself except for whatever the last stretch of garbage cost.
    EXPECT_LE(p.stats().rejected, rejectedAfterGarbage + 1u);
    // Once synchronised, clean frames keep flowing without further rejections.
    const uint32_t settled = p.stats().rejected;
    for (uint8_t s = 10; s < 20; s++) {
        const std::vector<uint8_t> g = specFrame(0x82, s, {0x01, 0x02});
        EXPECT_EQ(feedAll(p, g).ready, 1u);
    }
    EXPECT_EQ(p.stats().rejected, settled);
    EXPECT_EQ(p.stats().accepted, 11u);
}

TEST(DriverBoardProtocolCodec, FalseSyncInsidePayloadIsNotAFrameBoundary) {
    RecordProperty("verifies", "DriverBoardProtocol-3");
    const std::vector<uint8_t> payload = {0x11, 0x5C, 0xA1, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    const std::vector<uint8_t> f = specFrame(0x88, 33, payload);
    {
        Parser p;
        const FeedResult r = feedAll(p, f);
        EXPECT_EQ(r.ready, 1u);
        EXPECT_EQ(r.lastReadyIndex, static_cast<int>(f.size() - 1));
        EXPECT_TRUE(frameEquals(p.frame(), 0x88, 33, payload));
        EXPECT_EQ(p.stats().rejected, 0u);
    }
    {
        // Corrupt the CRC: the outer frame fails, the false SYNC inside it is
        // then tried and fails too (LEN or CRC); the spec says both count as one.
        std::vector<uint8_t> bad = f;
        bad[bad.size() - 2] ^= 0x80;
        Parser p;
        EXPECT_EQ(feedAll(p, bad).ready, 0u);
        EXPECT_EQ(p.stats().rejected, 1u);
        const std::vector<uint8_t> good = specFrame(0x88, 34, payload);
        EXPECT_EQ(feedAll(p, good).ready, 1u);
        EXPECT_TRUE(frameEquals(p.frame(), 0x88, 34, payload));
        EXPECT_EQ(p.stats().rejected, 1u);
        EXPECT_EQ(p.stats().accepted, 1u);
    }
}

TEST(DriverBoardProtocolCodec, LengthCorruptionThatSwallowsAShortFrameStillDeliversIt) {
    // A LEN corrupted upward on a short frame eats the whole next frame; the
    // parser must still find and deliver that frame from what it buffered.
    std::vector<uint8_t> c = specFrame(0x02, 1, {});                              // 7 bytes
    c[4] = 30;                                                                    // now claims 37 bytes
    const std::vector<uint8_t> v = specFrame(0x87, 2, {0x02, 0x01, 0x01, 0x00});  // 11 bytes
    const std::vector<uint8_t> w =
        specFrame(0x82, 3, {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
                            0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17});
    std::vector<uint8_t> stream = c;
    stream.insert(stream.end(), v.begin(), v.end());
    stream.insert(stream.end(), w.begin(), w.end());
    Parser p;
    std::vector<Frame> delivered;
    for (size_t i = 0; i < stream.size(); i++) {
        if (p.feed(stream[i])) {
            delivered.push_back(p.frame());
        }
    }
    ASSERT_EQ(delivered.size(), 2u);
    EXPECT_TRUE(frameEquals(delivered[0], 0x87, 2, {0x02, 0x01, 0x01, 0x00}));
    EXPECT_TRUE(frameEquals(delivered[1], 0x82, 3, std::vector<uint8_t>(w.begin() + 5, w.end() - 2)));
    EXPECT_EQ(p.stats().rejected, 1u);
    EXPECT_EQ(p.stats().accepted, 2u);
}

TEST(DriverBoardProtocolCodec, ResetDropsAPartialFrameAndSeqHistoryButKeepsCounters) {
    const std::vector<uint8_t> f = specFrame(0x01, 5, {0xAA, 0xBB});
    Parser p;
    EXPECT_EQ(feedAll(p, f).ready, 1u);
    feedAll(p, f.data(), 4);  // partial second frame
    p.reset();
    EXPECT_EQ(p.stats().accepted, 1u);
    const std::vector<uint8_t> g = specFrame(0x01, 9, {0xAA, 0xBB});  // would be a gap without reset
    const FeedResult r = feedAll(p, g);
    EXPECT_EQ(r.ready, 1u);
    EXPECT_EQ(r.lastReadyIndex, static_cast<int>(g.size() - 1));
    EXPECT_EQ(p.stats().seqGaps, 0u);
    EXPECT_EQ(p.stats().accepted, 2u);
    p.resetStats();
    EXPECT_EQ(p.stats().accepted, 0u);
    EXPECT_EQ(p.stats().rejected, 0u);
    EXPECT_EQ(p.stats().seqGaps, 0u);
    EXPECT_EQ(p.stats().lastReject, Reject::NONE);
}

// ----------------------------------------------------------------------
// DriverBoardProtocol-4: fixed memory
// ----------------------------------------------------------------------

TEST(DriverBoardProtocolCodec, ParserFitsIn64BytesAndNeverAllocates) {
    RecordProperty("verifies", "DriverBoardProtocol-4");
    static_assert(sizeof(Parser) <= 64, "Parser must fit in 64 bytes");
    EXPECT_LE(sizeof(Parser), 64u);
    EXPECT_EQ(static_cast<int>(DBP::MAX_FRAME), 39);
    EXPECT_EQ(static_cast<int>(DBP::MAX_PAYLOAD), 32);

    // Prepare every input outside the measured window.
    std::vector<uint8_t> big;
    for (uint8_t i = 0; i < DBP::MAX_PAYLOAD; i++) {
        big.push_back(i);
    }
    std::vector<uint8_t> stream;
    for (uint8_t s = 0; s < 40; s++) {
        const std::vector<uint8_t> f = specFrame(0x82, s, big);
        stream.insert(stream.end(), f.begin(), f.end());
        stream.push_back(0x5C);  // an orphan SYNC between frames forces a rejection path
        stream.push_back(0x00);
    }
    DBP::Hk hk = {};
    uint8_t out[DBP::MAX_FRAME];

    const size_t before = g_allocations;
    Parser p;
    uint32_t ready = 0;
    for (size_t i = 0; i < stream.size(); i++) {
        if (p.feed(stream[i])) {
            ready++;
            const Frame fr = p.frame();
            (void)fr;
        }
    }
    const size_t n = DBP::encodeMessage(hk, 1, out, sizeof(out));
    p.reset();
    p.resetStats();
    const size_t after = g_allocations;

    EXPECT_EQ(after, before) << "codec allocated on the heap";
    EXPECT_EQ(ready, 40u);
    EXPECT_EQ(n, 7u + DBP::HK_LEN);
}

// ----------------------------------------------------------------------
// DriverBoardProtocol-5: integer wire units, exact host conversion
// ----------------------------------------------------------------------

TEST(DriverBoardProtocolCodec, HkAndSampleCarryIntegerUnitsAtSpecOffsets) {
    RecordProperty("verifies", "DriverBoardProtocol-5");
    DBP::Hk hk = {};
    hk.currentMa[0] = 1500;
    hk.currentMa[1] = -200;
    hk.currentMa[2] = 0;
    hk.tempDeciC[0] = 251;
    hk.tempDeciC[1] = 300;
    hk.dutyPct[0] = 50;
    hk.dutyPct[1] = -50;
    hk.dutyPct[2] = 0;
    hk.state = 2;
    hk.faultFlags = 0;
    hk.uptimeMs = 65000;     // 0x0000FDE8
    hk.boardTickMs = 65001;  // 0x0000FDE9
    uint8_t pl[DBP::MAX_PAYLOAD];
    ASSERT_EQ(DBP::pack(hk, pl), DBP::HK_LEN);
    // mA as I16 LE
    EXPECT_EQ(pl[0], 0xDC);
    EXPECT_EQ(pl[1], 0x05);
    EXPECT_EQ(pl[2], 0x38);
    EXPECT_EQ(pl[3], 0xFF);
    // 0.1 degC as I16 LE
    EXPECT_EQ(pl[6], 0xFB);
    EXPECT_EQ(pl[7], 0x00);
    EXPECT_EQ(pl[8], 0x2C);
    EXPECT_EQ(pl[9], 0x01);
    // percent as I8
    EXPECT_EQ(pl[10], 50);
    EXPECT_EQ(pl[11], 0xCE);
    // ms as U32 LE
    EXPECT_EQ(pl[15], 0xE8);
    EXPECT_EQ(pl[16], 0xFD);
    EXPECT_EQ(pl[17], 0x00);
    EXPECT_EQ(pl[18], 0x00);
    EXPECT_EQ(pl[19], 0xE9);
    EXPECT_EQ(pl[20], 0xFD);

    DBP::Sample s = {};
    s.tMs = 60000;  // 0x0000EA60
    s.currentMa[0] = -32768;
    s.currentMa[1] = 32767;
    s.currentMa[2] = 1;
    s.dutyPct[0] = -100;
    s.dutyPct[1] = 100;
    s.dutyPct[2] = 0;
    ASSERT_EQ(DBP::pack(s, pl), DBP::SAMPLE_LEN);
    EXPECT_EQ(pl[0], 0x60);
    EXPECT_EQ(pl[1], 0xEA);
    EXPECT_EQ(pl[2], 0x00);
    EXPECT_EQ(pl[3], 0x00);
    EXPECT_EQ(pl[4], 0x00);
    EXPECT_EQ(pl[5], 0x80);
    EXPECT_EQ(pl[6], 0xFF);
    EXPECT_EQ(pl[7], 0x7F);
    EXPECT_EQ(pl[8], 0x01);
    EXPECT_EQ(pl[9], 0x00);
    EXPECT_EQ(pl[10], 0x9C);
    EXPECT_EQ(pl[11], 100);
    EXPECT_EQ(pl[12], 0);

    // Host conversions for the DriverBoardHandler-7 values.
    EXPECT_EQ(DBP::currentAmps(1500), 1.5f);
    EXPECT_EQ(DBP::currentAmps(-200), -0.2f);
    EXPECT_EQ(DBP::temperatureC(251), 25.1f);
    EXPECT_EQ(DBP::temperatureC(300), 30.0f);
    EXPECT_EQ(DBP::dutyPercent(-50), -50);
    EXPECT_EQ(DBP::dutyPercent(100), 100);
}

TEST(DriverBoardProtocolCodec, HostConversionIsExactOverTheWholeWireRange) {
    RecordProperty("verifies", "DriverBoardProtocol-5");
    // "Exact": every wire integer maps to the F32 nearest the true value and
    // maps back to the same integer, over the whole I16 range (±32 A, ±3276 °C).
    for (int32_t v = -32768; v <= 32767; v++) {
        const int16_t w = static_cast<int16_t>(v);
        const float a = DBP::currentAmps(w);
        ASSERT_EQ(a, static_cast<float>(v) / 1000.0f) << v;
        ASSERT_EQ(static_cast<int32_t>(std::lroundf(a * 1000.0f)), v) << v;
        const float t = DBP::temperatureC(w);
        ASSERT_EQ(t, static_cast<float>(v) / 10.0f) << v;
        ASSERT_EQ(static_cast<int32_t>(std::lroundf(t * 10.0f)), v) << v;
    }
    for (int32_t d = -128; d <= 127; d++) {
        ASSERT_EQ(DBP::dutyPercent(static_cast<int8_t>(d)), static_cast<int16_t>(d));
    }
}

// ----------------------------------------------------------------------
// DriverBoardProtocol-6: sequence continuity
// ----------------------------------------------------------------------

TEST(DriverBoardProtocolCodec, SeqGapIsCountedAndContinuityIsNot) {
    RecordProperty("verifies", "DriverBoardProtocol-6");
    Parser p;
    EXPECT_EQ(feedAll(p, specFrame(0x82, 5, {0x01})).ready, 1u);
    EXPECT_EQ(feedAll(p, specFrame(0x82, 7, {0x01})).ready, 1u);
    EXPECT_EQ(p.stats().accepted, 2u);
    EXPECT_EQ(p.stats().seqGaps, 1u);
    EXPECT_EQ(feedAll(p, specFrame(0x82, 8, {0x01})).ready, 1u);
    EXPECT_EQ(p.stats().accepted, 3u);
    EXPECT_EQ(p.stats().seqGaps, 1u);
    EXPECT_EQ(p.stats().rejected, 0u);
}

TEST(DriverBoardProtocolCodec, SeqWrapsWithoutAGapAndRejectedFramesDoNotAdvanceIt) {
    RecordProperty("verifies", "DriverBoardProtocol-6");
    Parser p;
    EXPECT_EQ(feedAll(p, specFrame(0x82, 255, {})).ready, 1u);
    EXPECT_EQ(feedAll(p, specFrame(0x82, 0, {})).ready, 1u);
    EXPECT_EQ(p.stats().seqGaps, 0u);
    std::vector<uint8_t> bad = specFrame(0x82, 1, {});
    bad[bad.size() - 1] ^= 0xFF;
    EXPECT_EQ(feedAll(p, bad).ready, 0u);
    EXPECT_EQ(feedAll(p, specFrame(0x82, 1, {})).ready, 1u);
    EXPECT_EQ(p.stats().seqGaps, 0u) << "a rejected frame's SEQ must not count";
    EXPECT_EQ(p.stats().rejected, 1u);
    EXPECT_EQ(p.stats().accepted, 3u);
    // First frame after construction never counts as a gap, whatever its SEQ.
    Parser q;
    EXPECT_EQ(feedAll(q, specFrame(0x82, 77, {})).ready, 1u);
    EXPECT_EQ(q.stats().seqGaps, 0u);
}

// ----------------------------------------------------------------------
// Message layer edge cases (no requirement claimed)
// ----------------------------------------------------------------------

TEST(DriverBoardProtocolCodec, UnpackRejectsWrongTypeOrWrongLength) {
    Parser p;
    EXPECT_EQ(feedAll(p, specFrame(static_cast<uint8_t>(Type::ACK), 1, {0x03, 0x00})).ready, 1u);
    const Frame ack = p.frame();
    DBP::Ack a = {};
    DBP::Pong pong = {};
    EXPECT_TRUE(DBP::unpack(ack, a));
    EXPECT_EQ(a.ackedType, 0x03);
    EXPECT_EQ(a.status, 0);
    EXPECT_FALSE(DBP::unpack(ack, pong));
    EXPECT_EQ(feedAll(p, specFrame(static_cast<uint8_t>(Type::ACK), 2, {0x03})).ready, 1u);
    EXPECT_FALSE(DBP::unpack(p.frame(), a)) << "ACK with one byte is the wrong length";
    EXPECT_EQ(feedAll(p, specFrame(static_cast<uint8_t>(Type::HK_REQUEST), 3, {0x00})).ready, 1u);
    DBP::HkRequest hr = {};
    EXPECT_FALSE(DBP::unpack(p.frame(), hr)) << "payload-less message with a payload";
}

TEST(DriverBoardProtocolCodec, KnownTypesAndDirectionBit) {
    const uint8_t known[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x81, 0x82, 0x87, 0x88, 0x8F};
    for (uint8_t t : known) {
        EXPECT_TRUE(DBP::isKnownType(t)) << static_cast<int>(t);
    }
    EXPECT_FALSE(DBP::isKnownType(0x00));
    EXPECT_FALSE(DBP::isKnownType(0x0B));
    EXPECT_FALSE(DBP::isKnownType(0x80));
    EXPECT_FALSE(DBP::isKnownType(0x83));
    EXPECT_FALSE(DBP::isKnownType(0xFF));
    EXPECT_EQ(static_cast<uint8_t>(Type::ACK) & DBP::BOARD_TO_HOST_BIT, DBP::BOARD_TO_HOST_BIT);
    EXPECT_EQ(static_cast<uint8_t>(Type::HEARTBEAT) & DBP::BOARD_TO_HOST_BIT, 0);
    EXPECT_EQ(DBP::STREAM_RATES_HZ[0], 5);
    EXPECT_EQ(DBP::STREAM_RATES_HZ[1], 10);
    EXPECT_EQ(DBP::STREAM_RATES_HZ[2], 20);
    EXPECT_EQ(DBP::STREAM_RATES_HZ[3], 50);
}

}  // namespace
