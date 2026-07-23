// ======================================================================
// \title  TxStateCodec.hpp
// \brief  Pure-logic encode/decode for the persisted telemetry-transmission
//         enable/disable state.
//
// This header intentionally contains NO F Prime / Svc / Zephyr includes so it
// can be linked into host (gtest) unit tests. It uses only <cstdint>.
// ======================================================================

#ifndef Components_TelemetryGate_TxStateCodec_HPP
#define Components_TelemetryGate_TxStateCodec_HPP

#include <cstdint>

namespace Components {

// Encoded telemetry-transmission-state blob layout (TX_STATE_ENCODED_SIZE bytes):
//   [0..3] magic "TGS1"
//   [4]    state byte (TX_STATE_ENABLED / TX_STATE_DISABLED)
//   [5]    integrity byte = ~(magic0 ^ magic1 ^ magic2 ^ magic3 ^ state)
// A single-byte flip in any position, as well as truncation or a zero-length
// buffer, is detectable by decodeTxState.
static constexpr uint32_t TX_STATE_ENCODED_SIZE = 6;

// State byte values. These MUST match the ordinals of the FPP TelemetryTxState
// enum (ENABLED = 0, DISABLED = 1).
static constexpr uint8_t TX_STATE_ENABLED = 0;
static constexpr uint8_t TX_STATE_DISABLED = 1;

//! Result of a decode attempt.
enum class TxStateDecodeStatus : uint8_t {
    OK,             //!< Valid blob; outState populated.
    BAD_LENGTH,     //!< Buffer null or not exactly TX_STATE_ENCODED_SIZE bytes (truncated/empty).
    BAD_MAGIC,      //!< Magic prefix mismatch.
    BAD_INTEGRITY,  //!< Integrity byte mismatch (catches single-byte corruption).
    BAD_STATE       //!< State byte is not a defined value.
};

//! \brief Encode a state value into buf.
//! \param state  state byte (must be TX_STATE_ENABLED or TX_STATE_DISABLED)
//! \param buf    destination buffer
//! \param bufLen size of destination buffer
//! \return number of bytes written (TX_STATE_ENCODED_SIZE) on success, 0 on
//!         invalid arguments (null buffer, buffer too small, or undefined state).
uint32_t encodeTxState(uint8_t state, uint8_t* buf, uint32_t bufLen);

//! \brief Decode a blob from buf.
//! \param buf      source buffer
//! \param bufLen   number of valid bytes in buf
//! \param outState receives the decoded state byte on OK (may be null to only validate)
//! \return TxStateDecodeStatus::OK when the blob is valid, otherwise the failure reason.
TxStateDecodeStatus decodeTxState(const uint8_t* buf, uint32_t bufLen, uint8_t* outState);

}  // namespace Components

#endif
