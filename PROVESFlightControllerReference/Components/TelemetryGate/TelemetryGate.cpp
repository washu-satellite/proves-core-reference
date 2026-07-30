// ======================================================================
// \title  TelemetryGate.cpp
// \brief  cpp file for TelemetryGate component implementation class
// ======================================================================

#include "PROVESFlightControllerReference/Components/TelemetryGate/TelemetryGate.hpp"

#include "Os/File.hpp"
#include "PROVESFlightControllerReference/Components/TelemetryGate/TxStateCodec.hpp"

namespace Components {

namespace {
// Flash path for the persisted telemetry-transmission state. Single leading
// slash matches the project convention (e.g. "/prmDb.dat", "/quiescence_start.bin").
constexpr const char* STATE_FILE_PATH = "/tlm_tx_state.bin";
}  // namespace

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

TelemetryGate ::TelemetryGate(const char* const compName)
    : TelemetryGateComponentBase(compName), m_state(TelemetryTxState::ENABLED), m_gated_ticks(0), m_loaded(false) {}

TelemetryGate ::~TelemetryGate() {}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

void TelemetryGate ::runIn_handler(FwIndexType portNum, U32 context) {
    // Lazily load persisted state on the first tick, when the filesystem is up.
    // The state is applied before the gating decision below, so the first gated
    // decision after boot already honors the persisted value.
    if (!this->m_loaded) {
        this->loadState();
        this->m_loaded = true;
    }

    if (this->m_state == TelemetryTxState::ENABLED) {
        // Forward the tick downstream to CdhCore.tlmSend.Run.
        this->runOut_out(0, context);
    } else {
        // Disabled: drop the tick (TlmChan does not run this cycle) and count it.
        this->m_gated_ticks++;
    }

    this->tlmWrite_TransmitState(this->m_state);
    this->tlmWrite_GatedTicks(this->m_gated_ticks);
}

// ----------------------------------------------------------------------
// Handler implementations for commands
// ----------------------------------------------------------------------

void TelemetryGate ::SET_TRANSMIT_STATE_cmdHandler(FwOpcodeType opCode,
                                                   U32 cmdSeq,
                                                   Components::TelemetryTxState txState) {
    // Latch the new state in RAM immediately so the very next scheduler tick
    // honors it (command is sync, so this takes effect within one cycle). Mark
    // as loaded so a not-yet-performed lazy load cannot overwrite it.
    this->m_state = txState;
    this->m_loaded = true;

    this->log_ACTIVITY_HI_TransmitStateSet(txState);
    this->tlmWrite_TransmitState(txState);

    // Persist for reboot survival. The in-RAM change already took effect, so a
    // write failure is reported but the state change still stands.
    if (!this->persistState(txState)) {
        this->log_WARNING_HI_StateFileWriteFailure();
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }

    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// ----------------------------------------------------------------------
// Private helper methods
// ----------------------------------------------------------------------

void TelemetryGate ::loadState() {
    Os::File file;
    U8 buffer[TX_STATE_ENCODED_SIZE];

    Os::File::Status status = file.open(STATE_FILE_PATH, Os::File::OPEN_READ);
    if (status != Os::File::OP_OK) {
        // Missing file: fail-operational default ENABLED. Not treated as corrupt.
        this->m_state = TelemetryTxState::ENABLED;
        (void)file.close();
        return;
    }

    FwSizeType size = sizeof(buffer);
    status = file.read(buffer, size, Os::File::WaitType::WAIT);
    (void)file.close();

    // Truncated or unreadable content is corruption.
    if (status != Os::File::OP_OK || size != static_cast<FwSizeType>(TX_STATE_ENCODED_SIZE)) {
        this->log_WARNING_HI_StateFileCorrupt();
        this->m_state = TelemetryTxState::ENABLED;
        return;
    }

    U8 decoded = TX_STATE_ENABLED;
    TxStateDecodeStatus decodeStatus = decodeTxState(buffer, static_cast<uint32_t>(size), &decoded);
    if (decodeStatus != TxStateDecodeStatus::OK) {
        this->log_WARNING_HI_StateFileCorrupt();
        this->m_state = TelemetryTxState::ENABLED;
        return;
    }

    this->m_state = (decoded == TX_STATE_DISABLED) ? TelemetryTxState::DISABLED : TelemetryTxState::ENABLED;
}

bool TelemetryGate ::persistState(Components::TelemetryTxState state) {
    U8 buffer[TX_STATE_ENCODED_SIZE];
    const U8 stateByte = (state == TelemetryTxState::DISABLED) ? TX_STATE_DISABLED : TX_STATE_ENABLED;

    if (encodeTxState(stateByte, buffer, sizeof(buffer)) != TX_STATE_ENCODED_SIZE) {
        return false;
    }

    Os::File file;
    Os::File::Status status = file.open(STATE_FILE_PATH, Os::File::OPEN_CREATE, Os::File::OVERWRITE);
    if (status != Os::File::OP_OK) {
        (void)file.close();
        return false;
    }

    FwSizeType size = sizeof(buffer);
    status = file.write(buffer, size, Os::File::WaitType::WAIT);
    (void)file.close();

    return status == Os::File::OP_OK && size == static_cast<FwSizeType>(TX_STATE_ENCODED_SIZE);
}

}  // namespace Components
