// ======================================================================
// \title  test_DataRecorder_Codec.cpp
// \brief  Host tests for the F'-free DataRecorder pieces: SegmentCodec,
//         PacketRing and flushDue (Cycle M row A8-1).
//
// Written from docs-site/dev-loop/cycles/cycle-m-plan/01-normative.md
// sections 5.2-5.4 (segment and record bytes, golden vector), 7.3 (flush
// trigger) and 8 (API names), and the pass criteria of DataRecorder-1, -2,
// -4 and -6. Everything here is byte-level or count-level: no Os, no F'.
// ======================================================================

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "PROVESFlightControllerReference/Components/DataRecorder/PacketRing.hpp"
#include "PROVESFlightControllerReference/Components/DataRecorder/SegmentCodec.hpp"
#include "PROVESFlightControllerReference/Components/PersistedRecord/PersistedRecordCodec.hpp"

namespace {

namespace SC = Components::SegmentCodec;
using Bytes = std::vector<uint8_t>;

// ---- golden segment, 01-normative.md section 5.4 (57 bytes) ----

//! Header: stream TLM, boot count 0xFFFF, open time 1000 s 5 us; CRC 0x6F63D8E5.
const uint8_t GOLDEN_HEADER[20] = {0x53, 0x45, 0x47, 0x31, 0x01, 0x00, 0xFF, 0xFF, 0xE8, 0x03,
                                   0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0xE5, 0xD8, 0x63, 0x6F};

//! The 31-byte FileSystem packet carried by the golden record (big-endian F' bytes).
const uint8_t GOLDEN_PAYLOAD[31] = {0x00, 0x04, 0x00, 0x05, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0xE8,
                                    0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0xEE, 0x6B, 0x28,
                                    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};

//! The whole golden record: len 0x001F LE, the payload, CRC 0xA6219237 LE.
const uint8_t GOLDEN_RECORD[37] = {0x1F, 0x00, 0x00, 0x04, 0x00, 0x05, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0xE8,
                                   0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0xEE, 0x6B, 0x28, 0x00, 0x00,
                                   0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x37, 0x92, 0x21, 0xA6};

Bytes goldenSegment() {
    Bytes out(GOLDEN_HEADER, GOLDEN_HEADER + sizeof(GOLDEN_HEADER));
    out.insert(out.end(), GOLDEN_RECORD, GOLDEN_RECORD + sizeof(GOLDEN_RECORD));
    return out;
}

SC::Header goldenHeader() {
    SC::Header h;
    h.stream = 0;  // RecorderStream::TLM
    h.bootCount = 0xFFFF;
    h.openSeconds = 1000;
    h.openUseconds = 5;
    return h;
}

//! Deterministic payload of the given length (pattern chosen per seed).
Bytes pattern(uint16_t len, uint8_t seed) {
    Bytes out(len);
    for (uint16_t i = 0; i < len; i++) {
        out[i] = static_cast<uint8_t>((i * (2u * seed + 1u) + seed * 11u + 1u) & 0xFFu);
    }
    return out;
}

//! Encode a header plus records with the codec under test.
Bytes encodeSegment(const SC::Header& h, const std::vector<Bytes>& payloads) {
    Bytes out(SC::HEADER_SIZE);
    EXPECT_EQ(SC::encodeHeader(h, out.data(), static_cast<uint32_t>(out.size())), SC::HEADER_SIZE);
    for (const Bytes& p : payloads) {
        Bytes rec(p.size() + SC::RECORD_OVERHEAD);
        const uint32_t n =
            SC::encodeRecord(p.data(), static_cast<uint16_t>(p.size()), rec.data(), static_cast<uint32_t>(rec.size()));
        EXPECT_EQ(n, p.size() + SC::RECORD_OVERHEAD);
        out.insert(out.end(), rec.begin(), rec.begin() + n);
    }
    return out;
}

void putLe16(Bytes& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
}

void putLe32(Bytes& out, uint32_t v) {
    for (int i = 0; i < 4; i++) {
        out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFFu));
    }
}

//! Result of decoding every record after the header.
struct DecodeRun {
    SC::Status stop;
    std::vector<Bytes> records;
};

//! Decode records from buf[HEADER_SIZE..len) until a non-OK status.
DecodeRun decodeAll(const uint8_t* buf, uint32_t len) {
    DecodeRun run;
    uint32_t off = SC::HEADER_SIZE;
    for (uint32_t guard = 0; guard < 10000; guard++) {
        const uint8_t* payload = nullptr;
        uint16_t payloadLen = 0;
        uint32_t consumed = 0;
        const SC::Status s = SC::decodeRecord(buf + off, len - off, payload, payloadLen, consumed);
        if (s != SC::Status::OK) {
            run.stop = s;
            return run;
        }
        run.records.push_back(Bytes(payload, payload + payloadLen));
        if (consumed == 0 || off + consumed > len) {
            // A decoder that reports OK must consume a whole record in range.
            ADD_FAILURE() << "decodeRecord returned OK with consumed=" << consumed << " at offset " << off;
            run.stop = SC::Status::OK;
            return run;
        }
        off += consumed;
    }
    ADD_FAILURE() << "decode loop did not terminate";
    run.stop = SC::Status::OK;
    return run;
}

bool isRecordFailure(SC::Status s) {
    return s == SC::Status::TRUNCATED || s == SC::Status::BAD_LENGTH || s == SC::Status::BAD_CRC;
}

//! Start offset of each record and the end of the last one.
std::vector<uint32_t> recordBounds(const std::vector<Bytes>& payloads) {
    std::vector<uint32_t> bounds;
    bounds.push_back(SC::HEADER_SIZE);
    for (const Bytes& p : payloads) {
        bounds.push_back(bounds.back() + static_cast<uint32_t>(p.size()) + SC::RECORD_OVERHEAD);
    }
    return bounds;
}

//! Index of the record whose bytes contain offset pos.
size_t recordIndexAt(const std::vector<uint32_t>& bounds, uint32_t pos) {
    size_t k = 0;
    while (k + 1 < bounds.size() && bounds[k + 1] <= pos) {
        k++;
    }
    return k;
}

std::vector<Bytes> threeRecordPayloads() {
    // Shortest, a middle size, and the longest legal payload.
    return {pattern(1, 1), pattern(100, 2), pattern(SC::MAX_PAYLOAD, 3)};
}

std::vector<Bytes> goldenPayloads() {
    return {Bytes(GOLDEN_PAYLOAD, GOLDEN_PAYLOAD + sizeof(GOLDEN_PAYLOAD))};
}

// ======================================================================
// DataRecorder-1: framing and the golden segment
// ======================================================================

TEST(DataRecorderCodec, ConstantsMatchTheFormat) {
    RecordProperty("verifies", "DataRecorder-1");
    EXPECT_EQ(SC::HEADER_SIZE, 20u);
    EXPECT_EQ(SC::RECORD_OVERHEAD, 6u);
    EXPECT_EQ(SC::MAX_PAYLOAD, 227u);  // FW_COM_BUFFER_MAX_SIZE at this tree
    EXPECT_EQ(SC::VERSION, 1u);
}

TEST(DataRecorderCodec, EncodeHeaderWritesTheGoldenHeader) {
    RecordProperty("verifies", "DataRecorder-1");
    uint8_t out[32];
    std::memset(out, 0xAA, sizeof(out));
    ASSERT_EQ(SC::encodeHeader(goldenHeader(), out, sizeof(out)), 20u);
    EXPECT_EQ(Bytes(out, out + 20), Bytes(GOLDEN_HEADER, GOLDEN_HEADER + 20));
    // Nothing beyond the 20 header bytes is touched.
    for (size_t i = 20; i < sizeof(out); i++) {
        EXPECT_EQ(out[i], 0xAA) << "byte " << i;
    }
}

TEST(DataRecorderCodec, EncodeHeaderFieldsAreLittleEndianWithCrcOverFirst16) {
    RecordProperty("verifies", "DataRecorder-1");
    SC::Header h;
    h.stream = 1;  // RecorderStream::EVT
    h.bootCount = 0x1234;
    h.openSeconds = 0x89ABCDEFu;
    h.openUseconds = 999999u;
    uint8_t out[20];
    ASSERT_EQ(SC::encodeHeader(h, out, sizeof(out)), 20u);
    Bytes expected = {0x53, 0x45, 0x47, 0x31, 0x01, 0x01};
    putLe16(expected, 0x1234);
    putLe32(expected, 0x89ABCDEFu);
    putLe32(expected, 999999u);
    putLe32(expected, Components::PersistedRecord::crc32(expected.data(), 16));
    EXPECT_EQ(Bytes(out, out + 20), expected);
}

TEST(DataRecorderCodec, EncodeHeaderNeedsTwentyBytesOfRoom) {
    RecordProperty("verifies", "DataRecorder-1");
    uint8_t out[20];
    for (uint32_t cap = 0; cap < 20; cap++) {
        EXPECT_EQ(SC::encodeHeader(goldenHeader(), out, cap), 0u) << "outCap " << cap;
    }
}

TEST(DataRecorderCodec, EncodeRecordWritesTheGoldenRecord) {
    RecordProperty("verifies", "DataRecorder-1");
    uint8_t out[64];
    ASSERT_EQ(SC::encodeRecord(GOLDEN_PAYLOAD, sizeof(GOLDEN_PAYLOAD), out, sizeof(out)), 37u);
    EXPECT_EQ(Bytes(out, out + 37), Bytes(GOLDEN_RECORD, GOLDEN_RECORD + 37));
}

TEST(DataRecorderCodec, GoldenSegmentReproducedByteForByte) {
    RecordProperty("verifies", "DataRecorder-1");
    const Bytes seg = encodeSegment(goldenHeader(), goldenPayloads());
    ASSERT_EQ(seg.size(), 57u);
    EXPECT_EQ(seg, goldenSegment());
}

TEST(DataRecorderCodec, MaxPayloadEncodesTo233BytesWithLeLengthAndCrc) {
    RecordProperty("verifies", "DataRecorder-1");
    const Bytes p = pattern(227, 9);
    uint8_t out[240];
    ASSERT_EQ(SC::encodeRecord(p.data(), 227, out, sizeof(out)), 233u);
    Bytes expected;
    putLe16(expected, 227);
    expected.insert(expected.end(), p.begin(), p.end());
    putLe32(expected, Components::PersistedRecord::crc32(expected.data(), static_cast<uint32_t>(expected.size())));
    EXPECT_EQ(Bytes(out, out + 233), expected);
}

TEST(DataRecorderCodec, ZeroAndOversizePayloadsEncodeToZeroBytes) {
    RecordProperty("verifies", "DataRecorder-1");
    const Bytes p = pattern(228, 4);
    uint8_t out[300];
    EXPECT_EQ(SC::encodeRecord(p.data(), 0, out, sizeof(out)), 0u);
    EXPECT_EQ(SC::encodeRecord(p.data(), 228, out, sizeof(out)), 0u);
}

TEST(DataRecorderCodec, EncodeRecordWithoutRoomWritesZeroBytes) {
    RecordProperty("verifies", "DataRecorder-1");
    const Bytes p = pattern(31, 5);
    uint8_t out[37];
    for (uint32_t cap = 0; cap < 37; cap++) {
        EXPECT_EQ(SC::encodeRecord(p.data(), 31, out, cap), 0u) << "outCap " << cap;
    }
    EXPECT_EQ(SC::encodeRecord(p.data(), 31, out, 37), 37u);
}

TEST(DataRecorderCodec, GoldenSegmentDecodesToItsHeaderAndRecord) {
    RecordProperty("verifies", "DataRecorder-1");
    const Bytes seg = goldenSegment();
    SC::Header h;
    ASSERT_EQ(SC::decodeHeader(seg.data(), static_cast<uint32_t>(seg.size()), h), SC::Status::OK);
    EXPECT_EQ(h.stream, 0u);
    EXPECT_EQ(h.bootCount, 0xFFFFu);
    EXPECT_EQ(h.openSeconds, 1000u);
    EXPECT_EQ(h.openUseconds, 5u);
    const DecodeRun run = decodeAll(seg.data(), static_cast<uint32_t>(seg.size()));
    EXPECT_EQ(run.stop, SC::Status::END);
    ASSERT_EQ(run.records.size(), 1u);
    EXPECT_EQ(run.records[0], goldenPayloads()[0]);
}

// ======================================================================
// DataRecorder-2: every single-byte corruption and every truncation
// ======================================================================

void checkEveryRecordFlip(const Bytes& seg, const std::vector<Bytes>& payloads) {
    const std::vector<uint32_t> bounds = recordBounds(payloads);
    ASSERT_EQ(bounds.back(), seg.size());
    for (uint32_t pos = SC::HEADER_SIZE; pos < seg.size(); pos++) {
        const size_t damaged = recordIndexAt(bounds, pos);
        for (unsigned mask = 1; mask < 256; mask++) {
            Bytes bad = seg;
            bad[pos] = static_cast<uint8_t>(bad[pos] ^ mask);
            const DecodeRun run = decodeAll(bad.data(), static_cast<uint32_t>(bad.size()));
            ASSERT_TRUE(isRecordFailure(run.stop)) << "byte " << pos << " xor 0x" << std::hex << mask
                                                   << ": stop status " << std::dec << static_cast<int>(run.stop);
            ASSERT_LE(run.records.size(), damaged) << "byte " << pos << " xor " << mask;
            for (size_t i = 0; i < run.records.size(); i++) {
                ASSERT_EQ(run.records[i], payloads[i]) << "byte " << pos << " xor " << mask << " record " << i;
            }
        }
    }
}

void checkEveryTruncation(const Bytes& seg, const std::vector<Bytes>& payloads) {
    const std::vector<uint32_t> bounds = recordBounds(payloads);
    ASSERT_EQ(bounds.back(), seg.size());
    for (uint32_t len = SC::HEADER_SIZE; len < seg.size(); len++) {
        const DecodeRun run = decodeAll(seg.data(), len);
        const size_t damaged = recordIndexAt(bounds, len);
        const bool atBoundary = (bounds[damaged] == len);
        if (atBoundary) {
            // A cut exactly between records damages none: the reader ends cleanly
            // after the whole records before the cut.
            EXPECT_EQ(run.stop, SC::Status::END) << "length " << len;
            EXPECT_EQ(run.records.size(), damaged) << "length " << len;
        } else {
            EXPECT_TRUE(isRecordFailure(run.stop)) << "length " << len;
            EXPECT_LE(run.records.size(), damaged) << "length " << len;
        }
        for (size_t i = 0; i < run.records.size() && i < payloads.size(); i++) {
            EXPECT_EQ(run.records[i], payloads[i]) << "length " << len << " record " << i;
        }
    }
}

TEST(DataRecorderCodec, GoldenSegmentEveryRecordByteFlipIsDetected) {
    RecordProperty("verifies", "DataRecorder-2");
    checkEveryRecordFlip(goldenSegment(), goldenPayloads());
}

TEST(DataRecorderCodec, ThreeRecordSegmentEveryRecordByteFlipIsDetected) {
    RecordProperty("verifies", "DataRecorder-2");
    const std::vector<Bytes> payloads = threeRecordPayloads();
    checkEveryRecordFlip(encodeSegment(goldenHeader(), payloads), payloads);
}

TEST(DataRecorderCodec, GoldenSegmentEveryTruncationStopsCleanly) {
    RecordProperty("verifies", "DataRecorder-2");
    checkEveryTruncation(goldenSegment(), goldenPayloads());
}

TEST(DataRecorderCodec, ThreeRecordSegmentEveryTruncationStopsCleanly) {
    RecordProperty("verifies", "DataRecorder-2");
    const std::vector<Bytes> payloads = threeRecordPayloads();
    checkEveryTruncation(encodeSegment(goldenHeader(), payloads), payloads);
}

TEST(DataRecorderCodec, EveryHeaderByteFlipFailsDecodeHeader) {
    RecordProperty("verifies", "DataRecorder-2");
    const Bytes seg = goldenSegment();
    for (uint32_t pos = 0; pos < SC::HEADER_SIZE; pos++) {
        for (unsigned mask = 1; mask < 256; mask++) {
            Bytes bad = seg;
            bad[pos] = static_cast<uint8_t>(bad[pos] ^ mask);
            SC::Header h;
            ASSERT_NE(SC::decodeHeader(bad.data(), static_cast<uint32_t>(bad.size()), h), SC::Status::OK)
                << "header byte " << pos << " xor " << mask;
        }
    }
}

TEST(DataRecorderCodec, EveryHeaderTruncationIsTruncated) {
    RecordProperty("verifies", "DataRecorder-2");
    const Bytes seg = goldenSegment();
    for (uint32_t len = 0; len < SC::HEADER_SIZE; len++) {
        SC::Header h;
        EXPECT_EQ(SC::decodeHeader(seg.data(), len, h), SC::Status::TRUNCATED) << "length " << len;
    }
}

TEST(DataRecorderCodec, DecodeRecordStatusOrder) {
    RecordProperty("verifies", "DataRecorder-2");
    const uint8_t* payload = nullptr;
    uint16_t payloadLen = 0;
    uint32_t consumed = 0;
    const uint8_t one[1] = {0x05};
    EXPECT_EQ(SC::decodeRecord(one, 0, payload, payloadLen, consumed), SC::Status::END);
    EXPECT_EQ(SC::decodeRecord(one, 1, payload, payloadLen, consumed), SC::Status::TRUNCATED);
    const uint8_t zeroLen[8] = {0x00, 0x00, 0, 0, 0, 0, 0, 0};
    EXPECT_EQ(SC::decodeRecord(zeroLen, 8, payload, payloadLen, consumed), SC::Status::BAD_LENGTH);
    Bytes over;
    putLe16(over, 228);
    over.resize(300, 0);
    EXPECT_EQ(SC::decodeRecord(over.data(), static_cast<uint32_t>(over.size()), payload, payloadLen, consumed),
              SC::Status::BAD_LENGTH);
    // Declared 31 with only 36 of the 37 bytes present.
    EXPECT_EQ(SC::decodeRecord(GOLDEN_RECORD, 36, payload, payloadLen, consumed), SC::Status::TRUNCATED);
    ASSERT_EQ(SC::decodeRecord(GOLDEN_RECORD, 37, payload, payloadLen, consumed), SC::Status::OK);
    EXPECT_EQ(payloadLen, 31u);
    EXPECT_EQ(consumed, 37u);
}

TEST(DataRecorderCodec, DecodeHeaderReportsMagicAndVersion) {
    RecordProperty("verifies", "DataRecorder-2");
    SC::Header h;
    Bytes badMagic(GOLDEN_HEADER, GOLDEN_HEADER + 20);
    badMagic[3] = '2';
    EXPECT_EQ(SC::decodeHeader(badMagic.data(), 20, h), SC::Status::BAD_MAGIC);

    // Version 2 with a CRC that is valid for those bytes: the version check fails.
    Bytes v2(GOLDEN_HEADER, GOLDEN_HEADER + 16);
    v2[4] = 2;
    putLe32(v2, Components::PersistedRecord::crc32(v2.data(), 16));
    EXPECT_EQ(SC::decodeHeader(v2.data(), 20, h), SC::Status::BAD_VERSION);

    Bytes badCrc(GOLDEN_HEADER, GOLDEN_HEADER + 20);
    badCrc[8] ^= 0x01;
    EXPECT_EQ(SC::decodeHeader(badCrc.data(), 20, h), SC::Status::BAD_CRC);
}

// ======================================================================
// DataRecorder-4 (trigger function): flushDue
// ======================================================================

TEST(DataRecorderFlushDue, FalseForEmptyTrueExactlyAtEitherThreshold) {
    RecordProperty("verifies", "DataRecorder-4");
    unsigned mismatches = 0;
    for (uint16_t flushRecords = 1; flushRecords <= 33; flushRecords++) {
        for (uint16_t interval = 1; interval <= 70; interval += 3) {
            for (uint32_t ticks = 0; ticks <= 75; ticks++) {
                for (uint16_t count = 0; count <= 33; count++) {
                    const bool expected = (count > 0) && ((count >= flushRecords) || (ticks >= interval));
                    if (Components::flushDue(count, flushRecords, ticks, interval) != expected && mismatches++ < 10) {
                        ADD_FAILURE() << "flushDue(" << count << ", " << flushRecords << ", " << ticks << ", "
                                      << interval << ") != " << expected;
                    }
                }
            }
        }
    }
    EXPECT_EQ(mismatches, 0u);
    // Interval limit of 01-normative section 3 and the TLM/EVT defaults.
    EXPECT_FALSE(Components::flushDue(1, 16, 3599, 3600));
    EXPECT_TRUE(Components::flushDue(1, 16, 3600, 3600));
    EXPECT_FALSE(Components::flushDue(15, 16, 59, 60));
    EXPECT_TRUE(Components::flushDue(16, 16, 0, 60));
    EXPECT_FALSE(Components::flushDue(7, 8, 9, 10));
    EXPECT_TRUE(Components::flushDue(1, 8, 10, 10));
}

// ======================================================================
// DataRecorder-6 (ring): full ring drops the oldest and counts it
// ======================================================================

using Ring32 = Components::PacketRing<32, 227>;

Bytes slotPayload(uint32_t n) {
    // Distinct content and length per push number.
    return pattern(static_cast<uint16_t>(1 + (n % 227)), static_cast<uint8_t>(n & 0xFF));
}

Bytes peekBytes(Ring32& ring, uint16_t i) {
    const uint8_t* data = nullptr;
    uint16_t len = 0;
    EXPECT_TRUE(ring.peek(i, data, len)) << "peek " << i;
    if (data == nullptr) {
        return Bytes();
    }
    return Bytes(data, data + len);
}

TEST(DataRecorderRing, FullRingDropsOldestForEveryCapacity) {
    RecordProperty("verifies", "DataRecorder-6");
    for (uint16_t n = 1; n <= 32; n++) {
        Ring32 ring;
        ASSERT_TRUE(ring.setCapacity(n)) << "capacity " << n;
        ASSERT_EQ(ring.capacity(), n);
        for (uint32_t i = 0; i < n; i++) {
            const Bytes p = slotPayload(i);
            ASSERT_TRUE(ring.push(p.data(), static_cast<uint16_t>(p.size())));
        }
        ASSERT_EQ(ring.count(), n);
        ASSERT_EQ(ring.dropped(), 0u);
        const Bytes formerSecond = (n >= 2) ? slotPayload(1) : slotPayload(n);

        const Bytes extra = slotPayload(n);
        ASSERT_TRUE(ring.push(extra.data(), static_cast<uint16_t>(extra.size())));
        EXPECT_EQ(ring.count(), n) << "capacity " << n;
        EXPECT_EQ(ring.dropped(), 1u) << "capacity " << n;
        EXPECT_EQ(peekBytes(ring, 0), formerSecond) << "capacity " << n;
        EXPECT_EQ(peekBytes(ring, static_cast<uint16_t>(n - 1)), extra) << "capacity " << n;
        EXPECT_EQ(ring.pushed(), n + 1u);
    }
}

TEST(DataRecorderRing, EachOverflowPushDropsExactlyOne) {
    RecordProperty("verifies", "DataRecorder-6");
    Ring32 ring;
    ASSERT_TRUE(ring.setCapacity(4));
    for (uint32_t i = 0; i < 100; i++) {
        const Bytes p = slotPayload(i);
        ASSERT_TRUE(ring.push(p.data(), static_cast<uint16_t>(p.size())));
        EXPECT_EQ(ring.dropped(), (i < 4) ? 0u : (i - 3u)) << "push " << i;
        EXPECT_EQ(ring.count(), (i < 4) ? static_cast<uint16_t>(i + 1) : static_cast<uint16_t>(4));
    }
    // Survivors are the four newest, oldest first.
    for (uint16_t k = 0; k < 4; k++) {
        EXPECT_EQ(peekBytes(ring, k), slotPayload(96u + k));
    }
}

TEST(DataRecorderRing, InvalidPushesAreRejectedAndNotCounted) {
    RecordProperty("verifies", "DataRecorder-6");
    Ring32 ring;
    ASSERT_TRUE(ring.setCapacity(2));
    const Bytes big = pattern(228, 1);
    EXPECT_FALSE(ring.push(nullptr, 5));
    EXPECT_FALSE(ring.push(big.data(), 0));
    EXPECT_FALSE(ring.push(big.data(), 228));
    EXPECT_EQ(ring.count(), 0u);
    EXPECT_EQ(ring.pushed(), 0u);
    EXPECT_EQ(ring.dropped(), 0u);
    EXPECT_TRUE(ring.push(big.data(), 227));
    EXPECT_EQ(ring.count(), 1u);
    EXPECT_EQ(peekBytes(ring, 0), Bytes(big.begin(), big.begin() + 227));
}

TEST(DataRecorderRing, SetCapacityBelowCountDropsSurplusOldest) {
    RecordProperty("verifies", "DataRecorder-6");
    for (uint16_t shrinkTo = 1; shrinkTo <= 10; shrinkTo++) {
        Ring32 ring;
        ASSERT_TRUE(ring.setCapacity(10));
        for (uint32_t i = 0; i < 10; i++) {
            const Bytes p = slotPayload(i);
            ASSERT_TRUE(ring.push(p.data(), static_cast<uint16_t>(p.size())));
        }
        ASSERT_TRUE(ring.setCapacity(shrinkTo));
        EXPECT_EQ(ring.capacity(), shrinkTo);
        EXPECT_EQ(ring.count(), shrinkTo);
        EXPECT_EQ(ring.dropped(), static_cast<uint32_t>(10 - shrinkTo));
        for (uint16_t k = 0; k < shrinkTo; k++) {
            EXPECT_EQ(peekBytes(ring, k), slotPayload(10u - shrinkTo + k)) << "shrink " << shrinkTo << " k " << k;
        }
    }
}

TEST(DataRecorderRing, SetCapacityRangeIsOneToSlots) {
    RecordProperty("verifies", "DataRecorder-6");
    Ring32 ring;
    ASSERT_TRUE(ring.setCapacity(5));
    EXPECT_FALSE(ring.setCapacity(0));
    EXPECT_FALSE(ring.setCapacity(33));
    EXPECT_EQ(ring.capacity(), 5u);
    EXPECT_TRUE(ring.setCapacity(32));
    EXPECT_EQ(ring.capacity(), 32u);
}

TEST(DataRecorderRing, PopRemovesOldestAndPeekIsBounded) {
    RecordProperty("verifies", "DataRecorder-6");
    Ring32 ring;
    ASSERT_TRUE(ring.setCapacity(8));
    for (uint32_t i = 0; i < 6; i++) {
        const Bytes p = slotPayload(i);
        ASSERT_TRUE(ring.push(p.data(), static_cast<uint16_t>(p.size())));
    }
    ring.pop(2);
    EXPECT_EQ(ring.count(), 4u);
    EXPECT_EQ(peekBytes(ring, 0), slotPayload(2));
    EXPECT_EQ(peekBytes(ring, 3), slotPayload(5));
    const uint8_t* data = nullptr;
    uint16_t len = 0;
    EXPECT_FALSE(ring.peek(4, data, len));
    EXPECT_EQ(ring.dropped(), 0u);  // pop is not a drop
}

}  // namespace
