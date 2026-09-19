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
// Requirements verified:
//   REQ-SM-008  the quiescence start file is a PersistedRecord updated
//               atomically; a corrupt, truncated or out-of-range file warns
//               once and restarts quiescence from now.
//   REQ-SM-010  with DEFAULT_STARTUP_VALUE == 0 the hard-coded transmit
//               countdown never asserts enableTransmit.
//   REQ-SM-011  an implausible boot count (> MAX_PLAUSIBLE_BOOT_COUNT) or a
//               short file is not propagated: one BootCountCorrupted (none
//               for a short file) and the count restarts at 1.
//   REQ-SM-012  the boot count is persisted through <path>.tmp + rename, only
//               by the once-per-boot increment, and a failed increment is
//               retried on every run tick until it is stored.
//
// Oracle (TP-3): expected values come from the pass criteria and the record
// layouts the design fixes -- the boot count as a raw big-endian FwSizeType
// (upstream #470: F Prime serialization, sizeof(FwSizeType) bytes, plausible
// while <= 1,000,000), magic "SQS1" over the 11-byte time layout mirroring
// Fw::Time::SERIALIZED_SIZE (TimeBase U16, context U8, seconds U32, useconds
// U32; lib/fprime/default/config/FpConfig.fpp:79,92) -- plus the parameter
// defaults in StartupManager.fpp. Corruption coverage follows
// test_TelemetryGate_Component.cpp:239-276.
// ======================================================================

#include <gtest/gtest.h>

#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "Os/File.hpp"
#include "PROVESFlightControllerReference/Components/PersistedRecord/PersistedRecordCodec.hpp"
#include "PROVESFlightControllerReference/Components/StartupManager/HardCodedStartup.h"
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
constexpr U8 QUIESCENCE_MAGIC[4] = {'S', 'Q', 'S', '1'};
constexpr U16 QUIESCENCE_PAYLOAD_SIZE = 11;
constexpr uint32_t QUIESCENCE_RECORD_SIZE = PR::OVERHEAD + QUIESCENCE_PAYLOAD_SIZE;
constexpr U64 MAX_PLAUSIBLE_BOOT_COUNT = 1000000;

// The boot count file is a raw big-endian FwSizeType (StartupManager.cpp
// read<FwSizeType, sizeof(FwSizeType)>): 8 bytes on this host, 4 on the target.
constexpr size_t BOOT_FILE_SIZE = sizeof(FwSizeType);

// The record the previous image (Cycle A, magic "SBC1") left on disk; it is
// not migrated (see LegacyBootRecordIsImplausibleWarnsOnceAndRestartsAtOne).
constexpr U8 LEGACY_BOOT_MAGIC[4] = {'S', 'B', 'C', '1'};
constexpr U16 LEGACY_BOOT_PAYLOAD_SIZE = 8;

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

    //! Write the boot count file in upstream's raw big-endian format.
    static void seedBootCount(U64 count) {
        std::vector<U8> raw(BOOT_FILE_SIZE);
        for (size_t i = 0; i < BOOT_FILE_SIZE; i++) {
            raw[i] = static_cast<U8>((count >> (8 * (BOOT_FILE_SIZE - 1 - i))) & 0xFFU);
        }
        blobAt(BOOT_FILE) = raw;
    }

    //! Big-endian value of the first BOOT_FILE_SIZE bytes of blob, as the
    //! component's read<> decodes them (it reads exactly that many bytes).
    static U64 bigEndianPrefix(const std::vector<U8>& blob) {
        U64 value = 0;
        for (size_t i = 0; i < BOOT_FILE_SIZE && i < blob.size(); i++) {
            value = (value << 8) | blob[i];
        }
        return value;
    }

    //! Decode the boot count file: false when it is not exactly one raw count.
    static bool readBootCount(U64& count) {
        const std::vector<U8>& blob = blobAt(BOOT_FILE);
        count = bigEndianPrefix(blob);
        return blob.size() == BOOT_FILE_SIZE;
    }

    //! Write the Cycle A PersistedRecord ("SBC1", little-endian U64 payload)
    //! that a pre-sync image left behind.
    static void seedLegacyBootRecord(U64 count) {
        U8 payload[LEGACY_BOOT_PAYLOAD_SIZE];
        for (U8 i = 0; i < LEGACY_BOOT_PAYLOAD_SIZE; i++) {
            payload[i] = static_cast<U8>((count >> (8 * i)) & 0xFFU);
        }
        U8 buffer[PR::MAX_RECORD_SIZE];
        const uint32_t size = PR::encode(LEGACY_BOOT_MAGIC, payload, LEGACY_BOOT_PAYLOAD_SIZE, buffer, sizeof(buffer));
        ASSERT_EQ(size, PR::OVERHEAD + LEGACY_BOOT_PAYLOAD_SIZE);
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

TEST_F(StartupManagerPersistenceTest, FirstBootWithNoQuiescenceFileWritesNowWithoutEvents) {
    RecordProperty("verifies", "REQ-SM-008");

    Instance sm;
    setNow(sm, NOW_SECONDS, NOW_USECONDS);
    sm.base().run_handler(0, 0);

    EXPECT_EQ(sm.base().eventsQuiescenceFileInitFailure, 0u) << "A missing file is a silent first boot";

    U32 seconds = 0;
    U32 useconds = 0;
    EXPECT_EQ(decodeQuiescence(seconds, useconds), PR::Status::OK) << "Quiescence start is written as a record";
    EXPECT_EQ(seconds, NOW_SECONDS);
    EXPECT_EQ(useconds, NOW_USECONDS);
    ASSERT_FALSE(sm.base().tlmQuiescenceEndTime.empty());
    EXPECT_EQ(sm.base().tlmQuiescenceEndTime.back().get_seconds(), NOW_SECONDS + 45u * 60u);

    // The staging file is renamed onto its target, leaving nothing behind.
    EXPECT_EQ(Os::Test::fileSystem().files.count(QUIESCENCE_TEMP), 0u);
}

TEST_F(StartupManagerPersistenceTest, FirstBootWithNoBootFileCountsOneAndStoresItAtomically) {
    RecordProperty("verifies", "REQ-SM-012");

    Instance sm;
    setNow(sm, NOW_SECONDS, NOW_USECONDS);
    sm.base().run_handler(0, 0);

    EXPECT_EQ(sm.base().eventsBootCountUpdateFailure, 0u);
    EXPECT_TRUE(sm.base().eventsBootCountCorrupted.empty()) << "A missing file is a silent first boot";
    ASSERT_FALSE(sm.base().tlmBootCount.empty());
    EXPECT_EQ(sm.base().tlmBootCount.back(), 1u) << "The first counted boot is boot 1";

    U64 count = 0;
    EXPECT_TRUE(readBootCount(count)) << "The boot count file is exactly one raw FwSizeType";
    EXPECT_EQ(count, 1u);
    EXPECT_EQ(Os::Test::fileSystem().files.count(BOOT_TEMP), 0u) << "The staging file was renamed onto the target";
}

TEST_F(StartupManagerPersistenceTest, BootCountRoundTripsAndIncrementsAcrossRestarts) {
    RecordProperty("verifies", "REQ-SM-012");

    for (U64 expected = 1; expected <= 5; expected++) {
        Instance sm;
        EXPECT_EQ(sm->get_boot_count(true), expected) << "boot " << expected;
        EXPECT_EQ(sm.base().eventsBootCountUpdateFailure, 0u) << "boot " << expected;
        EXPECT_TRUE(sm.base().eventsBootCountCorrupted.empty()) << "boot " << expected;
        U64 stored = 0;
        EXPECT_TRUE(readBootCount(stored)) << "boot " << expected;
        EXPECT_EQ(stored, expected) << "boot " << expected;
        EXPECT_EQ(Os::Test::fileSystem().files.count(BOOT_TEMP), 0u) << "boot " << expected;
    }

    // The largest plausible value round-trips untouched.
    Os::Test::resetFileSystem();
    ASSERT_NO_FATAL_FAILURE(seedBootCount(MAX_PLAUSIBLE_BOOT_COUNT));
    Instance sm;
    EXPECT_EQ(sm->get_boot_count(false), static_cast<FwSizeType>(MAX_PLAUSIBLE_BOOT_COUNT));
    EXPECT_EQ(sm.base().eventsBootCountUpdateFailure, 0u);
    EXPECT_TRUE(sm.base().eventsBootCountCorrupted.empty());
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

TEST_F(StartupManagerPersistenceTest, EverySingleByteCorruptionOfBootCountIsCappedByPlausibility) {
    RecordProperty("verifies", "REQ-SM-011");

    // The raw format has no checksum, so a corruption is detected only when
    // the decoded value is implausible. Every single-byte corruption of a
    // stored count is enumerated; the oracle is the decoded value itself:
    // above MAX_PLAUSIBLE_BOOT_COUNT it must warn once and restart at 1,
    // otherwise it is a plausible count and is incremented like any other.
    constexpr U64 SEED = 123456;
    for (size_t offset = 0; offset < BOOT_FILE_SIZE; offset++) {
        for (uint32_t byte = 0; byte < 256u; byte++) {
            Os::Test::resetFileSystem();
            ASSERT_NO_FATAL_FAILURE(seedBootCount(SEED));
            std::vector<U8>& blob = blobAt(BOOT_FILE);
            if (blob[offset] == static_cast<U8>(byte)) {
                continue;  // not a corruption
            }
            blob[offset] = static_cast<U8>(byte);
            const U64 decoded = bigEndianPrefix(blob);

            Instance sm;
            const FwSizeType count = sm->get_boot_count(true);

            const std::string where = "offset " + std::to_string(offset) + " byte " + std::to_string(byte);
            U64 stored = 0;
            EXPECT_TRUE(readBootCount(stored)) << where;
            if (decoded > MAX_PLAUSIBLE_BOOT_COUNT) {
                EXPECT_EQ(count, 1u) << where;
                ASSERT_EQ(sm.base().eventsBootCountCorrupted.size(), 1u) << where;
                EXPECT_EQ(sm.base().eventsBootCountCorrupted[0], static_cast<I64>(decoded)) << where;
                EXPECT_EQ(stored, 1u) << where;
            } else {
                EXPECT_EQ(count, decoded + 1) << where;
                EXPECT_TRUE(sm.base().eventsBootCountCorrupted.empty()) << where;
                EXPECT_EQ(stored, decoded + 1) << where;
            }
            EXPECT_EQ(sm.base().eventsBootCountUpdateFailure, 0u) << where;
        }
    }
}

TEST_F(StartupManagerPersistenceTest, ImplausibleBootCountWarnsOnceWithRawValueAndRestartsAtOne) {
    RecordProperty("verifies", "REQ-SM-011");

    ASSERT_NO_FATAL_FAILURE(seedBootCount(MAX_PLAUSIBLE_BOOT_COUNT + 1));

    Instance sm;
    setNow(sm, NOW_SECONDS, NOW_USECONDS);
    sm.base().run_handler(0, 0);

    ASSERT_EQ(sm.base().eventsBootCountCorrupted.size(), 1u);
    EXPECT_EQ(sm.base().eventsBootCountCorrupted[0], static_cast<I64>(MAX_PLAUSIBLE_BOOT_COUNT + 1));
    ASSERT_FALSE(sm.base().tlmBootCount.empty());
    EXPECT_EQ(sm.base().tlmBootCount.back(), 1u) << "The count reported on the first tick is 1";
    U64 stored = 0;
    EXPECT_TRUE(readBootCount(stored));
    EXPECT_EQ(stored, 1u) << "The implausible value is replaced, not incremented";
}

TEST_F(StartupManagerPersistenceTest, EveryTruncationOfBootCountIsSilentAndRestartsAtOne) {
    RecordProperty("verifies", "REQ-SM-011");

    for (size_t length = 0; length < BOOT_FILE_SIZE; length++) {
        Os::Test::resetFileSystem();
        ASSERT_NO_FATAL_FAILURE(seedBootCount(777));
        blobAt(BOOT_FILE).resize(length);

        Instance sm;
        const FwSizeType count = sm->get_boot_count(true);

        const std::string where = "length " + std::to_string(length);
        EXPECT_EQ(count, 1u) << where;
        EXPECT_TRUE(sm.base().eventsBootCountCorrupted.empty()) << where << ": a short file is a failed read, no event";
        EXPECT_EQ(sm.base().eventsBootCountUpdateFailure, 0u) << where;
        U64 stored = 0;
        EXPECT_TRUE(readBootCount(stored)) << where;
        EXPECT_EQ(stored, 1u) << where;
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

TEST_F(StartupManagerPersistenceTest, QuiescenceStoreFailureCannotTouchAValidRecord) {
    RecordProperty("verifies", "REQ-SM-008");

    ASSERT_NO_FATAL_FAILURE(seedQuiescence(TimeBase::TB_WORKSTATION_TIME, 0, 4242, 999));
    const std::vector<U8> quiescenceBefore = blobAt(QUIESCENCE_FILE);

    // Rename over the target fails: the step the update protocol exists for.
    Os::Test::fileSystem().failRename = true;

    Instance sm;
    // A valid quiescence record is not rewritten at all, so a store failure
    // cannot touch it.
    EXPECT_EQ(sm->update_quiescence_start().getSeconds(), 4242u);
    EXPECT_EQ(sm.base().eventsQuiescenceFileInitFailure, 0u);
    EXPECT_EQ(blobAt(QUIESCENCE_FILE), quiescenceBefore);
}

TEST_F(StartupManagerPersistenceTest, BootCountRenameFailureWarnsOnceAndKeepsPreviousValue) {
    RecordProperty("verifies", "REQ-SM-012");

    ASSERT_NO_FATAL_FAILURE(seedBootCount(5));
    const std::vector<U8> bootBefore = blobAt(BOOT_FILE);

    // Rename over the target fails: the step the update protocol exists for.
    Os::Test::fileSystem().failRename = true;

    Instance sm;
    EXPECT_EQ(sm->get_boot_count(true), 6u) << "The in-RAM count still advances";
    EXPECT_EQ(sm.base().eventsBootCountUpdateFailure, 1u) << "The read succeeded; only the store failed";
    EXPECT_EQ(blobAt(BOOT_FILE), bootBefore) << "A failed update leaves the previous value byte-for-byte intact";
}

TEST_F(StartupManagerPersistenceTest, FailedIncrementIsRetriedEveryTickUntilStored) {
    RecordProperty("verifies", "REQ-SM-012");

    constexpr U64 INITIAL = 41;
    constexpr U32 FAILING_TICKS = 3;
    ASSERT_NO_FATAL_FAILURE(seedBootCount(INITIAL));
    const std::vector<U8> bootBefore = blobAt(BOOT_FILE);

    // The staging file cannot be created for the first N ticks.
    Os::Test::fileSystem().failOpenCreate = true;

    Instance sm;
    setNow(sm, NOW_SECONDS, NOW_USECONDS);
    for (U32 tick = 0; tick < FAILING_TICKS; tick++) {
        sm.base().run_handler(0, tick);
        const std::string where = "tick " + std::to_string(tick);
        EXPECT_EQ(sm.base().eventsBootCountUpdateFailure, 1u) << where << ": one warning per failure streak";
        EXPECT_EQ(blobAt(BOOT_FILE), bootBefore) << where << ": the target is untouched while the increment fails";
        EXPECT_EQ(Os::Test::fileSystem().files.count(BOOT_TEMP), 0u) << where << ": nothing was renamed over it";
        ASSERT_FALSE(sm.base().tlmBootCount.empty()) << where;
        EXPECT_EQ(sm.base().tlmBootCount.back(), INITIAL + 1) << where << ": the in-RAM count already advanced";
    }

    // Storage recovers: the next tick persists initial+1 without a new warning.
    Os::Test::fileSystem().failOpenCreate = false;
    sm.base().run_handler(0, FAILING_TICKS);

    EXPECT_EQ(sm.base().eventsBootCountUpdateFailure, 1u);
    U64 stored = 0;
    EXPECT_TRUE(readBootCount(stored));
    EXPECT_EQ(stored, INITIAL + 1) << "The retried increment reached the file";
    EXPECT_EQ(Os::Test::fileSystem().files.count(BOOT_TEMP), 0u);

    // A further tick does not rewrite the file.
    const std::vector<U8> bootAfter = blobAt(BOOT_FILE);
    Os::Test::fileSystem().failOpenCreate = true;
    sm.base().run_handler(0, FAILING_TICKS + 1);
    EXPECT_EQ(sm.base().eventsBootCountUpdateFailure, 1u);
    EXPECT_EQ(blobAt(BOOT_FILE), bootAfter);
}

TEST_F(StartupManagerPersistenceTest, GetBootCountCommandReadsWithoutRewriting) {
    RecordProperty("verifies", "REQ-SM-012");

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

TEST_F(StartupManagerPersistenceTest, LegacyQuiescenceFileWarnsOnceAndRestartsNow) {
    RecordProperty("verifies", "REQ-SM-008");

    // The file an image before Cycle A left behind: a bare big-endian
    // Fw::Time serialization carrying no magic, version or CRC. It starts with
    // {0x00, 0x02, 0x00, ...}, which fails the magic check.
    const U8 legacyTime[11] = {0x00, 0x02, 0x00, 0x00, 0x00, 0x10, 0x92, 0x00, 0x00, 0x00, 0x00};
    blobAt(QUIESCENCE_FILE).assign(legacyTime, legacyTime + sizeof(legacyTime));

    Instance sm;
    setNow(sm, NOW_SECONDS, NOW_USECONDS);
    sm.base().run_handler(0, 0);

    EXPECT_EQ(sm.base().eventsQuiescenceFileInitFailure, 1u);
    U32 seconds = 0;
    U32 useconds = 0;
    EXPECT_EQ(decodeQuiescence(seconds, useconds), PR::Status::OK) << "The file is rewritten in the record format";
    EXPECT_EQ(seconds, NOW_SECONDS) << "Quiescence restarts at the upgrade boot";
}

TEST_F(StartupManagerPersistenceTest, LegacyBootRecordIsImplausibleWarnsOnceAndRestartsAtOne) {
    RecordProperty("verifies", "REQ-SM-011");

    // The Cycle A image stored the boot count as a PersistedRecord (magic
    // "SBC1"). It is not migrated: read<> takes the first sizeof(FwSizeType)
    // bytes of it as a big-endian count, which is far above the plausibility
    // cap, so the first post-sync boot warns once and restarts at 1.
    ASSERT_NO_FATAL_FAILURE(seedLegacyBootRecord(42));
    const U64 raw = bigEndianPrefix(blobAt(BOOT_FILE));
    ASSERT_GT(raw, MAX_PLAUSIBLE_BOOT_COUNT);

    Instance sm;
    setNow(sm, NOW_SECONDS, NOW_USECONDS);
    sm.base().run_handler(0, 0);

    ASSERT_EQ(sm.base().eventsBootCountCorrupted.size(), 1u);
    EXPECT_EQ(sm.base().eventsBootCountCorrupted[0], static_cast<I64>(raw));
    EXPECT_EQ(sm.base().eventsBootCountUpdateFailure, 0u);
    ASSERT_FALSE(sm.base().tlmBootCount.empty());
    EXPECT_EQ(sm.base().tlmBootCount.back(), 1u) << "The boot count restarts at 1";
    U64 stored = 0;
    EXPECT_TRUE(readBootCount(stored)) << "and the file is rewritten in the raw format";
    EXPECT_EQ(stored, 1u);
}

// ----------------------------------------------------------------------
// Hard-coded transmit enable (compiled out in this fork)
// ----------------------------------------------------------------------

TEST_F(StartupManagerPersistenceTest, HardcodedTransmitCountdownNeverFiresWhenGateIsDisabled) {
    RecordProperty("verifies", "REQ-SM-010");
    static_assert(DEFAULT_STARTUP_VALUE == 0, "This fork builds with the hard-coded startup gate disabled");

    // More ticks than TRANSMIT_ENABLE_TICKS (2800) so a live countdown would
    // have expired well inside the loop.
    constexpr U32 TICKS = 3000;
    Instance sm;
    setNow(sm, NOW_SECONDS, NOW_USECONDS);
    for (U32 tick = 0; tick < TICKS; tick++) {
        sm.base().run_handler(0, tick);
    }

    EXPECT_EQ(sm.base().enableTransmitCalls, 0u) << "enableTransmit is never asserted from the countdown";
    EXPECT_EQ(sm.base().eventsHardcodedRadioEnable, 0u);
    EXPECT_EQ(sm.base().runSequenceCalls.size(), 1u) << "Only the startup sequence dispatch on the first tick";
}

}  // namespace
