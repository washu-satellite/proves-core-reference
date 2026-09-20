// ======================================================================
// \title  test_PersistedRecord.cpp
// \brief  Host tests for the shared PersistedRecord codec and file layer.
//
// The codec half is pure logic. The file half compiles the real
// PersistedRecordFile.cpp against the stubs in support/ (an in-memory
// Os::File / Os::FileSystem with fault injection at open, write, flush and
// rename) so the atomic-replace protocol is exercised end to end.
// ======================================================================

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "Os/File.hpp"
#include "PROVESFlightControllerReference/Components/PersistedRecord/PersistedRecordCodec.hpp"
#include "PROVESFlightControllerReference/Components/PersistedRecord/PersistedRecordFile.hpp"

namespace {

namespace PR = Components::PersistedRecord;

constexpr uint8_t MAGIC[4] = {'P', 'R', 'T', '1'};
constexpr uint8_t OTHER_MAGIC[4] = {'P', 'R', 'T', '2'};
constexpr const char* TARGET = "/pr_test.bin";
constexpr const char* TEMP = "/pr_test.tmp";

//! Sentinel written into every output buffer before a decode; a non-OK decode
//! must leave it in place.
constexpr uint8_t SENTINEL = 0xEE;

// ----------------------------------------------------------------------
// Independent references (deliberately not the production implementations)
// ----------------------------------------------------------------------

//! Table-driven CRC-32, written independently of the bitwise production one.
uint32_t referenceCrc32(const uint8_t* data, size_t len) {
    static uint32_t table[256];
    static bool initialized = false;
    if (!initialized) {
        for (uint32_t index = 0; index < 256u; index++) {
            uint32_t value = index;
            for (int bit = 0; bit < 8; bit++) {
                value = (value & 1u) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
            }
            table[index] = value;
        }
        initialized = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t index = 0; index < len; index++) {
        crc = table[(crc ^ data[index]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

//! Deterministic pseudo-random fill so every run tests the same bytes.
void fillLcg(uint8_t* buffer, size_t len, uint32_t seed) {
    uint32_t state = seed;
    for (size_t index = 0; index < len; index++) {
        state = (state * 1664525u) + 1013904223u;
        buffer[index] = static_cast<uint8_t>((state >> 24) & 0xFFu);
    }
}

uint32_t readU32LE(const uint8_t* src) {
    return static_cast<uint32_t>(src[0]) | (static_cast<uint32_t>(src[1]) << 8) |
           (static_cast<uint32_t>(src[2]) << 16) | (static_cast<uint32_t>(src[3]) << 24);
}

//! Encode a deterministic payload of the given length into out; returns size.
uint32_t makeRecord(uint8_t* out, uint32_t outLen, uint8_t* payload, uint16_t payloadLen, uint32_t seed = 12345u) {
    fillLcg(payload, payloadLen, seed);
    return PR::encode(MAGIC, payload, payloadLen, out, outLen);
}

// ----------------------------------------------------------------------
// PersistedRecord-1: documented layout and CRC
// ----------------------------------------------------------------------

TEST(PersistedRecordCodec, EncodeLayoutMatchesDocumentedOffsets) {
    RecordProperty("verifies", "PersistedRecord-1");
    uint8_t payload[8];
    uint8_t record[PR::MAX_RECORD_SIZE];
    const uint32_t size = makeRecord(record, sizeof(record), payload, 8);
    ASSERT_EQ(size, PR::OVERHEAD + 8u);

    // magic(4) version(1) length(2, LE) payload(n) crc(4, LE)
    EXPECT_EQ(std::memcmp(record, MAGIC, PR::MAGIC_SIZE), 0);
    EXPECT_EQ(record[4], PR::FORMAT_VERSION);
    EXPECT_EQ(record[5], 8u);
    EXPECT_EQ(record[6], 0u);
    EXPECT_EQ(std::memcmp(record + PR::HEADER_SIZE, payload, 8), 0);
    EXPECT_EQ(readU32LE(record + PR::HEADER_SIZE + 8), referenceCrc32(record, PR::HEADER_SIZE + 8));
}

TEST(PersistedRecordCodec, Crc32MatchesKnownCheckVector) {
    RecordProperty("verifies", "PersistedRecord-1");
    const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT_EQ(PR::crc32(check, sizeof(check)), 0xCBF43926u);
    EXPECT_EQ(PR::crc32(check, 0), 0u);
    EXPECT_EQ(PR::crc32(nullptr, 0), 0u);
}

TEST(PersistedRecordCodec, Crc32MatchesIndependentTableReference) {
    RecordProperty("verifies", "PersistedRecord-1");
    uint8_t buffer[PR::MAX_RECORD_SIZE];
    for (uint32_t len = 0; len <= PR::MAX_RECORD_SIZE; len++) {
        fillLcg(buffer, len, 0xC0FFEEu + len);
        EXPECT_EQ(PR::crc32(buffer, len), referenceCrc32(buffer, len)) << "len " << len;
    }
}

TEST(PersistedRecordCodec, RoundTripRepresentativePayloads) {
    RecordProperty("verifies", "PersistedRecord-1");
    const uint16_t lengths[] = {0, 1, 7, static_cast<uint16_t>(PR::MAX_PAYLOAD_SIZE)};
    for (uint16_t length : lengths) {
        uint8_t payload[PR::MAX_PAYLOAD_SIZE];
        uint8_t record[PR::MAX_RECORD_SIZE];
        const uint32_t size = makeRecord(record, sizeof(record), payload, length, 7u + length);
        ASSERT_EQ(size, PR::OVERHEAD + length) << "length " << length;

        uint8_t decoded[PR::MAX_PAYLOAD_SIZE];
        std::memset(decoded, SENTINEL, sizeof(decoded));
        uint16_t decodedLen = 0xFFFF;
        ASSERT_EQ(PR::decode(MAGIC, record, size, decoded, sizeof(decoded), decodedLen), PR::Status::OK)
            << "length " << length;
        EXPECT_EQ(decodedLen, length);
        EXPECT_EQ(std::memcmp(decoded, payload, length), 0) << "length " << length;
    }
}

// ----------------------------------------------------------------------
// PersistedRecord-2: corruption detection
// ----------------------------------------------------------------------

//! Expected failure class for a corruption landing at the given offset.
bool statusMatchesOffset(uint32_t offset, uint16_t payloadLen, PR::Status status) {
    if (offset < PR::MAGIC_SIZE) {
        return status == PR::Status::BAD_MAGIC;
    }
    if (offset == 4) {  // version, checked after the CRC
        return status == PR::Status::BAD_CRC;
    }
    if (offset == 5 || offset == 6) {  // declared length
        return status == PR::Status::BAD_LENGTH || status == PR::Status::TRUNCATED;
    }
    (void)payloadLen;  // payload and CRC field alike
    return status == PR::Status::BAD_CRC;
}

TEST(PersistedRecordCodec, EverySingleByteCorruptionDetectedWithMatchingStatus) {
    RecordProperty("verifies", "PersistedRecord-2");
    const uint16_t payloadLen = 8;
    uint8_t payload[PR::MAX_PAYLOAD_SIZE];
    uint8_t pristine[PR::MAX_RECORD_SIZE];
    const uint32_t size = makeRecord(pristine, sizeof(pristine), payload, payloadLen);
    ASSERT_EQ(size, PR::OVERHEAD + payloadLen);

    for (uint32_t offset = 0; offset < size; offset++) {
        for (uint32_t value = 0; value < 256u; value++) {
            if (pristine[offset] == static_cast<uint8_t>(value)) {
                continue;  // not a corruption
            }
            uint8_t record[PR::MAX_RECORD_SIZE];
            std::memcpy(record, pristine, size);
            record[offset] = static_cast<uint8_t>(value);

            uint8_t decoded[PR::MAX_PAYLOAD_SIZE];
            std::memset(decoded, SENTINEL, sizeof(decoded));
            uint16_t decodedLen = 0xFFFF;
            const PR::Status status = PR::decode(MAGIC, record, size, decoded, sizeof(decoded), decodedLen);

            EXPECT_NE(status, PR::Status::OK) << "offset " << offset << " value " << value;
            EXPECT_TRUE(statusMatchesOffset(offset, payloadLen, status))
                << "offset " << offset << " value " << value << " status " << static_cast<uint32_t>(status);
            for (size_t index = 0; index < sizeof(decoded); index++) {
                ASSERT_EQ(decoded[index], SENTINEL) << "offset " << offset << " value " << value;
            }
        }
    }
}

TEST(PersistedRecordCodec, EverySingleBitFlipDetected) {
    RecordProperty("verifies", "PersistedRecord-2");
    uint8_t payload[PR::MAX_PAYLOAD_SIZE];
    uint8_t pristine[PR::MAX_RECORD_SIZE];
    const uint32_t size = makeRecord(pristine, sizeof(pristine), payload, 16, 999u);
    ASSERT_EQ(size, PR::OVERHEAD + 16u);

    for (uint32_t offset = 0; offset < size; offset++) {
        for (uint32_t bit = 0; bit < 8u; bit++) {
            uint8_t record[PR::MAX_RECORD_SIZE];
            std::memcpy(record, pristine, size);
            record[offset] = static_cast<uint8_t>(record[offset] ^ (1u << bit));

            uint8_t decoded[PR::MAX_PAYLOAD_SIZE];
            std::memset(decoded, SENTINEL, sizeof(decoded));
            uint16_t decodedLen = 0;
            EXPECT_NE(PR::decode(MAGIC, record, size, decoded, sizeof(decoded), decodedLen), PR::Status::OK)
                << "offset " << offset << " bit " << bit;
            EXPECT_EQ(decoded[0], SENTINEL) << "offset " << offset << " bit " << bit;
        }
    }
}

TEST(PersistedRecordCodec, EveryTruncationLengthDetected) {
    RecordProperty("verifies", "PersistedRecord-2");
    uint8_t payload[PR::MAX_PAYLOAD_SIZE];
    uint8_t record[PR::MAX_RECORD_SIZE];
    const uint32_t size = makeRecord(record, sizeof(record), payload, 8);

    for (uint32_t length = 0; length < size; length++) {
        uint8_t decoded[PR::MAX_PAYLOAD_SIZE];
        std::memset(decoded, SENTINEL, sizeof(decoded));
        uint16_t decodedLen = 0;
        EXPECT_EQ(PR::decode(MAGIC, record, length, decoded, sizeof(decoded), decodedLen), PR::Status::TRUNCATED)
            << "length " << length;
        EXPECT_EQ(decoded[0], SENTINEL) << "length " << length;
    }
}

TEST(PersistedRecordCodec, MismatchedMagicDetected) {
    RecordProperty("verifies", "PersistedRecord-2");
    uint8_t payload[PR::MAX_PAYLOAD_SIZE];
    uint8_t record[PR::MAX_RECORD_SIZE];
    const uint32_t size = makeRecord(record, sizeof(record), payload, 4);

    uint8_t decoded[PR::MAX_PAYLOAD_SIZE];
    std::memset(decoded, SENTINEL, sizeof(decoded));
    uint16_t decodedLen = 0;
    EXPECT_EQ(PR::decode(OTHER_MAGIC, record, size, decoded, sizeof(decoded), decodedLen), PR::Status::BAD_MAGIC);
    EXPECT_EQ(decoded[0], SENTINEL);
}

TEST(PersistedRecordCodec, TrailingBytesRejected) {
    RecordProperty("verifies", "PersistedRecord-2");
    uint8_t payload[PR::MAX_PAYLOAD_SIZE];
    uint8_t record[PR::MAX_RECORD_SIZE + 4];
    const uint32_t size = makeRecord(record, PR::MAX_RECORD_SIZE, payload, 8);
    record[size] = 0x00;

    uint8_t decoded[PR::MAX_PAYLOAD_SIZE];
    std::memset(decoded, SENTINEL, sizeof(decoded));
    uint16_t decodedLen = 0;
    EXPECT_EQ(PR::decode(MAGIC, record, size + 1, decoded, sizeof(decoded), decodedLen), PR::Status::BAD_LENGTH);
    EXPECT_EQ(decoded[0], SENTINEL);
}

TEST(PersistedRecordCodec, PayloadLargerThanCallerCapacityRejected) {
    RecordProperty("verifies", "PersistedRecord-2");
    uint8_t payload[PR::MAX_PAYLOAD_SIZE];
    uint8_t record[PR::MAX_RECORD_SIZE];
    const uint32_t size = makeRecord(record, sizeof(record), payload, 8);

    uint8_t decoded[4];
    std::memset(decoded, SENTINEL, sizeof(decoded));
    uint16_t decodedLen = 0;
    EXPECT_EQ(PR::decode(MAGIC, record, size, decoded, sizeof(decoded), decodedLen), PR::Status::BAD_LENGTH);
    for (size_t index = 0; index < sizeof(decoded); index++) {
        EXPECT_EQ(decoded[index], SENTINEL);
    }
}

// ----------------------------------------------------------------------
// PersistedRecord-3: format version
// ----------------------------------------------------------------------

TEST(PersistedRecordCodec, UnrecognizedVersionWithValidCrcRejected) {
    RecordProperty("verifies", "PersistedRecord-3");
    const uint16_t payloadLen = 8;
    uint8_t payload[PR::MAX_PAYLOAD_SIZE];
    uint8_t record[PR::MAX_RECORD_SIZE];
    const uint32_t size = makeRecord(record, sizeof(record), payload, payloadLen);

    // Bump the version and repair the CRC, so the record is structurally
    // intact and only the version is unrecognized.
    record[4] = static_cast<uint8_t>(PR::FORMAT_VERSION + 1);
    const uint32_t crc = referenceCrc32(record, PR::HEADER_SIZE + payloadLen);
    uint8_t* crcField = record + PR::HEADER_SIZE + payloadLen;
    crcField[0] = static_cast<uint8_t>(crc & 0xFFu);
    crcField[1] = static_cast<uint8_t>((crc >> 8) & 0xFFu);
    crcField[2] = static_cast<uint8_t>((crc >> 16) & 0xFFu);
    crcField[3] = static_cast<uint8_t>((crc >> 24) & 0xFFu);

    uint8_t decoded[PR::MAX_PAYLOAD_SIZE];
    std::memset(decoded, SENTINEL, sizeof(decoded));
    uint16_t decodedLen = 0;
    EXPECT_EQ(PR::decode(MAGIC, record, size, decoded, sizeof(decoded), decodedLen), PR::Status::BAD_VERSION);
    for (size_t index = 0; index < sizeof(decoded); index++) {
        EXPECT_EQ(decoded[index], SENTINEL);
    }
}

TEST(PersistedRecordCodec, EncodeAlwaysWritesCurrentVersion) {
    RecordProperty("verifies", "PersistedRecord-3");
    const uint16_t lengths[] = {0, 1, 32, static_cast<uint16_t>(PR::MAX_PAYLOAD_SIZE)};
    for (uint16_t length : lengths) {
        uint8_t payload[PR::MAX_PAYLOAD_SIZE];
        uint8_t record[PR::MAX_RECORD_SIZE];
        record[4] = 0xAB;
        ASSERT_NE(makeRecord(record, sizeof(record), payload, length, 3u + length), 0u);
        EXPECT_EQ(record[4], PR::FORMAT_VERSION) << "length " << length;
    }
}

// ----------------------------------------------------------------------
// PersistedRecord-7: bounded, allocation-free buffers
// ----------------------------------------------------------------------

TEST(PersistedRecordCodec, OversizePayloadRejectedAtEncode) {
    RecordProperty("verifies", "PersistedRecord-7");
    uint8_t payload[PR::MAX_PAYLOAD_SIZE + 1];
    uint8_t record[PR::MAX_RECORD_SIZE + 1];
    fillLcg(payload, sizeof(payload), 42u);

    EXPECT_EQ(PR::encode(MAGIC, payload, static_cast<uint16_t>(PR::MAX_PAYLOAD_SIZE + 1), record, sizeof(record)), 0u);
    EXPECT_EQ(PR::encode(MAGIC, payload, static_cast<uint16_t>(PR::MAX_PAYLOAD_SIZE), record, sizeof(record)),
              PR::MAX_RECORD_SIZE);
}

TEST(PersistedRecordCodec, EncodeRejectsSmallOutputBufferAndNull) {
    RecordProperty("verifies", "PersistedRecord-7");
    uint8_t payload[8];
    uint8_t record[PR::MAX_RECORD_SIZE];
    fillLcg(payload, sizeof(payload), 11u);

    EXPECT_EQ(PR::encode(MAGIC, payload, 8, record, PR::OVERHEAD + 7u), 0u);
    EXPECT_EQ(PR::encode(MAGIC, payload, 8, nullptr, sizeof(record)), 0u);
    EXPECT_EQ(PR::encode(nullptr, payload, 8, record, sizeof(record)), 0u);
    EXPECT_EQ(PR::encode(MAGIC, nullptr, 8, record, sizeof(record)), 0u);
    // A zero-length payload is legal and needs no payload pointer.
    EXPECT_EQ(PR::encode(MAGIC, nullptr, 0, record, sizeof(record)), PR::OVERHEAD);
}

TEST(PersistedRecordCodec, RecordSizeIsCompileTimeConstant) {
    RecordProperty("verifies", "PersistedRecord-7");
    static_assert(PR::OVERHEAD == 11u, "record overhead is magic(4) + version(1) + length(2) + crc(4)");
    static_assert(PR::MAX_RECORD_SIZE == PR::OVERHEAD + PR::MAX_PAYLOAD_SIZE, "max record size is compile-time");
    uint8_t stackSized[PR::MAX_RECORD_SIZE];
    EXPECT_EQ(sizeof(stackSized), static_cast<size_t>(PR::MAX_RECORD_SIZE));
}

// ----------------------------------------------------------------------
// PersistedRecord-4 / -6: the file layer
// ----------------------------------------------------------------------

class PersistedRecordFileTest : public ::testing::Test {
  protected:
    void SetUp() override { Os::Test::resetFileSystem(); }

    //! Store a deterministic payload of the given length and seed.
    static PR::Status storePayload(uint8_t* payload, uint16_t payloadLen, uint32_t seed) {
        fillLcg(payload, payloadLen, seed);
        return PR::store(TARGET, TEMP, MAGIC, payload, payloadLen);
    }

    //! Load into a sentinel-filled buffer.
    static PR::Status loadPayload(uint8_t* out, size_t outLen, uint16_t& outLenRead, bool useTemp = true) {
        std::memset(out, SENTINEL, outLen);
        outLenRead = 0xFFFF;
        return PR::load(TARGET, useTemp ? TEMP : nullptr, MAGIC, out, static_cast<uint16_t>(outLen), outLenRead);
    }

    //! Assert the target still decodes to the given payload.
    static void expectTargetHolds(const uint8_t* expected, uint16_t expectedLen) {
        uint8_t decoded[PR::MAX_PAYLOAD_SIZE];
        uint16_t decodedLen = 0;
        ASSERT_EQ(loadPayload(decoded, sizeof(decoded), decodedLen), PR::Status::OK);
        EXPECT_EQ(decodedLen, expectedLen);
        EXPECT_EQ(std::memcmp(decoded, expected, expectedLen), 0);
    }
};

TEST_F(PersistedRecordFileTest, StoreThenLoadRoundTripsAndLeavesNoTemp) {
    RecordProperty("verifies", "PersistedRecord-1,PersistedRecord-4");
    uint8_t payload[16];
    ASSERT_EQ(storePayload(payload, sizeof(payload), 1u), PR::Status::OK);

    // The staging file was renamed onto the target, so nothing is left behind.
    EXPECT_EQ(Os::Test::fileSystem().files.count(TEMP), 0u);
    const std::vector<U8>& stored = Os::Test::fileSystem().files[TARGET];
    ASSERT_EQ(stored.size(), static_cast<size_t>(PR::OVERHEAD + sizeof(payload)));
    EXPECT_EQ(std::memcmp(stored.data(), MAGIC, PR::MAGIC_SIZE), 0);
    EXPECT_EQ(stored[4], PR::FORMAT_VERSION);
    EXPECT_EQ(readU32LE(stored.data() + PR::HEADER_SIZE + sizeof(payload)),
              referenceCrc32(stored.data(), PR::HEADER_SIZE + sizeof(payload)));

    expectTargetHolds(payload, sizeof(payload));
}

TEST_F(PersistedRecordFileTest, StoreFailureAtOpenKeepsPreviousRecord) {
    RecordProperty("verifies", "PersistedRecord-4");
    uint8_t first[8];
    ASSERT_EQ(storePayload(first, sizeof(first), 2u), PR::Status::OK);

    uint8_t second[8];
    Os::Test::fileSystem().failOpenCreate = true;
    EXPECT_EQ(storePayload(second, sizeof(second), 3u), PR::Status::OPEN_ERROR);
    Os::Test::fileSystem().failOpenCreate = false;

    expectTargetHolds(first, sizeof(first));
}

TEST_F(PersistedRecordFileTest, StoreFailureAtWriteKeepsPreviousRecord) {
    RecordProperty("verifies", "PersistedRecord-4");
    uint8_t first[8];
    ASSERT_EQ(storePayload(first, sizeof(first), 4u), PR::Status::OK);

    uint8_t second[8];
    Os::Test::fileSystem().failWrite = true;
    EXPECT_EQ(storePayload(second, sizeof(second), 5u), PR::Status::WRITE_ERROR);
    Os::Test::fileSystem().failWrite = false;

    expectTargetHolds(first, sizeof(first));
}

TEST_F(PersistedRecordFileTest, StoreFailureAtPartialWriteKeepsPreviousRecord) {
    RecordProperty("verifies", "PersistedRecord-4");
    uint8_t first[8];
    ASSERT_EQ(storePayload(first, sizeof(first), 6u), PR::Status::OK);

    uint8_t second[8];
    Os::Test::fileSystem().partialWrite = true;
    EXPECT_EQ(storePayload(second, sizeof(second), 7u), PR::Status::WRITE_ERROR);
    Os::Test::fileSystem().partialWrite = false;

    expectTargetHolds(first, sizeof(first));
}

TEST_F(PersistedRecordFileTest, StoreFailureAtSyncKeepsPreviousRecord) {
    RecordProperty("verifies", "PersistedRecord-4");
    uint8_t first[8];
    ASSERT_EQ(storePayload(first, sizeof(first), 8u), PR::Status::OK);

    uint8_t second[8];
    Os::Test::fileSystem().failFlush = true;
    EXPECT_EQ(storePayload(second, sizeof(second), 9u), PR::Status::SYNC_ERROR);
    Os::Test::fileSystem().failFlush = false;

    expectTargetHolds(first, sizeof(first));
}

TEST_F(PersistedRecordFileTest, StoreFailureAtRenameKeepsPreviousRecord) {
    RecordProperty("verifies", "PersistedRecord-4");
    uint8_t first[8];
    ASSERT_EQ(storePayload(first, sizeof(first), 10u), PR::Status::OK);

    uint8_t second[8];
    Os::Test::fileSystem().failRename = true;
    EXPECT_EQ(storePayload(second, sizeof(second), 11u), PR::Status::RENAME_ERROR);
    Os::Test::fileSystem().failRename = false;

    expectTargetHolds(first, sizeof(first));
}

TEST_F(PersistedRecordFileTest, StaleTempDoesNotBlockSubsequentStore) {
    RecordProperty("verifies", "PersistedRecord-4");
    uint8_t first[8];
    ASSERT_EQ(storePayload(first, sizeof(first), 12u), PR::Status::OK);

    // A failed update leaves a full-size temporary behind ...
    uint8_t second[32];
    Os::Test::fileSystem().failRename = true;
    ASSERT_EQ(storePayload(second, sizeof(second), 13u), PR::Status::RENAME_ERROR);
    Os::Test::fileSystem().failRename = false;
    ASSERT_EQ(Os::Test::fileSystem().files.count(TEMP), 1u);

    // ... which the next store truncates rather than appends to.
    uint8_t third[8];
    ASSERT_EQ(storePayload(third, sizeof(third), 14u), PR::Status::OK);
    EXPECT_EQ(Os::Test::fileSystem().files.count(TEMP), 0u);
    expectTargetHolds(third, sizeof(third));
}

TEST_F(PersistedRecordFileTest, LoadFallsBackToTempWhenTargetAbsent) {
    RecordProperty("verifies", "PersistedRecord-4");
    // First-ever store fails at the rename: the target never appears, but the
    // temporary holds a complete, flushed record.
    uint8_t payload[8];
    Os::Test::fileSystem().failRename = true;
    ASSERT_EQ(storePayload(payload, sizeof(payload), 15u), PR::Status::RENAME_ERROR);
    Os::Test::fileSystem().failRename = false;
    ASSERT_EQ(Os::Test::fileSystem().files.count(TARGET), 0u);

    uint8_t decoded[PR::MAX_PAYLOAD_SIZE];
    uint16_t decodedLen = 0;
    EXPECT_EQ(loadPayload(decoded, sizeof(decoded), decodedLen), PR::Status::OK);
    EXPECT_EQ(decodedLen, sizeof(payload));
    EXPECT_EQ(std::memcmp(decoded, payload, sizeof(payload)), 0);

    // The fallback is read-only: the target is not created at load time.
    EXPECT_EQ(Os::Test::fileSystem().files.count(TARGET), 0u);
}

TEST_F(PersistedRecordFileTest, LoadIgnoresTempWhenTargetPresent) {
    RecordProperty("verifies", "PersistedRecord-4");
    uint8_t first[8];
    ASSERT_EQ(storePayload(first, sizeof(first), 16u), PR::Status::OK);

    uint8_t second[8];
    Os::Test::fileSystem().failRename = true;
    ASSERT_EQ(storePayload(second, sizeof(second), 17u), PR::Status::RENAME_ERROR);
    Os::Test::fileSystem().failRename = false;
    ASSERT_EQ(Os::Test::fileSystem().files.count(TEMP), 1u);

    // The stale temporary beside a present target is not consulted.
    expectTargetHolds(first, sizeof(first));
}

TEST_F(PersistedRecordFileTest, LoadMissingFileReturnsMissingAndLeavesPayloadUntouched) {
    RecordProperty("verifies", "PersistedRecord-6");
    uint8_t decoded[PR::MAX_PAYLOAD_SIZE];
    uint16_t decodedLen = 0;

    EXPECT_EQ(loadPayload(decoded, sizeof(decoded), decodedLen, false), PR::Status::MISSING);
    for (size_t index = 0; index < sizeof(decoded); index++) {
        ASSERT_EQ(decoded[index], SENTINEL);
    }
    // Same answer with a temporary path supplied but no temporary present.
    EXPECT_EQ(loadPayload(decoded, sizeof(decoded), decodedLen, true), PR::Status::MISSING);
    EXPECT_EQ(decoded[0], SENTINEL);
}

TEST_F(PersistedRecordFileTest, CorruptTempWithNoTargetReturnsMissing) {
    RecordProperty("verifies", "PersistedRecord-6");
    // A torn temporary with no target is indistinguishable from a first boot:
    // the consumer must apply its default, not raise a corruption warning.
    const uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03, 0x04};
    Os::Test::fileSystem().files[TEMP].assign(garbage, garbage + sizeof(garbage));

    uint8_t decoded[PR::MAX_PAYLOAD_SIZE];
    uint16_t decodedLen = 0;
    EXPECT_EQ(loadPayload(decoded, sizeof(decoded), decodedLen), PR::Status::MISSING);
    for (size_t index = 0; index < sizeof(decoded); index++) {
        ASSERT_EQ(decoded[index], SENTINEL);
    }
}

TEST_F(PersistedRecordFileTest, LoadCorruptFileReturnsCorruptStatusAndLeavesPayloadUntouched) {
    RecordProperty("verifies", "PersistedRecord-2");
    uint8_t payload[8];
    ASSERT_EQ(storePayload(payload, sizeof(payload), 18u), PR::Status::OK);
    Os::Test::fileSystem().files[TARGET][PR::HEADER_SIZE] ^= 0xFF;  // flip a payload byte

    uint8_t decoded[PR::MAX_PAYLOAD_SIZE];
    uint16_t decodedLen = 0;
    EXPECT_EQ(loadPayload(decoded, sizeof(decoded), decodedLen), PR::Status::BAD_CRC);
    for (size_t index = 0; index < sizeof(decoded); index++) {
        ASSERT_EQ(decoded[index], SENTINEL);
    }
}

}  // namespace
