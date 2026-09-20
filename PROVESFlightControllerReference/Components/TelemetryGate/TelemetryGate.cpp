// ======================================================================
// \title  TelemetryGate.cpp
// \brief  cpp file for TelemetryGate component implementation class
// ======================================================================

#include "PROVESFlightControllerReference/Components/TelemetryGate/TelemetryGate.hpp"

#include "Os/File.hpp"
#include "PROVESFlightControllerReference/Components/PersistedRecord/PersistedRecordFile.hpp"

namespace Components {

namespace {
// Flash path for the persisted telemetry-transmission state. Single leading
// slash matches the project convention (e.g. "/prmDb.dat", "/quiescence_start.bin").
constexpr const char* STATE_FILE_PATH = "/tlm_tx_state.bin";
// Staging file for the atomic replace: written and flushed in full, then
// renamed over STATE_FILE_PATH, so the target never holds a partial record.
constexpr const char* STATE_TEMP_PATH = "/tlm_tx_state.tmp";

// Record-type magic: "TGS2" (Telemetry Gate State, PersistedRecord format).
// Distinct from the retired bespoke "TGS1" blob, so a legacy file left by an
// older image fails the length check and is reported as corrupt rather than
// misread.
constexpr U8 TX_STATE_MAGIC[4] = {'T', 'G', 'S', '2'};

// Payload byte values. These MUST match the ordinals of the FPP
// TelemetryTxState enum (ENABLED = 0, DISABLED = 1).
constexpr U8 TX_STATE_ENABLED = 0;
constexpr U8 TX_STATE_DISABLED = 1;

// The persisted payload is a single state byte.
constexpr uint16_t TX_STATE_PAYLOAD_SIZE = 1;
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
                                                   const Components::TelemetryTxState& txState) {
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
    U8 payload = TX_STATE_ENABLED;
    uint16_t payloadLen = 0;

    const PersistedRecord::Status status = PersistedRecord::load(STATE_FILE_PATH, STATE_TEMP_PATH, TX_STATE_MAGIC,
                                                                 &payload, TX_STATE_PAYLOAD_SIZE, payloadLen);

    if (status == PersistedRecord::Status::MISSING || status == PersistedRecord::Status::OPEN_ERROR) {
        // Missing file: fail-operational default ENABLED. Not treated as corrupt.
        // An unopenable path that stat reports as absent lands here too, which
        // is how a first boot presents on Zephyr (open collapses to OTHER_ERROR).
        this->m_state = TelemetryTxState::ENABLED;
        return;
    }

    // Anything else -- bad magic, bad length, bad CRC, unknown version,
    // truncation, a read failure, or a valid record carrying a state byte that
    // is not a defined ordinal -- is corruption.
    if (status != PersistedRecord::Status::OK || payloadLen != TX_STATE_PAYLOAD_SIZE ||
        (payload != TX_STATE_ENABLED && payload != TX_STATE_DISABLED)) {
        this->log_WARNING_HI_StateFileCorrupt();
        this->m_state = TelemetryTxState::ENABLED;
        return;
    }

    this->m_state = (payload == TX_STATE_DISABLED) ? TelemetryTxState::DISABLED : TelemetryTxState::ENABLED;
}

bool TelemetryGate ::persistState(Components::TelemetryTxState state) {
    const U8 stateByte = (state == TelemetryTxState::DISABLED) ? TX_STATE_DISABLED : TX_STATE_ENABLED;

    // Atomic replace: a failure at any step leaves the previously persisted
    // record intact, and is reported to the caller as a write failure.
    return PersistedRecord::store(STATE_FILE_PATH, STATE_TEMP_PATH, TX_STATE_MAGIC, &stateByte,
                                  TX_STATE_PAYLOAD_SIZE) == PersistedRecord::Status::OK;
}

}  // namespace Components
