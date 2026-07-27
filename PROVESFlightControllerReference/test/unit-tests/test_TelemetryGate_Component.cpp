// ======================================================================
// \title  test_TelemetryGate_Component.cpp
// \brief  Component-level host tests for TelemetryGate.
//
// Compiles the real TelemetryGate.cpp against the stubs in support/
// (TelemetryGateComponentAc.hpp records outgoing effects; Os/File.hpp is an
// in-memory filesystem with fault injection) and drives the actual handlers.
// Covers what the pure-codec test (test_TelemetryGate_TxStateCodec.cpp)
// cannot: gating, lazy state load ordering, corrupt-file fail-operational
// default, write-failure semantics, and persistence across reboot.
// ======================================================================

#include <gtest/gtest.h>

#include "PROVESFlightControllerReference/Components/TelemetryGate/TelemetryGate.hpp"
#include "PROVESFlightControllerReference/Components/TelemetryGate/TxStateCodec.hpp"
#include "Os/File.hpp"

namespace {

using Components::TelemetryGate;
using Components::TelemetryGateComponentBase;
using Components::TelemetryTxState;

constexpr const char* STATE_FILE = "/tlm_tx_state.bin";
constexpr FwOpcodeType OPCODE = 0x42;

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

    //! Write a valid encoded state blob into the fake filesystem.
    static void seedStateFile(U8 stateByte) {
        U8 buffer[Components::TX_STATE_ENCODED_SIZE];
        ASSERT_EQ(Components::encodeTxState(stateByte, buffer, sizeof(buffer)), Components::TX_STATE_ENCODED_SIZE);
        Os::Test::fileSystem().files[STATE_FILE].assign(buffer, buffer + sizeof(buffer));
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
    RecordProperty("verifies", "TelemetryGate-1,TelemetryGate-4");
    TelemetryGate gate("gate");
    sendSetState(gate, TelemetryTxState::DISABLED, 5);

    ASSERT_EQ(gate.eventsTransmitStateSet.size(), 1u);
    EXPECT_EQ(gate.eventsTransmitStateSet[0], TelemetryTxState::DISABLED);
    ASSERT_EQ(gate.cmdResponses.size(), 1u);
    EXPECT_EQ(gate.cmdResponses[0].opCode, OPCODE);
    EXPECT_EQ(gate.cmdResponses[0].cmdSeq, 5u);
    EXPECT_EQ(gate.cmdResponses[0].response, Fw::CmdResponse::OK);
    // Persisted blob decodes back to DISABLED.
    const std::vector<U8>& blob = Os::Test::fileSystem().files[STATE_FILE];
    ASSERT_EQ(blob.size(), static_cast<size_t>(Components::TX_STATE_ENCODED_SIZE));
    U8 decoded = Components::TX_STATE_ENABLED;
    ASSERT_EQ(Components::decodeTxState(blob.data(), static_cast<uint32_t>(blob.size()), &decoded),
              Components::TxStateDecodeStatus::OK);
    EXPECT_EQ(decoded, Components::TX_STATE_DISABLED);
}

TEST_F(TelemetryGateComponentTest, CommandBeforeFirstTickIsNotClobberedByLazyLoad) {
    RecordProperty("verifies", "TelemetryGate-1");
    seedStateFile(Components::TX_STATE_DISABLED);
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
    seedStateFile(Components::TX_STATE_DISABLED);
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
    seedStateFile(Components::TX_STATE_DISABLED);
    Os::Test::fileSystem().files[STATE_FILE].resize(3);  // torn write

    TelemetryGate gate("gate");
    tick(gate);

    EXPECT_EQ(gate.runOutCalls.size(), 1u);
    EXPECT_EQ(gate.eventsStateFileCorrupt, 1u);
}

TEST_F(TelemetryGateComponentTest, CorruptFileEventIsEmittedOnlyOnce) {
    RecordProperty("verifies", "TelemetryGate-6");
    seedStateFile(Components::TX_STATE_DISABLED);
    Os::Test::fileSystem().files[STATE_FILE].resize(3);

    TelemetryGate gate("gate");
    tick(gate);
    tick(gate);
    tick(gate);

    // Lazy load happens once; later ticks must not re-read or re-log.
    EXPECT_EQ(gate.eventsStateFileCorrupt, 1u);
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

}  // namespace
