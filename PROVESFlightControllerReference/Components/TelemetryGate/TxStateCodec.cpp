// ======================================================================
// \title  TxStateCodec.cpp
// \brief  Pure-logic encode/decode for the persisted telemetry-transmission
//         enable/disable state. No F Prime includes (host-testable).
// ======================================================================

#include "PROVESFlightControllerReference/Components/TelemetryGate/TxStateCodec.hpp"

namespace Components {

namespace {
// 4-byte magic prefix: "TGS1" (Telemetry Gate State v1).
constexpr uint8_t MAGIC0 = 0x54;  // 'T'
constexpr uint8_t MAGIC1 = 0x47;  // 'G'
constexpr uint8_t MAGIC2 = 0x53;  // 'S'
constexpr uint8_t MAGIC3 = 0x31;  // '1'

//! Compute the integrity byte over the magic prefix and the state byte.
inline uint8_t integrityByte(uint8_t state) {
    return static_cast<uint8_t>(~(MAGIC0 ^ MAGIC1 ^ MAGIC2 ^ MAGIC3 ^ state));
}

inline bool isDefinedState(uint8_t state) {
    return state == TX_STATE_ENABLED || state == TX_STATE_DISABLED;
}
}  // namespace

uint32_t encodeTxState(uint8_t state, uint8_t* buf, uint32_t bufLen) {
    if (buf == nullptr || bufLen < TX_STATE_ENCODED_SIZE || !isDefinedState(state)) {
        return 0;
    }
    buf[0] = MAGIC0;
    buf[1] = MAGIC1;
    buf[2] = MAGIC2;
    buf[3] = MAGIC3;
    buf[4] = state;
    buf[5] = integrityByte(state);
    return TX_STATE_ENCODED_SIZE;
}

TxStateDecodeStatus decodeTxState(const uint8_t* buf, uint32_t bufLen, uint8_t* outState) {
    // Reject null / truncated / zero-length buffers first.
    if (buf == nullptr || bufLen != TX_STATE_ENCODED_SIZE) {
        return TxStateDecodeStatus::BAD_LENGTH;
    }
    if (buf[0] != MAGIC0 || buf[1] != MAGIC1 || buf[2] != MAGIC2 || buf[3] != MAGIC3) {
        return TxStateDecodeStatus::BAD_MAGIC;
    }
    const uint8_t state = buf[4];
    // Integrity is checked before state validity so that a single-byte flip in
    // the state field is caught here even if it lands on a defined value.
    if (buf[5] != integrityByte(state)) {
        return TxStateDecodeStatus::BAD_INTEGRITY;
    }
    if (!isDefinedState(state)) {
        return TxStateDecodeStatus::BAD_STATE;
    }
    if (outState != nullptr) {
        *outState = state;
    }
    return TxStateDecodeStatus::OK;
}

}  // namespace Components
