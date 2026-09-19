// ======================================================================
// \title  test_ModeManager_StatePersistence.cpp
// \brief  Host unit tests for ModeManager persisted-state format and recovery.
//
// Level: Unit. Compiles the real ModeManager.cpp against the recorder stub in
// support/PROVESFlightControllerReference/Components/ModeManager/ plus the
// in-memory Os::File / Os::FileSystem; no F Prime or Zephyr code is linked
// (test/unit-tests/README.md).
//
// Requirements verified: MM0011 (the state is a PersistedRecord, updated
// atomically) and MM0012 (a state that fails validation boots SAFE with reason
// SYSTEM_FAULT and exactly one StatePersistenceFailure; only a missing file is
// a silent NORMAL first boot).
//
// Oracle (TP-3): expected values come from the MM0011 / MM0012 pass criteria
// and the record layout the design fixes — magic "MMS1", the 7-byte
// little-endian payload documented at ModeManager.hpp STATE_PAYLOAD_SIZE, and
// the PersistedRecord header/CRC framing in PersistedRecordCodec.hpp. The
// codec used to build and inspect files here is the shared one, which is
// itself what MM0011 requires; nothing is read back out of ModeManager's own
// decision logic to form an expectation.
//
// Corruption coverage follows test_TelemetryGate_Component.cpp:239-276: the
// file is reseeded pristine for every iteration, one byte is set to each of
// the 255 other values (and each shorter length is tried), and a fresh
// component is booted each time.
// ======================================================================

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Os/File.hpp"
#include "PROVESFlightControllerReference/Components/ModeManager/ModeManager.hpp"
#include "PROVESFlightControllerReference/Components/PersistedRecord/PersistedRecordCodec.hpp"

namespace {

using Components::ModeManager;
using Components::ModeManagerComponentBase;
using Components::SafeModeReason;
using Components::SystemMode;

namespace PR = Components::PersistedRecord;

constexpr const char* STATE_FILE = "/mode_state.bin";
constexpr const char* STATE_TEMP = "/mode_state.tmp";
constexpr FwOpcodeType OPCODE = 0x77;

// Must match the constants in ModeManager.cpp / ModeManager.hpp.
constexpr U8 STATE_MAGIC[4] = {'M', 'M', 'S', '1'};
constexpr U16 STATE_PAYLOAD_SIZE = 7;
constexpr uint32_t STATE_RECORD_SIZE = PR::OVERHEAD + STATE_PAYLOAD_SIZE;

// Payload byte offsets, from the layout documented on ModeManager.hpp.
constexpr size_t OFF_MODE = 0;
constexpr size_t OFF_COUNT = 1;
constexpr size_t OFF_REASON = 5;
constexpr size_t OFF_CLEAN = 6;

// Offset of the format-version byte inside the record (PersistedRecordCodec.hpp:9-15).
constexpr size_t OFF_VERSION = PR::MAGIC_SIZE;

//! Build a payload from explicit field values, so a test can express an
//! out-of-range field without going through the component.
std::vector<U8> makePayload(U8 mode, U32 count, U8 reason, U8 clean) {
    std::vector<U8> payload(STATE_PAYLOAD_SIZE, 0);
    payload[OFF_MODE] = mode;
    payload[OFF_COUNT] = static_cast<U8>(count & 0xFFU);
    payload[OFF_COUNT + 1] = static_cast<U8>((count >> 8) & 0xFFU);
    payload[OFF_COUNT + 2] = static_cast<U8>((count >> 16) & 0xFFU);
    payload[OFF_COUNT + 3] = static_cast<U8>((count >> 24) & 0xFFU);
    payload[OFF_REASON] = reason;
    payload[OFF_CLEAN] = clean;
    return payload;
}

class ModeManagerStatePersistenceTest : public ::testing::Test {
  protected:
    void SetUp() override { Os::Test::resetFileSystem(); }

    static SystemMode::T currentMode(ModeManager& component) {
        return static_cast<ModeManagerComponentBase&>(component).getMode_handler(0).value();
    }

    static SafeModeReason::T reportedReason(ModeManager& component) {
        ModeManagerComponentBase& base = static_cast<ModeManagerComponentBase&>(component);
        base.GET_SAFE_MODE_REASON_cmdHandler(OPCODE, 0);
        return base.eventsCurrentSafeModeReasonReading.back();
    }

    static void forceSafeModeCmd(ModeManager& component) {
        static_cast<ModeManagerComponentBase&>(component).FORCE_SAFE_MODE_cmdHandler(OPCODE, 0);
    }

    static void prepareForReboot(ModeManager& component) {
        static_cast<ModeManagerComponentBase&>(component).prepareForReboot_handler(0);
    }

    //! Leave a pristine, cleanly shut down record on the filesystem by running
    //! one full component life: a first boot (which writes the record with the
    //! clean flag clear) followed by an orderly shutdown (which sets it).
    static void seedPristineRecord() {
        ModeManager first("modeManager");
        first.init(0);
        first.restorePersistentState();
        prepareForReboot(first);
    }

    //! Overwrite the state file with a record built from an explicit payload,
    //! using the shared codec so the CRC is always correct.
    static void seedRecord(const std::vector<U8>& payload) {
        U8 buffer[PR::MAX_RECORD_SIZE];
        const uint32_t size =
            PR::encode(STATE_MAGIC, payload.data(), static_cast<U16>(payload.size()), buffer, sizeof(buffer));
        ASSERT_NE(size, 0u);
        Os::Test::fileSystem().files[STATE_FILE].assign(buffer, buffer + size);
    }

    static std::vector<U8>& stateBlob() { return Os::Test::fileSystem().files[STATE_FILE]; }

    //! Decode whatever currently sits at the state path with the shared codec.
    static PR::Status decodeStateFile(std::vector<U8>& payloadOut) {
        const std::vector<U8>& blob = Os::Test::fileSystem().files[STATE_FILE];
        payloadOut.assign(STATE_PAYLOAD_SIZE, 0);
        U16 len = 0;
        const PR::Status status = PR::decode(STATE_MAGIC, blob.data(), static_cast<uint32_t>(blob.size()),
                                             payloadOut.data(), STATE_PAYLOAD_SIZE, len);
        payloadOut.resize(len);
        return status;
    }

    //! Assert the MM0012 contract for a state file that fails validation.
    static void expectCorruptBootsSafe(ModeManager& component, const std::string& where) {
        EXPECT_EQ(currentMode(component), SystemMode::SAFE_MODE) << where;
        EXPECT_EQ(reportedReason(component), SafeModeReason::SYSTEM_FAULT) << where;
        ASSERT_EQ(component.eventsStatePersistenceFailure.size(), 1u) << where;
        EXPECT_EQ(component.eventsStatePersistenceFailure[0].op, "load-corrupt") << where;
        // The rewritten record is valid again, so the next boot is clean.
        std::vector<U8> payload;
        EXPECT_EQ(decodeStateFile(payload), PR::Status::OK) << where;
    }
};

// ----------------------------------------------------------------------
// MM0011 -- record format and atomic update
// ----------------------------------------------------------------------

TEST_F(ModeManagerStatePersistenceTest, StateFileDecodesWithSharedCodecAndDocumentedLayout) {
    RecordProperty("verifies", "MM0011");

    ModeManager mm("modeManager");
    mm.init(0);
    mm.restorePersistentState();
    forceSafeModeCmd(mm);

    const std::vector<U8>& blob = stateBlob();
    EXPECT_EQ(blob.size(), STATE_RECORD_SIZE) << "Record is the fixed overhead plus the 7-byte payload";
    EXPECT_EQ(blob[0], 'M');
    EXPECT_EQ(blob[1], 'M');
    EXPECT_EQ(blob[2], 'S');
    EXPECT_EQ(blob[3], '1');
    EXPECT_EQ(blob[OFF_VERSION], PR::FORMAT_VERSION);

    std::vector<U8> payload;
    ASSERT_EQ(decodeStateFile(payload), PR::Status::OK) << "The state file decodes with the shared codec";
    ASSERT_EQ(payload.size(), STATE_PAYLOAD_SIZE);
    EXPECT_EQ(payload[OFF_MODE], static_cast<U8>(SystemMode::SAFE_MODE));
    EXPECT_EQ(payload[OFF_COUNT], 1u) << "One safe-mode entry, little-endian low byte first";
    EXPECT_EQ(payload[OFF_COUNT + 1], 0u);
    EXPECT_EQ(payload[OFF_COUNT + 2], 0u);
    EXPECT_EQ(payload[OFF_COUNT + 3], 0u);
    EXPECT_EQ(payload[OFF_REASON], static_cast<U8>(SafeModeReason::GROUND_COMMAND));
    EXPECT_EQ(payload[OFF_CLEAN], 0u) << "Only prepareForReboot sets the clean-shutdown flag";
}

TEST_F(ModeManagerStatePersistenceTest, PrepareForRebootStoresCleanFlagAndLeavesNoTemp) {
    RecordProperty("verifies", "MM0011");

    ModeManager mm("modeManager");
    mm.init(0);
    mm.restorePersistentState();
    forceSafeModeCmd(mm);
    prepareForReboot(mm);

    EXPECT_EQ(mm.eventsPreparingForReboot, 1u);
    EXPECT_TRUE(mm.eventsStatePersistenceFailure.empty());

    std::vector<U8> payload;
    ASSERT_EQ(decodeStateFile(payload), PR::Status::OK);
    ASSERT_EQ(payload.size(), STATE_PAYLOAD_SIZE);
    EXPECT_EQ(payload[OFF_CLEAN], 1u);
    EXPECT_EQ(payload[OFF_MODE], static_cast<U8>(SystemMode::SAFE_MODE)) << "Mode survives the shutdown write";
    // The staging file is renamed onto the target, so nothing is left behind.
    EXPECT_EQ(Os::Test::fileSystem().files.count(STATE_TEMP), 0u);
}

TEST_F(ModeManagerStatePersistenceTest, ModeChangeStoreFailureKeepsPreviousRecordAndEmitsOneEvent) {
    RecordProperty("verifies", "MM0011");

    seedPristineRecord();
    const std::vector<U8> before = stateBlob();

    // The staging write succeeds but the rename over the target fails, which is
    // the step PersistedRecord's update protocol exists to make survivable.
    Os::Test::fileSystem().failRename = true;

    ModeManager mm("modeManager");
    forceSafeModeCmd(mm);

    ASSERT_EQ(mm.eventsStatePersistenceFailure.size(), 1u);
    EXPECT_EQ(mm.eventsStatePersistenceFailure[0].op, "save-store");
    EXPECT_EQ(stateBlob(), before) << "A failed update leaves the previous record byte-for-byte intact";
    std::vector<U8> payload;
    EXPECT_EQ(decodeStateFile(payload), PR::Status::OK) << "and still valid";
    // The in-RAM transition still stands: persistence failure never blocks the
    // protective mode change.
    EXPECT_EQ(currentMode(mm), SystemMode::SAFE_MODE);
}

// ----------------------------------------------------------------------
// MM0012 -- validation failures boot SAFE, a missing file does not
// ----------------------------------------------------------------------

TEST_F(ModeManagerStatePersistenceTest, NoFileBootsNormalWithZeroEvents) {
    RecordProperty("verifies", "MM0012");

    ASSERT_EQ(Os::Test::fileSystem().files.count(STATE_FILE), 0u);

    ModeManager mm("modeManager");
    mm.init(0);
    mm.restorePersistentState();

    EXPECT_EQ(currentMode(mm), SystemMode::NORMAL) << "A first boot on a fresh filesystem is NORMAL";
    EXPECT_EQ(reportedReason(mm), SafeModeReason::NONE);
    EXPECT_TRUE(mm.eventsStatePersistenceFailure.empty())
        << "A missing file is not a corruption and must not warn (issue #1)";
    EXPECT_TRUE(mm.eventsEnteringSafeMode.empty());
    EXPECT_EQ(mm.loadSwitchTurnOnCalls.size(), 8u) << "NORMAL re-asserts the load switches on";

    // The boot arms unintended-reboot detection by writing a clear clean flag.
    std::vector<U8> payload;
    ASSERT_EQ(decodeStateFile(payload), PR::Status::OK);
    ASSERT_EQ(payload.size(), STATE_PAYLOAD_SIZE);
    EXPECT_EQ(payload[OFF_MODE], static_cast<U8>(SystemMode::NORMAL));
    EXPECT_EQ(payload[OFF_CLEAN], 0u);
}

TEST_F(ModeManagerStatePersistenceTest, EmptyFileBootsSafeWithOneEvent) {
    RecordProperty("verifies", "MM0012");

    // A zero-length file is present, so it is a failed validation rather than a
    // first boot: shorter than the record header can possibly be.
    Os::Test::fileSystem().files[STATE_FILE].clear();

    ModeManager mm("modeManager");
    mm.init(0);
    mm.restorePersistentState();

    expectCorruptBootsSafe(mm, "empty file");
    EXPECT_EQ(mm.loadSwitchTurnOffCalls.size(), 8u) << "Safe mode entry turns the non-critical switches off";
}

TEST_F(ModeManagerStatePersistenceTest, EverySingleByteCorruptionBootsSafeWithSystemFaultAndOneEvent) {
    RecordProperty("verifies", "MM0012");

    for (size_t offset = 0; offset < STATE_RECORD_SIZE; offset++) {
        for (uint32_t value = 0; value < 256u; value++) {
            Os::Test::resetFileSystem();
            seedPristineRecord();
            std::vector<U8>& blob = stateBlob();
            ASSERT_EQ(blob.size(), STATE_RECORD_SIZE);
            if (blob[offset] == static_cast<U8>(value)) {
                continue;  // not a corruption
            }
            blob[offset] = static_cast<U8>(value);

            ModeManager mm("modeManager");
            mm.init(0);
            mm.restorePersistentState();

            const std::string where = "offset " + std::to_string(offset) + " value " + std::to_string(value);
            ASSERT_NO_FATAL_FAILURE(expectCorruptBootsSafe(mm, where));
        }
    }
}

TEST_F(ModeManagerStatePersistenceTest, EveryTruncationLengthBootsSafeWithSystemFaultAndOneEvent) {
    RecordProperty("verifies", "MM0012");

    for (size_t length = 0; length < STATE_RECORD_SIZE; length++) {
        Os::Test::resetFileSystem();
        seedPristineRecord();
        stateBlob().resize(length);

        ModeManager mm("modeManager");
        mm.init(0);
        mm.restorePersistentState();

        ASSERT_NO_FATAL_FAILURE(expectCorruptBootsSafe(mm, "length " + std::to_string(length)));
    }
}

TEST_F(ModeManagerStatePersistenceTest, WrongMagicBootsSafe) {
    RecordProperty("verifies", "MM0012");

    // A record whose framing and CRC are perfect but which belongs to another
    // consumer must never be adopted as mode state.
    const U8 otherMagic[4] = {'T', 'G', 'S', '2'};
    const std::vector<U8> payload = makePayload(static_cast<U8>(SystemMode::NORMAL), 3, 0, 1);
    U8 buffer[PR::MAX_RECORD_SIZE];
    const uint32_t size = PR::encode(otherMagic, payload.data(), STATE_PAYLOAD_SIZE, buffer, sizeof(buffer));
    ASSERT_EQ(size, STATE_RECORD_SIZE);
    Os::Test::fileSystem().files[STATE_FILE].assign(buffer, buffer + size);

    ModeManager mm("modeManager");
    mm.init(0);
    mm.restorePersistentState();

    expectCorruptBootsSafe(mm, "wrong magic");
}

TEST_F(ModeManagerStatePersistenceTest, WrongVersionWithValidCrcBootsSafe) {
    RecordProperty("verifies", "MM0012");

    // Patch the version byte and recompute the CRC, so the record is intact in
    // every respect except the format version it declares.
    seedRecord(makePayload(static_cast<U8>(SystemMode::NORMAL), 7, 0, 1));
    std::vector<U8>& blob = stateBlob();
    ASSERT_EQ(blob.size(), STATE_RECORD_SIZE);
    blob[OFF_VERSION] = static_cast<U8>(PR::FORMAT_VERSION + 1);
    const uint32_t crc = PR::crc32(blob.data(), STATE_RECORD_SIZE - PR::CRC_SIZE);
    for (uint32_t i = 0; i < PR::CRC_SIZE; i++) {
        blob[STATE_RECORD_SIZE - PR::CRC_SIZE + i] = static_cast<U8>((crc >> (8 * i)) & 0xFFU);
    }

    ModeManager mm("modeManager");
    mm.init(0);
    mm.restorePersistentState();

    expectCorruptBootsSafe(mm, "wrong version");
}

TEST_F(ModeManagerStatePersistenceTest, OutOfRangeFieldsInCrcValidRecordBootSafe) {
    RecordProperty("verifies", "MM0012");

    struct Case {
        const char* name;
        U8 mode;
        U8 reason;
        U8 clean;
    };
    // Mode outside {SAFE_MODE, NORMAL}, a SafeModeReason past COMMAND_LOSS (=6), and a
    // clean-shutdown flag that is neither 0 nor 1.
    const Case cases[] = {{"mode 0", 0, 0, 0},   {"mode 3", 3, 0, 0},       {"mode 255", 255, 0, 0},
                          {"reason 7", 2, 7, 0}, {"reason 255", 2, 255, 0}, {"clean 2", 2, 0, 2}};

    for (const Case& c : cases) {
        Os::Test::resetFileSystem();
        seedRecord(makePayload(c.mode, 11, c.reason, c.clean));
        // The record itself is well formed; only the field values are illegal.
        std::vector<U8> decoded;
        ASSERT_EQ(decodeStateFile(decoded), PR::Status::OK) << c.name;

        ModeManager mm("modeManager");
        mm.init(0);
        mm.restorePersistentState();

        ASSERT_NO_FATAL_FAILURE(expectCorruptBootsSafe(mm, c.name));
    }
}

TEST_F(ModeManagerStatePersistenceTest, ShortPayloadInCrcValidRecordBootsSafe) {
    RecordProperty("verifies", "MM0012");

    // A CRC-valid record that declares a shorter payload than the layout
    // requires decodes cleanly but cannot supply every field.
    seedRecord(std::vector<U8>(STATE_PAYLOAD_SIZE - 1, 0));
    std::vector<U8> decoded;
    ASSERT_EQ(decodeStateFile(decoded), PR::Status::OK);
    ASSERT_EQ(decoded.size(), static_cast<size_t>(STATE_PAYLOAD_SIZE - 1));

    ModeManager mm("modeManager");
    mm.init(0);
    mm.restorePersistentState();

    expectCorruptBootsSafe(mm, "short payload");
}

TEST_F(ModeManagerStatePersistenceTest, LegacyRawStructFileBootsSafeWithOneEvent) {
    RecordProperty("verifies", "MM0012");

    // The file the previous image left behind: sizeof(PersistentState) = 12
    // bytes of raw struct with alignment padding (U8 mode, 3 pad, U32 count,
    // U8 reason, U8 clean, 2 pad) and no magic, version or CRC. It is long
    // enough to reach the header check, where the first four bytes
    // {mode, pad, pad, pad} fail the magic comparison.
    const U8 legacy[12] = {static_cast<U8>(SystemMode::NORMAL),          0, 0, 0, 0x2A, 0, 0, 0,
                           static_cast<U8>(SafeModeReason::LOW_BATTERY), 1, 0, 0};
    Os::Test::fileSystem().files[STATE_FILE].assign(legacy, legacy + sizeof(legacy));

    ModeManager mm("modeManager");
    mm.init(0);
    mm.restorePersistentState();

    expectCorruptBootsSafe(mm, "legacy 12-byte struct");
    // The upgrade boot loses the old entry count (telemetry only) and restarts
    // it at 1 with the safe-mode entry this fault causes.
    std::vector<U8> payload;
    ASSERT_EQ(decodeStateFile(payload), PR::Status::OK);
    ASSERT_EQ(payload.size(), STATE_PAYLOAD_SIZE);
    EXPECT_EQ(payload[OFF_COUNT], 1u);
    EXPECT_EQ(payload[OFF_REASON], static_cast<U8>(SafeModeReason::SYSTEM_FAULT));
}

}  // namespace
