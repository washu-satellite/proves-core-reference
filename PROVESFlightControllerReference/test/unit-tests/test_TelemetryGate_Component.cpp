// ======================================================================
// \title  test_TelemetryGate_Component.cpp
// \brief  Component-level host tests for TelemetryGate.
//
// Compiles the real TelemetryGate.cpp against the stubs in support/
// (TelemetryGateComponentAc.hpp records outgoing effects; Os/File.hpp and
// Os/FileSystem.hpp are an in-memory filesystem with fault injection) and
// drives the actual handlers. Covers what the shared codec test
// (test_PersistedRecord.cpp) cannot: gating, lazy state load ordering,
// corrupt-file fail-operational default, write-failure semantics, and
// persistence across reboot.
// ======================================================================

#include <gtest/gtest.h>

#include "Os/File.hpp"
#include "PROVESFlightControllerReference/Components/PersistedRecord/PersistedRecordCodec.hpp"
#include "PROVESFlightControllerReference/Components/TelemetryGate/TelemetryGate.hpp"

namespace {

using Components::TelemetryGate;
using Components::TelemetryGateComponentBase;
using Components::TelemetryTxState;

namespace PR = Components::PersistedRecord;

constexpr const char* STATE_FILE = "/tlm_tx_state.bin";
constexpr const char* STATE_TEMP = "/tlm_tx_state.tmp";
constexpr FwOpcodeType OPCODE = 0x42;

// Must match the anonymous-namespace constants in TelemetryGate.cpp.
constexpr U8 STATE_MAGIC[4] = {'T', 'G', 'S', '2'};
constexpr U8 STATE_ENABLED = 0;
constexpr U8 STATE_DISABLED = 1;
constexpr size_t STATE_RECORD_SIZE = PR::OVERHEAD + 1;

class TelemetryGateComponentTest : public ::testing::Test {
  protected:
    void SetUp() override { Os::Test::resetFileSystem(); }

    //! Drive a scheduler tick through the component's real runIn handler.
    static void tick(TelemetryGate& gate, U32 context = 0) {
        static_cast<TelemetryGateComponentBase&>(gate).runIn_handler(0, context);
    }

    //! Send SET_TRANSMIT_STATE through the component's real command handler.
    static void sendSetState(TelemetryGate& gate, TelemetryTxState::T state, U32 cmdSeq = 0) {
        static_cast<TelemetryGateComponentBase&>(gate).SET_TRANSMIT_STATE_cmdHandler(OPCODE, cmdSeq, state);
    }

    //! Write a valid encoded state record into the fake filesystem.
    static void seedStateFile(U8 stateByte) {
        U8 buffer[PR::MAX_RECORD_SIZE];
        const uint32_t size = PR::encode(STATE_MAGIC, &stateByte, 1, buffer, sizeof(buffer));
        ASSERT_EQ(size, static_cast<uint32_t>(STATE_RECORD_SIZE));
        Os::Test::fileSystem().files[STATE_FILE].assign(buffer, buffer + size);
    }

    //! Decode whatever currently sits at STATE_FILE.
    static PR::Status decodeStateFile(U8& stateByte) {
        const std::vector<U8>& blob = Os::Test::fileSystem().files[STATE_FILE];
        uint16_t len = 0;
        return PR::decode(STATE_MAGIC, blob.data(), static_cast<uint32_t>(blob.size()), &stateByte, 1, len);
    }
};

// ----------------------------------------------------------------------
// Gating
// ----------------------------------------------------------------------

TEST_F(TelemetryGateComponentTest, EnabledForwardsEveryTick) {
    RecordProperty("verifies", "TelemetryGate-3");
    TelemetryGate gate("gate");
    tick(gate, 7);
    tick(gate, 8);

    ASSERT_EQ(gate.runOutCalls.size(), 2u);
    EXPECT_EQ(gate.runOutCalls[0], 7u);
    EXPECT_EQ(gate.runOutCalls[1], 8u);
    ASSERT_FALSE(gate.tlmGatedTicks.empty());
    EXPECT_EQ(gate.tlmGatedTicks.back(), 0u);
    ASSERT_FALSE(gate.tlmTransmitState.empty());
    EXPECT_EQ(gate.tlmTransmitState.back(), TelemetryTxState::ENABLED);
}

TEST_F(TelemetryGateComponentTest, DisabledDropsTicksAndCountsThem) {
    RecordProperty("verifies", "TelemetryGate-2,TelemetryGate-8");
    TelemetryGate gate("gate");
    sendSetState(gate, TelemetryTxState::DISABLED);
    tick(gate);
    tick(gate);
    tick(gate);

    EXPECT_TRUE(gate.runOutCalls.empty());
    EXPECT_EQ(gate.tlmGatedTicks.back(), 3u);
    EXPECT_EQ(gate.tlmTransmitState.back(), TelemetryTxState::DISABLED);
}

TEST_F(TelemetryGateComponentTest, ReEnableResumesForwardingAndStopsCounting) {
    RecordProperty("verifies", "TelemetryGate-1,TelemetryGate-3");
    TelemetryGate gate("gate");
    sendSetState(gate, TelemetryTxState::DISABLED);
    tick(gate);
    sendSetState(gate, TelemetryTxState::ENABLED);
    tick(gate, 99);
    tick(gate, 100);

    ASSERT_EQ(gate.runOutCalls.size(), 2u);
    EXPECT_EQ(gate.runOutCalls[0], 99u);
    EXPECT_EQ(gate.tlmGatedTicks.back(), 1u);  // count freezes; does not reset
}

// ----------------------------------------------------------------------
// Command semantics
// ----------------------------------------------------------------------

TEST_F(TelemetryGateComponentTest, SetStateEmitsEventAndRespondsOk) {
    RecordProperty("verifies", "TelemetryGate-1,TelemetryGate-4,TelemetryGate-9");
    TelemetryGate gate("gate");
    sendSetState(gate, TelemetryTxState::DISABLED, 5);

    ASSERT_EQ(gate.eventsTransmitStateSet.size(), 1u);
    EXPECT_EQ(gate.eventsTransmitStateSet[0], TelemetryTxState::DISABLED);
    ASSERT_EQ(gate.cmdResponses.size(), 1u);
    EXPECT_EQ(gate.cmdResponses[0].opCode, OPCODE);
    EXPECT_EQ(gate.cmdResponses[0].cmdSeq, 5u);
    EXPECT_EQ(gate.cmdResponses[0].response, Fw::CmdResponse::OK);
    // Persisted record decodes back to DISABLED.
    ASSERT_EQ(Os::Test::fileSystem().files[STATE_FILE].size(), STATE_RECORD_SIZE);
    U8 decoded = STATE_ENABLED;
    ASSERT_EQ(decodeStateFile(decoded), PR::Status::OK);
    EXPECT_EQ(decoded, STATE_DISABLED);
}

TEST_F(TelemetryGateComponentTest, CommandBeforeFirstTickIsNotClobberedByLazyLoad) {
    RecordProperty("verifies", "TelemetryGate-1");
    seedStateFile(STATE_DISABLED);
    TelemetryGate gate("gate");
    // Operator enables before the first scheduler tick ever fires.
    sendSetState(gate, TelemetryTxState::ENABLED);
    tick(gate, 1);

    // The persisted DISABLED must not override the fresher command.
    ASSERT_EQ(gate.runOutCalls.size(), 1u);
    EXPECT_EQ(gate.tlmTransmitState.back(), TelemetryTxState::ENABLED);
}

// ----------------------------------------------------------------------
// Persistence and boot behavior
// ----------------------------------------------------------------------

TEST_F(TelemetryGateComponentTest, FirstBootWithNoFileDefaultsEnabledWithoutCorruptEvent) {
    RecordProperty("verifies", "TelemetryGate-5");
    TelemetryGate gate("gate");
    tick(gate);

    EXPECT_EQ(gate.runOutCalls.size(), 1u);
    EXPECT_EQ(gate.eventsStateFileCorrupt, 0u);
}

TEST_F(TelemetryGateComponentTest, DisabledStatePersistsAcrossReboot) {
    RecordProperty("verifies", "TelemetryGate-4");
    {
        TelemetryGate gate("boot1");
        sendSetState(gate, TelemetryTxState::DISABLED);
    }
    // "Reboot": fresh component instance, same filesystem contents.
    TelemetryGate rebooted("boot2");
    tick(rebooted);

    EXPECT_TRUE(rebooted.runOutCalls.empty());
    EXPECT_EQ(rebooted.eventsStateFileCorrupt, 0u);
    EXPECT_EQ(rebooted.tlmTransmitState.back(), TelemetryTxState::DISABLED);
}

TEST_F(TelemetryGateComponentTest, CorruptStateFileDefaultsEnabledAndEmitsEvent) {
    RecordProperty("verifies", "TelemetryGate-6");
    seedStateFile(STATE_DISABLED);
    Os::Test::fileSystem().files[STATE_FILE][4] ^= 0xFF;  // flip the state byte

    TelemetryGate gate("gate");
    tick(gate);

    // Fail-operational: telemetry keeps flowing despite the DISABLED-ish blob.
    EXPECT_EQ(gate.runOutCalls.size(), 1u);
    EXPECT_EQ(gate.eventsStateFileCorrupt, 1u);
    EXPECT_EQ(gate.tlmTransmitState.back(), TelemetryTxState::ENABLED);
}

TEST_F(TelemetryGateComponentTest, TruncatedStateFileDefaultsEnabledAndEmitsEvent) {
    RecordProperty("verifies", "TelemetryGate-6");
    seedStateFile(STATE_DISABLED);
    Os::Test::fileSystem().files[STATE_FILE].resize(3);  // torn write

    TelemetryGate gate("gate");
    tick(gate);

    EXPECT_EQ(gate.runOutCalls.size(), 1u);
    EXPECT_EQ(gate.eventsStateFileCorrupt, 1u);
}

TEST_F(TelemetryGateComponentTest, CorruptFileEventIsEmittedOnlyOnce) {
    RecordProperty("verifies", "TelemetryGate-6");
    seedStateFile(STATE_DISABLED);
    Os::Test::fileSystem().files[STATE_FILE].resize(3);

    TelemetryGate gate("gate");
    tick(gate);
    tick(gate);
    tick(gate);

    // Lazy load happens once; later ticks must not re-read or re-log.
    EXPECT_EQ(gate.eventsStateFileCorrupt, 1u);
}

// ----------------------------------------------------------------------
// PersistedRecord migration (TelemetryGate-9)
// ----------------------------------------------------------------------

TEST_F(TelemetryGateComponentTest, LegacyTgs1FileDefaultsEnabledWithOneCorruptEvent) {
    RecordProperty("verifies", "TelemetryGate-6,TelemetryGate-9");
    // A file written by the previous image: 6-byte "TGS1" blob holding DISABLED
    // (magic, state, XOR integrity byte). Shorter than the PersistedRecord
    // overhead, so it is rejected rather than migrated.
    const U8 legacy[] = {0x54, 0x47, 0x53, 0x31, 0x01, 0x8F};
    Os::Test::fileSystem().files[STATE_FILE].assign(legacy, legacy + sizeof(legacy));

    TelemetryGate gate("gate");
    tick(gate);

    // Fail-operational: the commanded DISABLED is lost, telemetry keeps flowing,
    // and the ground sees exactly one ungated corruption warning.
    EXPECT_EQ(gate.runOutCalls.size(), 1u);
    EXPECT_EQ(gate.eventsStateFileCorrupt, 1u);
    EXPECT_EQ(gate.tlmTransmitState.back(), TelemetryTxState::ENABLED);
}

TEST_F(TelemetryGateComponentTest, EverySingleByteCorruptionDefaultsEnabledWithOneEvent) {
    RecordProperty("verifies", "TelemetryGate-6,TelemetryGate-9");
    for (size_t offset = 0; offset < STATE_RECORD_SIZE; offset++) {
        for (uint32_t value = 0; value < 256u; value++) {
            Os::Test::resetFileSystem();
            seedStateFile(STATE_DISABLED);
            std::vector<U8>& blob = Os::Test::fileSystem().files[STATE_FILE];
            if (blob[offset] == static_cast<U8>(value)) {
                continue;  // not a corruption
            }
            blob[offset] = static_cast<U8>(value);

            TelemetryGate gate("gate");
            tick(gate);

            EXPECT_EQ(gate.runOutCalls.size(), 1u) << "offset " << offset << " value " << value;
            EXPECT_EQ(gate.eventsStateFileCorrupt, 1u) << "offset " << offset << " value " << value;
            EXPECT_EQ(gate.tlmTransmitState.back(), TelemetryTxState::ENABLED)
                << "offset " << offset << " value " << value;
        }
    }
}

TEST_F(TelemetryGateComponentTest, EveryTruncationLengthDefaultsEnabledWithOneEvent) {
    RecordProperty("verifies", "TelemetryGate-6,TelemetryGate-9");
    for (size_t length = 0; length < STATE_RECORD_SIZE; length++) {
        Os::Test::resetFileSystem();
        seedStateFile(STATE_DISABLED);
        Os::Test::fileSystem().files[STATE_FILE].resize(length);

        TelemetryGate gate("gate");
        tick(gate);

        EXPECT_EQ(gate.runOutCalls.size(), 1u) << "length " << length;
        EXPECT_EQ(gate.eventsStateFileCorrupt, 1u) << "length " << length;
        EXPECT_EQ(gate.tlmTransmitState.back(), TelemetryTxState::ENABLED) << "length " << length;
    }
}

TEST_F(TelemetryGateComponentTest, SuccessfulPersistLeavesNoTempFile) {
    RecordProperty("verifies", "TelemetryGate-4,TelemetryGate-9");
    TelemetryGate gate("gate");
    sendSetState(gate, TelemetryTxState::DISABLED);

    ASSERT_EQ(gate.cmdResponses.size(), 1u);
    EXPECT_EQ(gate.cmdResponses[0].response, Fw::CmdResponse::OK);
    // The staging file is renamed onto the target, so nothing is left behind.
    EXPECT_EQ(Os::Test::fileSystem().files.count(STATE_TEMP), 0u);
    U8 decoded = STATE_ENABLED;
    ASSERT_EQ(decodeStateFile(decoded), PR::Status::OK);
    EXPECT_EQ(decoded, STATE_DISABLED);
}

// ----------------------------------------------------------------------
// Write-failure semantics
// ----------------------------------------------------------------------

TEST_F(TelemetryGateComponentTest, WriteFailureReportsErrorButStateChangeStands) {
    RecordProperty("verifies", "TelemetryGate-7");
    Os::Test::fileSystem().failWrite = true;

    TelemetryGate gate("gate");
    sendSetState(gate, TelemetryTxState::DISABLED);

    EXPECT_EQ(gate.eventsStateFileWriteFailure, 1u);
    ASSERT_EQ(gate.cmdResponses.size(), 1u);
    EXPECT_EQ(gate.cmdResponses[0].response, Fw::CmdResponse::EXECUTION_ERROR);
    // Documented fail-operational intent: the in-RAM change still takes effect.
    ASSERT_EQ(gate.eventsTransmitStateSet.size(), 1u);
    tick(gate);
    EXPECT_TRUE(gate.runOutCalls.empty());
}

TEST_F(TelemetryGateComponentTest, OpenFailureOnPersistReportsError) {
    RecordProperty("verifies", "TelemetryGate-7");
    Os::Test::fileSystem().failOpenCreate = true;

    TelemetryGate gate("gate");
    sendSetState(gate, TelemetryTxState::DISABLED);

    EXPECT_EQ(gate.eventsStateFileWriteFailure, 1u);
    ASSERT_EQ(gate.cmdResponses.size(), 1u);
    EXPECT_EQ(gate.cmdResponses[0].response, Fw::CmdResponse::EXECUTION_ERROR);
}

TEST_F(TelemetryGateComponentTest, PartialWriteReportsError) {
    RecordProperty("verifies", "TelemetryGate-7");
    Os::Test::fileSystem().partialWrite = true;

    TelemetryGate gate("gate");
    sendSetState(gate, TelemetryTxState::DISABLED);

    EXPECT_EQ(gate.eventsStateFileWriteFailure, 1u);
    ASSERT_EQ(gate.cmdResponses.size(), 1u);
    EXPECT_EQ(gate.cmdResponses[0].response, Fw::CmdResponse::EXECUTION_ERROR);
}

TEST_F(TelemetryGateComponentTest, SyncFailureReportsErrorButStateChangeStands) {
    RecordProperty("verifies", "TelemetryGate-7,TelemetryGate-9");
    // A flush failure used to be invisible: write(WAIT) discards the flush
    // status. The store now checks it, so an unsynced record is reported.
    Os::Test::fileSystem().failFlush = true;

    TelemetryGate gate("gate");
    sendSetState(gate, TelemetryTxState::DISABLED);

    EXPECT_EQ(gate.eventsStateFileWriteFailure, 1u);
    ASSERT_EQ(gate.cmdResponses.size(), 1u);
    EXPECT_EQ(gate.cmdResponses[0].response, Fw::CmdResponse::EXECUTION_ERROR);
    // The target was never opened, so no half-written record is left there.
    EXPECT_EQ(Os::Test::fileSystem().files.count(STATE_FILE), 0u);
    // Fail-operational: the in-RAM change still takes effect.
    ASSERT_EQ(gate.eventsTransmitStateSet.size(), 1u);
    tick(gate);
    EXPECT_TRUE(gate.runOutCalls.empty());
}

TEST_F(TelemetryGateComponentTest, RenameFailureReportsErrorAndPreviousFileSurvives) {
    RecordProperty("verifies", "TelemetryGate-7,TelemetryGate-9");
    seedStateFile(STATE_ENABLED);
    Os::Test::fileSystem().failRename = true;

    TelemetryGate gate("gate");
    sendSetState(gate, TelemetryTxState::DISABLED);

    EXPECT_EQ(gate.eventsStateFileWriteFailure, 1u);
    ASSERT_EQ(gate.cmdResponses.size(), 1u);
    EXPECT_EQ(gate.cmdResponses[0].response, Fw::CmdResponse::EXECUTION_ERROR);
    // The previously persisted record is intact and still decodes.
    U8 decoded = STATE_DISABLED;
    ASSERT_EQ(decodeStateFile(decoded), PR::Status::OK);
    EXPECT_EQ(decoded, STATE_ENABLED);
    // Fail-operational: the in-RAM change still takes effect.
    tick(gate);
    EXPECT_TRUE(gate.runOutCalls.empty());
}

}  // namespace
