// ======================================================================
// \title  test_StartupManager_Persistence.cpp
// \brief  Host unit tests for StartupManager boot-count / quiescence files.
//
// Level: Unit. Compiles the real StartupManager.cpp against the recorder stub
// in support/PROVESFlightControllerReference/Components/StartupManager/ plus
// the in-memory Os::File / Os::FileSystem, a fake Fw::Time and a fake
// <zephyr/drivers/rtc.h>; no F Prime or Zephyr code is linked
// (test/unit-tests/README.md).
//
// Requirement verified: REQ-SM-008 (both files are PersistedRecords updated
// atomically; a corrupt or truncated file is detected, warns, and falls back
// to a defined default -- boot count 0 before the minimum-of-one bump, and
// quiescence restarted from now).
//
// Oracle (TP-3): expected values come from the REQ-SM-008 pass criteria and
// the record layouts the design fixes -- magic "SBC1" over an 8-byte
// little-endian count, magic "SQS1" over the 11-byte time layout mirroring
// Fw::Time::SERIALIZED_SIZE (TimeBase U16, context U8, seconds U32, useconds
// U32; lib/fprime/default/config/FpConfig.fpp:79,92) -- plus the parameter
// defaults at StartupManager.fpp:49-61. Corruption coverage follows
// test_TelemetryGate_Component.cpp:239-276.
// ======================================================================

#include <gtest/gtest.h>

#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "Os/File.hpp"
#include "PROVESFlightControllerReference/Components/PersistedRecord/PersistedRecordCodec.hpp"
#include "PROVESFlightControllerReference/Components/StartupManager/StartupManager.hpp"

namespace {

using Components::StartupManager;
using Components::StartupManagerComponentBase;

namespace PR = Components::PersistedRecord;

// Parameter defaults, StartupManager.fpp:55,61.
constexpr const char* BOOT_FILE = "/boot_count.bin";
constexpr const char* BOOT_TEMP = "/boot_count.bin.tmp";
constexpr const char* QUIESCENCE_FILE = "/quiescence_start.bin";
constexpr const char* QUIESCENCE_TEMP = "/quiescence_start.bin.tmp";
constexpr const char* STARTUP_SEQUENCE = "/seq/startup.bin";

constexpr FwOpcodeType OPCODE = 0x55;

// Must match the constants in StartupManager.cpp.
constexpr U8 BOOT_MAGIC[4] = {'S', 'B', 'C', '1'};
constexpr U8 QUIESCENCE_MAGIC[4] = {'S', 'Q', 'S', '1'};
constexpr U16 BOOT_PAYLOAD_SIZE = 8;
constexpr U16 QUIESCENCE_PAYLOAD_SIZE = 11;
constexpr uint32_t BOOT_RECORD_SIZE = PR::OVERHEAD + BOOT_PAYLOAD_SIZE;
constexpr uint32_t QUIESCENCE_RECORD_SIZE = PR::OVERHEAD + QUIESCENCE_PAYLOAD_SIZE;

// The time the recorder stub serves through getTime() unless a test changes it.
constexpr U32 NOW_SECONDS = 1000;
constexpr U32 NOW_USECONDS = 250;

//! Hold a StartupManager in zeroed storage.
//!
//! StartupManager's constructor leaves m_boot_count, m_waiting, m_stored_opcode
//! and m_stored_sequence uninitialized. In flight the component is a file-scope
//! instance created by the topology, so those members are zero-initialized
//! before the constructor runs; run_handler's "first call" branch
//! (m_boot_count == 0) depends on it. Placement-new into a zeroed buffer
//! reproduces the flight condition rather than testing indeterminate values.
class Instance {
  public:
    Instance() {
        std::memset(this->m_storage, 0, sizeof(this->m_storage));
        this->m_component = new (this->m_storage) StartupManager("startupManager");
    }
    ~Instance() { this->m_component->~StartupManager(); }
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;

    StartupManager& operator*() const { return *this->m_component; }
    StartupManager* operator->() const { return this->m_component; }
    StartupManagerComponentBase& base() const { return static_cast<StartupManagerComponentBase&>(*this->m_component); }

  private:
    alignas(StartupManager) unsigned char m_storage[sizeof(StartupManager)];
    StartupManager* m_component;
};

class StartupManagerPersistenceTest : public ::testing::Test {
  protected:
    void SetUp() override { Os::Test::resetFileSystem(); }

    static std::vector<U8>& blobAt(const char* path) { return Os::Test::fileSystem().files[path]; }

    //! Write a well-formed boot-count record holding count.
    static void seedBootCount(U64 count) {
        U8 payload[BOOT_PAYLOAD_SIZE];
        for (U8 i = 0; i < BOOT_PAYLOAD_SIZE; i++) {
            payload[i] = static_cast<U8>((count >> (8 * i)) & 0xFFU);
        }
        U8 buffer[PR::MAX_RECORD_SIZE];
        const uint32_t size = PR::encode(BOOT_MAGIC, payload, BOOT_PAYLOAD_SIZE, buffer, sizeof(buffer));
        ASSERT_EQ(size, BOOT_RECORD_SIZE);
        blobAt(BOOT_FILE).assign(buffer, buffer + size);
    }

    //! Write a well-formed quiescence record holding the given fields.
    static void seedQuiescence(U16 timeBase, U8 context, U32 seconds, U32 useconds) {
        U8 payload[QUIESCENCE_PAYLOAD_SIZE];
        payload[0] = static_cast<U8>(timeBase & 0xFFU);
        payload[1] = static_cast<U8>((timeBase >> 8) & 0xFFU);
        payload[2] = context;
        for (U8 i = 0; i < 4; i++) {
            payload[3 + i] = static_cast<U8>((seconds >> (8 * i)) & 0xFFU);
            payload[7 + i] = static_cast<U8>((useconds >> (8 * i)) & 0xFFU);
        }
        U8 buffer[PR::MAX_RECORD_SIZE];
        const uint32_t size = PR::encode(QUIESCENCE_MAGIC, payload, QUIESCENCE_PAYLOAD_SIZE, buffer, sizeof(buffer));
        ASSERT_EQ(size, QUIESCENCE_RECORD_SIZE);
        blobAt(QUIESCENCE_FILE).assign(buffer, buffer + size);
    }

    //! Decode the boot-count file with the shared codec.
    static PR::Status decodeBootCount(U64& count) {
        const std::vector<U8>& blob = blobAt(BOOT_FILE);
        U8 payload[BOOT_PAYLOAD_SIZE] = {0};
        U16 len = 0;
        const PR::Status status =
            PR::decode(BOOT_MAGIC, blob.data(), static_cast<uint32_t>(blob.size()), payload, BOOT_PAYLOAD_SIZE, len);
        count = 0;
        if (status == PR::Status::OK && len == BOOT_PAYLOAD_SIZE) {
            for (U8 i = 0; i < BOOT_PAYLOAD_SIZE; i++) {
                count |= static_cast<U64>(payload[i]) << (8 * i);
            }
        }
        return status;
    }

    //! Decode the quiescence file with the shared codec.
    static PR::Status decodeQuiescence(U32& seconds, U32& useconds) {
        const std::vector<U8>& blob = blobAt(QUIESCENCE_FILE);
        U8 payload[QUIESCENCE_PAYLOAD_SIZE] = {0};
        U16 len = 0;
        const PR::Status status = PR::decode(QUIESCENCE_MAGIC, blob.data(), static_cast<uint32_t>(blob.size()), payload,
                                             QUIESCENCE_PAYLOAD_SIZE, len);
        seconds = 0;
        useconds = 0;
        if (status == PR::Status::OK && len == QUIESCENCE_PAYLOAD_SIZE) {
            for (U8 i = 0; i < 4; i++) {
                seconds |= static_cast<U32>(payload[3 + i]) << (8 * i);
                useconds |= static_cast<U32>(payload[7 + i]) << (8 * i);
            }
        }
        return status;
    }

    //! Set the time the component's getTime() returns.
    static void setNow(Instance& sm, U32 seconds, U32 useconds) {
        sm.base().injectedTime = Fw::Time(TimeBase::TB_WORKSTATION_TIME, 0, seconds, useconds);
    }
};

// ----------------------------------------------------------------------
// First boot and round trips
// ----------------------------------------------------------------------

TEST_F(StartupManagerPersistenceTest, FirstBootWithNoFilesCountsOneAndWritesQuiescenceWithoutEvents) {
    RecordProperty("verifies", "REQ-SM-008");

    ASSERT_EQ(Os::Test::fileSystem().files.count(BOOT_FILE), 0u);
    ASSERT_EQ(Os::Test::fileSystem().files.count(QUIESCENCE_FILE), 0u);

    Instance sm;
    setNow(sm, NOW_SECONDS, NOW_USECONDS);
    sm.base().run_handler(0, 0);

    // Missing files are the first boot: defined defaults, and no warning.
    EXPECT_EQ(sm.base().eventsBootCountUpdateFailure, 0u);
    EXPECT_EQ(sm.base().eventsQuiescenceFileInitFailure, 0u);
    ASSERT_FALSE(sm.base().tlmBootCount.empty());
    EXPECT_EQ(sm.base().tlmBootCount.back(), 1u) << "The first counted boot is boot 1";
    ASSERT_EQ(sm.base().runSequenceCalls.size(), 1u);
    EXPECT_EQ(sm.base().runSequenceCalls[0], STARTUP_SEQUENCE);

    U64 count = 0;
    EXPECT_EQ(decodeBootCount(count), PR::Status::OK) << "The boot count file decodes with the shared codec";
    EXPECT_EQ(count, 1u);
    U32 seconds = 0;
    U32 useconds = 0;
    EXPECT_EQ(decodeQuiescence(seconds, useconds), PR::Status::OK);
    EXPECT_EQ(seconds, NOW_SECONDS);
    EXPECT_EQ(useconds, NOW_USECONDS);

    // Both staging files are renamed onto their targets, leaving nothing behind.
    EXPECT_EQ(Os::Test::fileSystem().files.count(BOOT_TEMP), 0u);
    EXPECT_EQ(Os::Test::fileSystem().files.count(QUIESCENCE_TEMP), 0u);
}

TEST_F(StartupManagerPersistenceTest, BootCountRoundTripsAndIncrementsAcrossRestarts) {
    RecordProperty("verifies", "REQ-SM-008");

    for (U64 expected = 1; expected <= 5; expected++) {
        Instance sm;
        EXPECT_EQ(sm->get_boot_count(true), expected) << "boot " << expected;
        EXPECT_EQ(sm.base().eventsBootCountUpdateFailure, 0u) << "boot " << expected;
        U64 stored = 0;
        EXPECT_EQ(decodeBootCount(stored), PR::Status::OK) << "boot " << expected;
        EXPECT_EQ(stored, expected) << "boot " << expected;
    }

    // A large value round-trips through the 8-byte little-endian payload, so
    // the format does not depend on the target's FwSizeType width.
    Os::Test::resetFileSystem();
    ASSERT_NO_FATAL_FAILURE(seedBootCount(0x0102030405060708ULL));
    Instance sm;
    EXPECT_EQ(sm->get_boot_count(false), static_cast<FwSizeType>(0x0102030405060708ULL));
    EXPECT_EQ(sm.base().eventsBootCountUpdateFailure, 0u);
}

TEST_F(StartupManagerPersistenceTest, QuiescenceStartRoundTripsAndIsNotRewritten) {
    RecordProperty("verifies", "REQ-SM-008");

    // First boot establishes the single quiescence start time for the mission.
    {
        Instance sm;
        setNow(sm, 500, 7);
        const Fw::Time first = sm->update_quiescence_start();
        EXPECT_EQ(first.getSeconds(), 500u);
        EXPECT_EQ(first.getUSeconds(), 7u);
        EXPECT_EQ(sm.base().eventsQuiescenceFileInitFailure, 0u);
    }
    const std::vector<U8> after_first = blobAt(QUIESCENCE_FILE);

    // A later boot reads it back unchanged and does not rewrite the file, even
    // though the clock has moved on.
    Instance sm;
    setNow(sm, 9999, 123);
    const Fw::Time second = sm->update_quiescence_start();
    EXPECT_EQ(second.getSeconds(), 500u) << "The stored start time wins over the current time";
    EXPECT_EQ(second.getUSeconds(), 7u);
    EXPECT_EQ(second.getTimeBase(), TimeBase::TB_WORKSTATION_TIME) << "The time base round-trips too";
    EXPECT_EQ(sm.base().eventsQuiescenceFileInitFailure, 0u);
    EXPECT_EQ(blobAt(QUIESCENCE_FILE), after_first) << "A valid record is left untouched";
}

// ----------------------------------------------------------------------
// Validation failures fall back to the defined defaults
// ----------------------------------------------------------------------

TEST_F(StartupManagerPersistenceTest, EverySingleByteCorruptionOfBootCountWarnsOnceAndRestartsAtOne) {
    RecordProperty("verifies", "REQ-SM-008");

    for (size_t offset = 0; offset < BOOT_RECORD_SIZE; offset++) {
        for (uint32_t byte = 0; byte < 256u; byte++) {
            Os::Test::resetFileSystem();
            ASSERT_NO_FATAL_FAILURE(seedBootCount(0x1122334455667788ULL));
            std::vector<U8>& blob = blobAt(BOOT_FILE);
            if (blob[offset] == static_cast<U8>(byte)) {
                continue;  // not a corruption
            }
            blob[offset] = static_cast<U8>(byte);

            Instance sm;
            const FwSizeType count = sm->get_boot_count(true);

            const std::string where = "offset " + std::to_string(offset) + " byte " + std::to_string(byte);
            EXPECT_EQ(count, 1u) << where;
            EXPECT_EQ(sm.base().eventsBootCountUpdateFailure, 1u) << where;
            U64 stored = 0;
            EXPECT_EQ(decodeBootCount(stored), PR::Status::OK) << where;
            EXPECT_EQ(stored, 1u) << where;
        }
    }
}

TEST_F(StartupManagerPersistenceTest, EveryTruncationOfBootCountWarnsOnceAndRestartsAtOne) {
    RecordProperty("verifies", "REQ-SM-008");

    for (size_t length = 0; length < BOOT_RECORD_SIZE; length++) {
        Os::Test::resetFileSystem();
        ASSERT_NO_FATAL_FAILURE(seedBootCount(0x1122334455667788ULL));
        blobAt(BOOT_FILE).resize(length);

        Instance sm;
        const FwSizeType count = sm->get_boot_count(true);

        const std::string where = "length " + std::to_string(length);
        EXPECT_EQ(count, 1u) << where;
        EXPECT_EQ(sm.base().eventsBootCountUpdateFailure, 1u) << where;
    }
}

TEST_F(StartupManagerPersistenceTest, EverySingleByteCorruptionOfQuiescenceWarnsOnceAndRestartsNow) {
    RecordProperty("verifies", "REQ-SM-008");

    for (size_t offset = 0; offset < QUIESCENCE_RECORD_SIZE; offset++) {
        for (uint32_t byte = 0; byte < 256u; byte++) {
            Os::Test::resetFileSystem();
            ASSERT_NO_FATAL_FAILURE(seedQuiescence(TimeBase::TB_WORKSTATION_TIME, 0, 4242, 999));
            std::vector<U8>& blob = blobAt(QUIESCENCE_FILE);
            if (blob[offset] == static_cast<U8>(byte)) {
                continue;  // not a corruption
            }
            blob[offset] = static_cast<U8>(byte);

            Instance sm;
            setNow(sm, NOW_SECONDS, NOW_USECONDS);
            const Fw::Time time = sm->update_quiescence_start();

            const std::string where = "offset " + std::to_string(offset) + " byte " + std::to_string(byte);
            EXPECT_EQ(time.getSeconds(), NOW_SECONDS) << where;
            EXPECT_EQ(time.getUSeconds(), NOW_USECONDS) << where;
            EXPECT_EQ(sm.base().eventsQuiescenceFileInitFailure, 1u) << where;
            U32 seconds = 0;
            U32 useconds = 0;
            EXPECT_EQ(decodeQuiescence(seconds, useconds), PR::Status::OK) << where;
            EXPECT_EQ(seconds, NOW_SECONDS) << where;
        }
    }
}

TEST_F(StartupManagerPersistenceTest, EveryTruncationOfQuiescenceWarnsOnceAndRestartsNow) {
    RecordProperty("verifies", "REQ-SM-008");

    for (size_t length = 0; length < QUIESCENCE_RECORD_SIZE; length++) {
        Os::Test::resetFileSystem();
        ASSERT_NO_FATAL_FAILURE(seedQuiescence(TimeBase::TB_WORKSTATION_TIME, 0, 4242, 999));
        blobAt(QUIESCENCE_FILE).resize(length);

        Instance sm;
        setNow(sm, NOW_SECONDS, NOW_USECONDS);
        const Fw::Time time = sm->update_quiescence_start();

        const std::string where = "length " + std::to_string(length);
        EXPECT_EQ(time.getSeconds(), NOW_SECONDS) << where;
        EXPECT_EQ(sm.base().eventsQuiescenceFileInitFailure, 1u) << where;
    }
}

TEST_F(StartupManagerPersistenceTest, UsecondsOutOfRangeInValidRecordIsCorrupt) {
    RecordProperty("verifies", "REQ-SM-008");

    // A CRC-valid record whose useconds field is outside the [0, 999999]
    // contract Fw::Time::set asserts on -- e.g. a partial flash write leaving
    // 0xFF bytes. Adopting it panics Fw::Time::add in a boot loop.
    ASSERT_NO_FATAL_FAILURE(seedQuiescence(TimeBase::TB_WORKSTATION_TIME, 0, 4242, 1000000));
    U32 seededSeconds = 0;
    U32 seededUseconds = 0;
    ASSERT_EQ(decodeQuiescence(seededSeconds, seededUseconds), PR::Status::OK) << "The framing itself is valid";
    ASSERT_EQ(seededUseconds, 1000000u);

    Instance sm;
    setNow(sm, NOW_SECONDS, NOW_USECONDS);
    const Fw::Time time = sm->update_quiescence_start();

    EXPECT_EQ(time.getSeconds(), NOW_SECONDS) << "Quiescence restarts from now";
    EXPECT_EQ(time.getUSeconds(), NOW_USECONDS);
    EXPECT_EQ(sm.base().eventsQuiescenceFileInitFailure, 1u);
    U32 seconds = 0;
    U32 useconds = 0;
    EXPECT_EQ(decodeQuiescence(seconds, useconds), PR::Status::OK);
    EXPECT_EQ(useconds, NOW_USECONDS) << "and the bad value is replaced on disk";
}

// ----------------------------------------------------------------------
// Atomic update and the read-only command path
// ----------------------------------------------------------------------

TEST_F(StartupManagerPersistenceTest, StoreFailureWarnsAndKeepsPreviousRecord) {
    RecordProperty("verifies", "REQ-SM-008");

    ASSERT_NO_FATAL_FAILURE(seedBootCount(5));
    ASSERT_NO_FATAL_FAILURE(seedQuiescence(TimeBase::TB_WORKSTATION_TIME, 0, 4242, 999));
    const std::vector<U8> bootBefore = blobAt(BOOT_FILE);
    const std::vector<U8> quiescenceBefore = blobAt(QUIESCENCE_FILE);

    // Rename over the target fails: the step the update protocol exists for.
    Os::Test::fileSystem().failRename = true;

    Instance sm;
    EXPECT_EQ(sm->get_boot_count(true), 6u) << "The in-RAM count still advances";
    EXPECT_EQ(sm.base().eventsBootCountUpdateFailure, 1u) << "The load succeeded; only the store failed";
    EXPECT_EQ(blobAt(BOOT_FILE), bootBefore) << "A failed update leaves the previous record byte-for-byte intact";

    // A valid quiescence record is not rewritten at all, so a store failure
    // cannot touch it.
    EXPECT_EQ(sm->update_quiescence_start().getSeconds(), 4242u);
    EXPECT_EQ(sm.base().eventsQuiescenceFileInitFailure, 0u);
    EXPECT_EQ(blobAt(QUIESCENCE_FILE), quiescenceBefore);
}

TEST_F(StartupManagerPersistenceTest, GetBootCountCommandReadsWithoutRewriting) {
    RecordProperty("verifies", "REQ-SM-008");

    ASSERT_NO_FATAL_FAILURE(seedBootCount(7));
    const std::vector<U8> before = blobAt(BOOT_FILE);

    // Any attempt to write would have to create the staging file, which is
    // rigged to fail here: a rewrite would show up as a warning event.
    Os::Test::fileSystem().failOpenCreate = true;

    Instance sm;
    sm.base().GET_BOOT_COUNT_cmdHandler(OPCODE, 3);

    ASSERT_EQ(sm.base().eventsCurrentBootCount.size(), 1u);
    EXPECT_EQ(sm.base().eventsCurrentBootCount[0], 7);
    EXPECT_EQ(sm.base().eventsBootCountUpdateFailure, 0u) << "GET_BOOT_COUNT reads without rewriting the file";
    ASSERT_EQ(sm.base().cmdResponses.size(), 1u);
    EXPECT_EQ(sm.base().cmdResponses[0].response, Fw::CmdResponse::OK);
    EXPECT_EQ(sm.base().cmdResponses[0].cmdSeq, 3u);
    EXPECT_EQ(blobAt(BOOT_FILE), before);
    EXPECT_EQ(Os::Test::fileSystem().files.count(BOOT_TEMP), 0u);
}

TEST_F(StartupManagerPersistenceTest, LegacyFilesWarnOnceEachAndApplyDefaults) {
    RecordProperty("verifies", "REQ-SM-008");

    // The files the previous image left behind: a bare big-endian FwSizeType
    // and a bare big-endian Fw::Time serialization, neither carrying a magic,
    // a version or a CRC. The 8-byte boot count is shorter than the record
    // overhead; the 11-byte time starts with {0x00, 0x02, 0x00, ...}, which
    // fails the magic check.
    const U8 legacyBootCount[8] = {0, 0, 0, 0, 0, 0, 0, 42};
    blobAt(BOOT_FILE).assign(legacyBootCount, legacyBootCount + sizeof(legacyBootCount));
    const U8 legacyTime[11] = {0x00, 0x02, 0x00, 0x00, 0x00, 0x10, 0x92, 0x00, 0x00, 0x00, 0x00};
    blobAt(QUIESCENCE_FILE).assign(legacyTime, legacyTime + sizeof(legacyTime));

    Instance sm;
    setNow(sm, NOW_SECONDS, NOW_USECONDS);
    sm.base().run_handler(0, 0);

    EXPECT_EQ(sm.base().eventsBootCountUpdateFailure, 1u);
    EXPECT_EQ(sm.base().eventsQuiescenceFileInitFailure, 1u);
    ASSERT_FALSE(sm.base().tlmBootCount.empty());
    EXPECT_EQ(sm.base().tlmBootCount.back(), 1u) << "The boot count restarts at 1";

    U64 count = 0;
    EXPECT_EQ(decodeBootCount(count), PR::Status::OK) << "and both files are rewritten in the new format";
    EXPECT_EQ(count, 1u);
    U32 seconds = 0;
    U32 useconds = 0;
    EXPECT_EQ(decodeQuiescence(seconds, useconds), PR::Status::OK);
    EXPECT_EQ(seconds, NOW_SECONDS) << "Quiescence restarts at the upgrade boot";
}

}  // namespace
