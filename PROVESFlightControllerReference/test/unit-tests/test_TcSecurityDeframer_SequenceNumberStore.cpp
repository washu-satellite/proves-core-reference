// ======================================================================
// \title  test_TcSecurityDeframer_SequenceNumberStore.cpp
// \brief  Host unit tests for the persisted anti-replay sequence number.
//
// Level: Unit. Compiles the real
// Components/TcSecurityDeframer/SequenceNumberStore.cpp against the in-memory
// Os::File / Os::FileSystem stubs in support/; no F Prime, Zephyr or mbedTLS
// code is linked (test/unit-tests/README.md). TcSecurityDeframer.cpp itself
// pulls in <psa/crypto.h>, Fw::Buffer and ComCfg::FrameContext and cannot be
// built on the host, which is why the whole load/store decision lives in this
// helper.
//
// Requirement verified: AUTH013 (the sequence number is a PersistedRecord at
// SEQ_NUM_FILE_PATH, updated atomically; a corrupt or truncated record is
// detected, warned about once, and replaced by the first-boot baseline 0
// instead of being adopted).
//
// Evidence note: the helper returns exactly one CORRUPT result per load, and
// TcSecurityDeframer::readSequenceNumber maps that single result to exactly
// one log_WARNING_LO_SequenceNumberRecordInvalid followed by store(0) -- one
// branch, one call, checked by the target compile. The event count assertion
// here is therefore made on the CORRUPT results the mapping is driven by, and
// the write-back is exercised by calling store(0) as the component does.
//
// Oracle (TP-3): expected values come from the AUTH013 pass criteria and the
// record layout the design fixes -- magic "ASN1", a 4-byte little-endian
// payload, and the PersistedRecord header/CRC framing in
// PersistedRecordCodec.hpp. Corruption coverage follows
// test_TelemetryGate_Component.cpp.
// ======================================================================

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Os/File.hpp"
#include "PROVESFlightControllerReference/Components/PersistedRecord/PersistedRecordCodec.hpp"
#include "PROVESFlightControllerReference/Components/TcSecurityDeframer/SequenceNumberStore.hpp"

namespace {

namespace PR = Components::PersistedRecord;
namespace SNS = Components::SequenceNumberStore;

using SNS::LoadResult;

//! The SEQ_NUM_FILE_PATH parameter default (TcSecurityDeframer.fpp) and the
//! staging file the helper derives from it.
constexpr const char* SEQ_FILE = "/sequence_number.bin";
constexpr const char* SEQ_TEMP = "/sequence_number.bin.tmp";
//! The path upstream's FileHelper image used: a bare U32 blob, binary despite
//! the extension, with no magic, version or CRC.
constexpr const char* LEGACY_FILE = "//sequence_number.txt";

// Must match the constants in SequenceNumberStore.cpp / .hpp.
constexpr uint8_t SEQ_MAGIC[4] = {'A', 'S', 'N', '1'};
constexpr uint16_t SEQ_PAYLOAD_SIZE = 4;
constexpr uint32_t SEQ_RECORD_SIZE = PR::OVERHEAD + SEQ_PAYLOAD_SIZE;
constexpr size_t OFF_VERSION = PR::MAGIC_SIZE;

class SequenceNumberStoreTest : public ::testing::Test {
  protected:
    void SetUp() override { Os::Test::resetFileSystem(); }

    static std::vector<U8>& blobAt(const char* path) { return Os::Test::fileSystem().files[path]; }
    static std::vector<U8>& seqBlob() { return blobAt(SEQ_FILE); }

    //! Write a valid record holding value to path, through the real store path.
    static void seedValidAt(const char* path, uint32_t value) {
        uint32_t status = 0;
        ASSERT_TRUE(SNS::store(path, value, status));
        ASSERT_EQ(status, static_cast<uint32_t>(PR::Status::OK));
        ASSERT_EQ(blobAt(path).size(), SEQ_RECORD_SIZE);
    }
    static void seedValid(uint32_t value) { seedValidAt(SEQ_FILE, value); }
};

// ----------------------------------------------------------------------
// Round trip and first boot
// ----------------------------------------------------------------------

TEST_F(SequenceNumberStoreTest, ValidFileRoundTripsStoredValue) {
    RecordProperty("verifies", "AUTH013");

    const uint32_t values[] = {0u, 1u, 42u, 0x7FFFFFFFu, 0xFFFFFFFFu};
    for (uint32_t expected : values) {
        Os::Test::resetFileSystem();
        ASSERT_NO_FATAL_FAILURE(seedValid(expected));

        // The record carries the shared framing, and the payload is explicit
        // little-endian bytes rather than a struct image.
        const std::vector<U8>& blob = seqBlob();
        EXPECT_EQ(blob[0], 'A');
        EXPECT_EQ(blob[1], 'S');
        EXPECT_EQ(blob[2], 'N');
        EXPECT_EQ(blob[3], '1');
        EXPECT_EQ(blob[OFF_VERSION], PR::FORMAT_VERSION);
        EXPECT_EQ(blob[PR::HEADER_SIZE + 0], static_cast<U8>(expected & 0xFFu));
        EXPECT_EQ(blob[PR::HEADER_SIZE + 3], static_cast<U8>((expected >> 24) & 0xFFu));

        // A fresh load -- what configure() does after a reboot -- reads the
        // stored value back.
        uint32_t value = 0xDEADBEEFu;
        uint32_t status = 0;
        EXPECT_EQ(SNS::load(SEQ_FILE, value, status), LoadResult::LOADED) << "value " << expected;
        EXPECT_EQ(value, expected);
        EXPECT_EQ(status, static_cast<uint32_t>(PR::Status::OK));
        // The staging file is renamed onto the target, so nothing is left behind.
        EXPECT_EQ(Os::Test::fileSystem().files.count(SEQ_TEMP), 0u);
    }
}

TEST_F(SequenceNumberStoreTest, PathIsTakenFromTheCallerAndStagedAtPathDotTmp) {
    RecordProperty("verifies", "AUTH013");

    // The component passes its SEQ_NUM_FILE_PATH parameter; a non-default
    // path must round-trip through the same layout, staged at "<path>.tmp".
    constexpr const char* OTHER = "/lfs/seq_alt.bin";
    ASSERT_NO_FATAL_FAILURE(seedValidAt(OTHER, 777u));
    EXPECT_EQ(Os::Test::fileSystem().files.count(SEQ_FILE), 0u) << "the default path is untouched";
    EXPECT_EQ(Os::Test::fileSystem().files.count("/lfs/seq_alt.bin.tmp"), 0u) << "staging file renamed away";

    uint32_t value = 0xDEADBEEFu;
    uint32_t status = 0;
    EXPECT_EQ(SNS::load(OTHER, value, status), LoadResult::LOADED);
    EXPECT_EQ(value, 777u);

    // A failed rename leaves the staging file at exactly "<path>.tmp".
    Os::Test::fileSystem().failRename = true;
    EXPECT_FALSE(SNS::store(OTHER, 778u, status));
    EXPECT_EQ(Os::Test::fileSystem().files.count("/lfs/seq_alt.bin.tmp"), 1u);
    Os::Test::fileSystem().failRename = false;

    // A path too long for the staging buffer is refused up front, never
    // truncated into some other file.
    std::string longPath(SNS::MAX_TEMP_PATH_SIZE, 'p');
    longPath[0] = '/';
    EXPECT_FALSE(SNS::store(longPath.c_str(), 1u, status));
    EXPECT_EQ(status, static_cast<uint32_t>(PR::Status::INVALID_ARGUMENT));
    EXPECT_EQ(SNS::load(longPath.c_str(), value, status), LoadResult::CORRUPT);
    EXPECT_EQ(value, 0u);
    EXPECT_EQ(status, static_cast<uint32_t>(PR::Status::INVALID_ARGUMENT));
}

TEST_F(SequenceNumberStoreTest, NoFileIsFirstBootBaselineZeroAndWritesNothing) {
    RecordProperty("verifies", "AUTH013");

    ASSERT_EQ(Os::Test::fileSystem().files.count(SEQ_FILE), 0u);

    uint32_t value = 0xDEADBEEFu;
    uint32_t status = 0;
    EXPECT_EQ(SNS::load(SEQ_FILE, value, status), LoadResult::FIRST_BOOT);
    EXPECT_EQ(value, 0u) << "The first-boot baseline is sequence number 0";
    EXPECT_EQ(status, static_cast<uint32_t>(PR::Status::MISSING));
    // A missing file is not a corruption: the component emits no event on this
    // result, and the load path itself writes nothing.
    EXPECT_EQ(Os::Test::fileSystem().files.count(SEQ_FILE), 0u);
    EXPECT_EQ(Os::Test::fileSystem().files.count(SEQ_TEMP), 0u);
}

// ----------------------------------------------------------------------
// Validation failures fall back to the baseline
// ----------------------------------------------------------------------

TEST_F(SequenceNumberStoreTest, EverySingleByteCorruptionIsCorruptExactlyOnceThenBaselineIsRewritten) {
    RecordProperty("verifies", "AUTH013");

    for (size_t offset = 0; offset < SEQ_RECORD_SIZE; offset++) {
        for (uint32_t byte = 0; byte < 256u; byte++) {
            Os::Test::resetFileSystem();
            ASSERT_NO_FATAL_FAILURE(seedValid(0x11223344u));
            std::vector<U8>& blob = seqBlob();
            if (blob[offset] == static_cast<U8>(byte)) {
                continue;  // not a corruption
            }
            blob[offset] = static_cast<U8>(byte);

            uint32_t value = 0xDEADBEEFu;
            uint32_t status = 0;
            const LoadResult result = SNS::load(SEQ_FILE, value, status);

            const std::string where = "offset " + std::to_string(offset) + " byte " + std::to_string(byte);
            // Exactly one CORRUPT result -- the component's single warning.
            EXPECT_EQ(result, LoadResult::CORRUPT) << where;
            EXPECT_EQ(value, 0u) << where;
            EXPECT_NE(status, static_cast<uint32_t>(PR::Status::OK)) << where;

            // The component then stores the baseline so the next boot does not
            // re-warn: the rewritten record is valid and reads back as 0.
            EXPECT_TRUE(SNS::store(SEQ_FILE, 0u, status)) << where;
            value = 0xDEADBEEFu;
            EXPECT_EQ(SNS::load(SEQ_FILE, value, status), LoadResult::LOADED) << where;
            EXPECT_EQ(value, 0u) << where;
            EXPECT_EQ(status, static_cast<uint32_t>(PR::Status::OK)) << where;
        }
    }
}

TEST_F(SequenceNumberStoreTest, EveryTruncationLengthIsCorruptWithBaselineZero) {
    RecordProperty("verifies", "AUTH013");

    for (size_t length = 0; length < SEQ_RECORD_SIZE; length++) {
        Os::Test::resetFileSystem();
        ASSERT_NO_FATAL_FAILURE(seedValid(0x11223344u));
        seqBlob().resize(length);

        uint32_t value = 0xDEADBEEFu;
        uint32_t status = 0;
        const LoadResult result = SNS::load(SEQ_FILE, value, status);

        const std::string where = "length " + std::to_string(length);
        EXPECT_EQ(result, LoadResult::CORRUPT) << where;
        EXPECT_EQ(value, 0u) << where;
    }
}

TEST_F(SequenceNumberStoreTest, WrongMagicAndWrongVersionAreCorrupt) {
    RecordProperty("verifies", "AUTH013");

    // A record from another consumer: perfect framing and CRC, wrong owner.
    {
        const uint8_t otherMagic[4] = {'T', 'G', 'S', '2'};
        const uint8_t payload[SEQ_PAYLOAD_SIZE] = {1, 0, 0, 0};
        U8 buffer[PR::MAX_RECORD_SIZE];
        const uint32_t size = PR::encode(otherMagic, payload, SEQ_PAYLOAD_SIZE, buffer, sizeof(buffer));
        ASSERT_EQ(size, SEQ_RECORD_SIZE);
        seqBlob().assign(buffer, buffer + size);

        uint32_t value = 0xDEADBEEFu;
        uint32_t status = 0;
        EXPECT_EQ(SNS::load(SEQ_FILE, value, status), LoadResult::CORRUPT);
        EXPECT_EQ(value, 0u);
        EXPECT_EQ(status, static_cast<uint32_t>(PR::Status::BAD_MAGIC));
    }

    // An otherwise intact record declaring a format version this image does not
    // know: the CRC is recomputed so only the version is wrong.
    {
        Os::Test::resetFileSystem();
        ASSERT_NO_FATAL_FAILURE(seedValid(99u));
        std::vector<U8>& blob = seqBlob();
        blob[OFF_VERSION] = static_cast<U8>(PR::FORMAT_VERSION + 1);
        const uint32_t crc = PR::crc32(blob.data(), SEQ_RECORD_SIZE - PR::CRC_SIZE);
        for (uint32_t i = 0; i < PR::CRC_SIZE; i++) {
            blob[SEQ_RECORD_SIZE - PR::CRC_SIZE + i] = static_cast<U8>((crc >> (8 * i)) & 0xFFu);
        }

        uint32_t value = 0xDEADBEEFu;
        uint32_t status = 0;
        EXPECT_EQ(SNS::load(SEQ_FILE, value, status), LoadResult::CORRUPT);
        EXPECT_EQ(value, 0u);
        EXPECT_EQ(status, static_cast<uint32_t>(PR::Status::BAD_VERSION));
    }
}

TEST_F(SequenceNumberStoreTest, ShortPayloadInCrcValidRecordIsCorrupt) {
    RecordProperty("verifies", "AUTH013");

    // A CRC-valid record declaring a 3-byte payload decodes cleanly but cannot
    // supply a U32; adopting its low bytes would silently rewind the window.
    const uint8_t payload[3] = {0xAA, 0xBB, 0xCC};
    U8 buffer[PR::MAX_RECORD_SIZE];
    const uint32_t size = PR::encode(SEQ_MAGIC, payload, 3, buffer, sizeof(buffer));
    ASSERT_NE(size, 0u);
    seqBlob().assign(buffer, buffer + size);

    uint32_t value = 0xDEADBEEFu;
    uint32_t status = 0;
    EXPECT_EQ(SNS::load(SEQ_FILE, value, status), LoadResult::CORRUPT);
    EXPECT_EQ(value, 0u);
    EXPECT_EQ(status, static_cast<uint32_t>(PR::Status::OK)) << "The framing is valid; the payload length is not";
}

// ----------------------------------------------------------------------
// Atomic update
// ----------------------------------------------------------------------

TEST_F(SequenceNumberStoreTest, StoreFailureAtEveryStepKeepsPreviousRecord) {
    RecordProperty("verifies", "AUTH013");

    ASSERT_NO_FATAL_FAILURE(seedValid(1234u));
    const std::vector<U8> before = seqBlob();
    Os::Test::FileSystemState& fs = Os::Test::fileSystem();
    uint32_t status = 0;

    // Each step of the update protocol is made to fail in turn through the
    // Os::File fake; the component reports the returned status in
    // SequenceNumberRecordInvalid and returns OTHER_ERROR to SET_SEQ_NUM.
    fs.failOpenCreate = true;
    EXPECT_FALSE(SNS::store(SEQ_FILE, 5678u, status));
    EXPECT_EQ(status, static_cast<uint32_t>(PR::Status::OPEN_ERROR));
    EXPECT_EQ(seqBlob(), before) << "open failure: previous record byte-for-byte intact";
    fs.failOpenCreate = false;

    fs.failWrite = true;
    EXPECT_FALSE(SNS::store(SEQ_FILE, 5678u, status));
    EXPECT_EQ(status, static_cast<uint32_t>(PR::Status::WRITE_ERROR));
    EXPECT_EQ(seqBlob(), before) << "write failure: previous record byte-for-byte intact";
    fs.failWrite = false;

    fs.failFlush = true;
    EXPECT_FALSE(SNS::store(SEQ_FILE, 5678u, status));
    EXPECT_EQ(status, static_cast<uint32_t>(PR::Status::SYNC_ERROR));
    EXPECT_EQ(seqBlob(), before) << "flush failure: previous record byte-for-byte intact";
    fs.failFlush = false;

    // Rename over the target fails: the step the update protocol exists for.
    fs.failRename = true;
    EXPECT_FALSE(SNS::store(SEQ_FILE, 5678u, status));
    EXPECT_EQ(status, static_cast<uint32_t>(PR::Status::RENAME_ERROR));
    EXPECT_EQ(seqBlob(), before) << "rename failure: previous record byte-for-byte intact";
    fs.failRename = false;

    // The previous value is still readable after every failure.
    uint32_t value = 0;
    EXPECT_EQ(SNS::load(SEQ_FILE, value, status), LoadResult::LOADED);
    EXPECT_EQ(value, 1234u);
}

TEST_F(SequenceNumberStoreTest, LegacyFileAtOldPathIsIgnored) {
    RecordProperty("verifies", "AUTH013");

    // The FileHelper image wrote a bare U32 to "//sequence_number.txt". The
    // new default path is different, so the old file is orphaned rather than
    // misread: the upgrade boot is a clean first boot with no warning, and the
    // ground resyncs silently through the replay window.
    const U8 legacy[4] = {0x00, 0x00, 0x30, 0x39};  // 12345, big-endian
    Os::Test::fileSystem().files[LEGACY_FILE].assign(legacy, legacy + sizeof(legacy));

    uint32_t value = 0xDEADBEEFu;
    uint32_t status = 0;
    EXPECT_EQ(SNS::load(SEQ_FILE, value, status), LoadResult::FIRST_BOOT);
    EXPECT_EQ(value, 0u);
    EXPECT_EQ(Os::Test::fileSystem().files.count(SEQ_FILE), 0u) << "and it is not rewritten in the new format";
    EXPECT_EQ(Os::Test::fileSystem().files.count(LEGACY_FILE), 1u) << "the orphan is left alone, not deleted";
}

}  // namespace
