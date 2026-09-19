// ======================================================================
// \title  DriverBoardMessages.hpp
// \brief  POD structs and pack/unpack for every driver-board message type.
//
// Header-only, F Prime free (<cstddef>, <cstdint> and DriverBoardProtocol.hpp
// only). One struct per TYPE in the spec's two message tables (docs/sdd.md,
// "Messages, host -> board" / "Messages, board -> host"), with:
//
//   constexpr uint8_t X_LEN                    payload length on the wire
//   Type    messageType(const X&)              the TYPE byte
//   uint8_t pack(const X&, uint8_t* payload)   writes X_LEN little-endian bytes
//   bool    unpack(const Frame&, X&)           false unless type and len match
//   size_t  encodeMessage(msg, seq, out, cap)  pack + encode in one call
//
// Integers are written and read one byte at a time (never memcpy of a struct),
// so the layout is the same on any host endianness and padding.
//
// Errata against 02-protocol.md: the HK field list sums to 23 bytes, not the
// 22 the table's trailing note says; HK_LEN is 23 (see docs/sdd.md).
// ======================================================================

#ifndef Components_DriverBoardProtocol_DriverBoardMessages_HPP
#define Components_DriverBoardProtocol_DriverBoardMessages_HPP

#include <cstddef>
#include <cstdint>

#include "PROVESFlightControllerReference/Components/DriverBoardProtocol/DriverBoardProtocol.hpp"

namespace Components {
namespace DriverBoardProtocol {

// ---- Protocol constants -----------------------------------------------

constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr uint8_t ARM_MAGIC = 0xA5;
constexpr uint16_t PULSE_DURATION_MIN_MS = 50;
constexpr uint16_t PULSE_DURATION_MAX_MS = 5000;
constexpr uint8_t PULSE_DUTY_MAX_PCT = 100;
constexpr uint8_t CHANNEL_MASK_BITS = 0x07;  //!< bits 0-2
constexpr uint8_t STREAM_RATES_HZ[4] = {5, 10, 20, 50};

//! ACK.status
enum class AckStatus : uint8_t {
    OK = 0,
    REFUSED_NOT_ARMED = 1,
    REFUSED_FAULT = 2,
    BAD_ARG = 3,
    BUSY = 4,
};

//! HK.state
enum class BoardState : uint8_t {
    UNPOWERED = 0,  //!< never sent
    DISARMED = 1,
    ARMED = 2,
    PULSING = 3,
    FAULT = 4,
};

//! faultFlags bits (HK.faultFlags, FAULT.faultFlags)
constexpr uint8_t FAULT_OVERCURRENT = 0x01;
constexpr uint8_t FAULT_OVERTEMP = 0x02;
constexpr uint8_t FAULT_HOST_TIMEOUT = 0x04;
constexpr uint8_t FAULT_UNDERVOLTAGE = 0x08;
constexpr uint8_t FAULT_SENSE_FAIL = 0x10;

//! True for every TYPE in the spec tables; unknown types are the caller's to count and drop.
inline bool isKnownType(uint8_t type) {
    switch (static_cast<Type>(type)) {
        case Type::HEARTBEAT:
        case Type::HK_REQUEST:
        case Type::ARM:
        case Type::DISARM:
        case Type::PULSE:
        case Type::ABORT:
        case Type::PING:
        case Type::TIME_SYNC:
        case Type::STREAM_START:
        case Type::STREAM_STOP:
        case Type::ACK:
        case Type::HK:
        case Type::PONG:
        case Type::SAMPLE:
        case Type::FAULT:
            return true;
        default:
            return false;
    }
}

// ---- Little-endian byte helpers ---------------------------------------

namespace Wire {

inline void putU8(uint8_t* p, uint8_t v) {
    p[0] = v;
}
inline void putI8(uint8_t* p, int8_t v) {
    p[0] = static_cast<uint8_t>(v);
}
inline void putU16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
}
inline void putI16(uint8_t* p, int16_t v) {
    putU16(p, static_cast<uint16_t>(v));
}
inline void putU32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}
inline uint8_t getU8(const uint8_t* p) {
    return p[0];
}
inline int8_t getI8(const uint8_t* p) {
    return static_cast<int8_t>(p[0]);
}
inline uint16_t getU16(const uint8_t* p) {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}
inline int16_t getI16(const uint8_t* p) {
    return static_cast<int16_t>(getU16(p));
}
inline uint32_t getU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace Wire

// ---- host -> board ----------------------------------------------------

//! 0x01 HEARTBEAT: resets the board's failsafe timer; no response.
struct Heartbeat {
    uint32_t hostTickMs;
};
constexpr uint8_t HEARTBEAT_LEN = 4;
inline Type messageType(const Heartbeat&) {
    return Type::HEARTBEAT;
}
inline uint8_t pack(const Heartbeat& m, uint8_t* p) {
    Wire::putU32(p, m.hostTickMs);
    return HEARTBEAT_LEN;
}
inline bool unpack(const Frame& f, Heartbeat& m) {
    if (f.type != static_cast<uint8_t>(Type::HEARTBEAT) || f.len != HEARTBEAT_LEN) {
        return false;
    }
    m.hostTickMs = Wire::getU32(f.payload);
    return true;
}

//! 0x02 HK_REQUEST: board answers HK.
struct HkRequest {};
constexpr uint8_t HK_REQUEST_LEN = 0;
inline Type messageType(const HkRequest&) {
    return Type::HK_REQUEST;
}
inline uint8_t pack(const HkRequest&, uint8_t*) {
    return HK_REQUEST_LEN;
}
inline bool unpack(const Frame& f, HkRequest&) {
    return f.type == static_cast<uint8_t>(Type::HK_REQUEST) && f.len == HK_REQUEST_LEN;
}

//! 0x03 ARM: magic must be ARM_MAGIC; board answers ACK(0x03, status).
struct Arm {
    uint8_t magic;
};
constexpr uint8_t ARM_LEN = 1;
inline Type messageType(const Arm&) {
    return Type::ARM;
}
inline uint8_t pack(const Arm& m, uint8_t* p) {
    Wire::putU8(p, m.magic);
    return ARM_LEN;
}
inline bool unpack(const Frame& f, Arm& m) {
    if (f.type != static_cast<uint8_t>(Type::ARM) || f.len != ARM_LEN) {
        return false;
    }
    m.magic = Wire::getU8(f.payload);
    return true;
}

//! 0x04 DISARM: board answers ACK(0x04, OK).
struct Disarm {};
constexpr uint8_t DISARM_LEN = 0;
inline Type messageType(const Disarm&) {
    return Type::DISARM;
}
inline uint8_t pack(const Disarm&, uint8_t*) {
    return DISARM_LEN;
}
inline bool unpack(const Frame& f, Disarm&) {
    return f.type == static_cast<uint8_t>(Type::DISARM) && f.len == DISARM_LEN;
}

//! 0x05 PULSE: board answers ACK(0x05, status) immediately; HK after completion.
struct Pulse {
    uint16_t durationMs;   //!< 50..5000
    uint8_t dutyPct;       //!< 0..100
    uint8_t channelMask;   //!< bits 0-2
    uint8_t polarityMask;  //!< bits 0-2; 1 = reversed
};
constexpr uint8_t PULSE_LEN = 5;
inline Type messageType(const Pulse&) {
    return Type::PULSE;
}
inline uint8_t pack(const Pulse& m, uint8_t* p) {
    Wire::putU16(p, m.durationMs);
    Wire::putU8(p + 2, m.dutyPct);
    Wire::putU8(p + 3, m.channelMask);
    Wire::putU8(p + 4, m.polarityMask);
    return PULSE_LEN;
}
inline bool unpack(const Frame& f, Pulse& m) {
    if (f.type != static_cast<uint8_t>(Type::PULSE) || f.len != PULSE_LEN) {
        return false;
    }
    m.durationMs = Wire::getU16(f.payload);
    m.dutyPct = Wire::getU8(f.payload + 2);
    m.channelMask = Wire::getU8(f.payload + 3);
    m.polarityMask = Wire::getU8(f.payload + 4);
    return true;
}

//! 0x06 ABORT: board answers ACK(0x06, OK); coils off within 1 ms.
struct Abort {};
constexpr uint8_t ABORT_LEN = 0;
inline Type messageType(const Abort&) {
    return Type::ABORT;
}
inline uint8_t pack(const Abort&, uint8_t*) {
    return ABORT_LEN;
}
inline bool unpack(const Frame& f, Abort&) {
    return f.type == static_cast<uint8_t>(Type::ABORT) && f.len == ABORT_LEN;
}

//! 0x07 PING: board answers PONG.
struct Ping {};
constexpr uint8_t PING_LEN = 0;
inline Type messageType(const Ping&) {
    return Type::PING;
}
inline uint8_t pack(const Ping&, uint8_t*) {
    return PING_LEN;
}
inline bool unpack(const Frame& f, Ping&) {
    return f.type == static_cast<uint8_t>(Type::PING) && f.len == PING_LEN;
}

//! 0x08 TIME_SYNC: board answers ACK and records the offset (A9).
struct TimeSync {
    uint32_t hostSeconds;
    uint32_t hostMicroseconds;
};
constexpr uint8_t TIME_SYNC_LEN = 8;
inline Type messageType(const TimeSync&) {
    return Type::TIME_SYNC;
}
inline uint8_t pack(const TimeSync& m, uint8_t* p) {
    Wire::putU32(p, m.hostSeconds);
    Wire::putU32(p + 4, m.hostMicroseconds);
    return TIME_SYNC_LEN;
}
inline bool unpack(const Frame& f, TimeSync& m) {
    if (f.type != static_cast<uint8_t>(Type::TIME_SYNC) || f.len != TIME_SYNC_LEN) {
        return false;
    }
    m.hostSeconds = Wire::getU32(f.payload);
    m.hostMicroseconds = Wire::getU32(f.payload + 4);
    return true;
}

//! 0x09 STREAM_START: rateHz in STREAM_RATES_HZ; board answers ACK then SAMPLE frames (A9).
struct StreamStart {
    uint8_t rateHz;
};
constexpr uint8_t STREAM_START_LEN = 1;
inline Type messageType(const StreamStart&) {
    return Type::STREAM_START;
}
inline uint8_t pack(const StreamStart& m, uint8_t* p) {
    Wire::putU8(p, m.rateHz);
    return STREAM_START_LEN;
}
inline bool unpack(const Frame& f, StreamStart& m) {
    if (f.type != static_cast<uint8_t>(Type::STREAM_START) || f.len != STREAM_START_LEN) {
        return false;
    }
    m.rateHz = Wire::getU8(f.payload);
    return true;
}

//! 0x0A STREAM_STOP: board answers ACK.
struct StreamStop {};
constexpr uint8_t STREAM_STOP_LEN = 0;
inline Type messageType(const StreamStop&) {
    return Type::STREAM_STOP;
}
inline uint8_t pack(const StreamStop&, uint8_t*) {
    return STREAM_STOP_LEN;
}
inline bool unpack(const Frame& f, StreamStop&) {
    return f.type == static_cast<uint8_t>(Type::STREAM_STOP) && f.len == STREAM_STOP_LEN;
}

// ---- board -> host ----------------------------------------------------

//! 0x81 ACK
struct Ack {
    uint8_t ackedType;
    uint8_t status;  //!< AckStatus
};
constexpr uint8_t ACK_LEN = 2;
inline Type messageType(const Ack&) {
    return Type::ACK;
}
inline uint8_t pack(const Ack& m, uint8_t* p) {
    Wire::putU8(p, m.ackedType);
    Wire::putU8(p + 1, m.status);
    return ACK_LEN;
}
inline bool unpack(const Frame& f, Ack& m) {
    if (f.type != static_cast<uint8_t>(Type::ACK) || f.len != ACK_LEN) {
        return false;
    }
    m.ackedType = Wire::getU8(f.payload);
    m.status = Wire::getU8(f.payload + 1);
    return true;
}

//! 0x82 HK: 23 bytes (see errata in the file header).
struct Hk {
    int16_t currentMa[3];
    int16_t tempDeciC[2];
    int8_t dutyPct[3];  //!< signed percent; negative = reversed polarity
    uint8_t state;      //!< BoardState
    uint8_t faultFlags;
    uint32_t uptimeMs;
    uint32_t boardTickMs;
};
constexpr uint8_t HK_LEN = 23;
inline Type messageType(const Hk&) {
    return Type::HK;
}
inline uint8_t pack(const Hk& m, uint8_t* p) {
    Wire::putI16(p + 0, m.currentMa[0]);
    Wire::putI16(p + 2, m.currentMa[1]);
    Wire::putI16(p + 4, m.currentMa[2]);
    Wire::putI16(p + 6, m.tempDeciC[0]);
    Wire::putI16(p + 8, m.tempDeciC[1]);
    Wire::putI8(p + 10, m.dutyPct[0]);
    Wire::putI8(p + 11, m.dutyPct[1]);
    Wire::putI8(p + 12, m.dutyPct[2]);
    Wire::putU8(p + 13, m.state);
    Wire::putU8(p + 14, m.faultFlags);
    Wire::putU32(p + 15, m.uptimeMs);
    Wire::putU32(p + 19, m.boardTickMs);
    return HK_LEN;
}
inline bool unpack(const Frame& f, Hk& m) {
    if (f.type != static_cast<uint8_t>(Type::HK) || f.len != HK_LEN) {
        return false;
    }
    const uint8_t* p = f.payload;
    m.currentMa[0] = Wire::getI16(p + 0);
    m.currentMa[1] = Wire::getI16(p + 2);
    m.currentMa[2] = Wire::getI16(p + 4);
    m.tempDeciC[0] = Wire::getI16(p + 6);
    m.tempDeciC[1] = Wire::getI16(p + 8);
    m.dutyPct[0] = Wire::getI8(p + 10);
    m.dutyPct[1] = Wire::getI8(p + 11);
    m.dutyPct[2] = Wire::getI8(p + 12);
    m.state = Wire::getU8(p + 13);
    m.faultFlags = Wire::getU8(p + 14);
    m.uptimeMs = Wire::getU32(p + 15);
    m.boardTickMs = Wire::getU32(p + 19);
    return true;
}

//! 0x87 PONG
struct Pong {
    uint16_t firmwareVersion;
    uint8_t protocolVersion;  //!< = PROTOCOL_VERSION
    uint8_t reserved;
};
constexpr uint8_t PONG_LEN = 4;
inline Type messageType(const Pong&) {
    return Type::PONG;
}
inline uint8_t pack(const Pong& m, uint8_t* p) {
    Wire::putU16(p, m.firmwareVersion);
    Wire::putU8(p + 2, m.protocolVersion);
    Wire::putU8(p + 3, m.reserved);
    return PONG_LEN;
}
inline bool unpack(const Frame& f, Pong& m) {
    if (f.type != static_cast<uint8_t>(Type::PONG) || f.len != PONG_LEN) {
        return false;
    }
    m.firmwareVersion = Wire::getU16(f.payload);
    m.protocolVersion = Wire::getU8(f.payload + 2);
    m.reserved = Wire::getU8(f.payload + 3);
    return true;
}

//! 0x88 SAMPLE: 13 bytes (A9).
struct Sample {
    uint32_t tMs;  //!< board clock
    int16_t currentMa[3];
    int8_t dutyPct[3];
};
constexpr uint8_t SAMPLE_LEN = 13;
inline Type messageType(const Sample&) {
    return Type::SAMPLE;
}
inline uint8_t pack(const Sample& m, uint8_t* p) {
    Wire::putU32(p + 0, m.tMs);
    Wire::putI16(p + 4, m.currentMa[0]);
    Wire::putI16(p + 6, m.currentMa[1]);
    Wire::putI16(p + 8, m.currentMa[2]);
    Wire::putI8(p + 10, m.dutyPct[0]);
    Wire::putI8(p + 11, m.dutyPct[1]);
    Wire::putI8(p + 12, m.dutyPct[2]);
    return SAMPLE_LEN;
}
inline bool unpack(const Frame& f, Sample& m) {
    if (f.type != static_cast<uint8_t>(Type::SAMPLE) || f.len != SAMPLE_LEN) {
        return false;
    }
    const uint8_t* p = f.payload;
    m.tMs = Wire::getU32(p + 0);
    m.currentMa[0] = Wire::getI16(p + 4);
    m.currentMa[1] = Wire::getI16(p + 6);
    m.currentMa[2] = Wire::getI16(p + 8);
    m.dutyPct[0] = Wire::getI8(p + 10);
    m.dutyPct[1] = Wire::getI8(p + 11);
    m.dutyPct[2] = Wire::getI8(p + 12);
    return true;
}

//! 0x8F FAULT
struct Fault {
    uint8_t faultFlags;
    int16_t value;
};
constexpr uint8_t FAULT_LEN = 3;
inline Type messageType(const Fault&) {
    return Type::FAULT;
}
inline uint8_t pack(const Fault& m, uint8_t* p) {
    Wire::putU8(p, m.faultFlags);
    Wire::putI16(p + 1, m.value);
    return FAULT_LEN;
}
inline bool unpack(const Frame& f, Fault& m) {
    if (f.type != static_cast<uint8_t>(Type::FAULT) || f.len != FAULT_LEN) {
        return false;
    }
    m.faultFlags = Wire::getU8(f.payload);
    m.value = Wire::getI16(f.payload + 1);
    return true;
}

// ---- Framing convenience ------------------------------------------------

//! \brief Pack a message and frame it in one call.
//! \return the frame length, or 0 if out is too small (see encode)
template <class M>
inline size_t encodeMessage(const M& msg, uint8_t seq, uint8_t* out, size_t outCapacity) {
    uint8_t payload[MAX_PAYLOAD] = {0};
    const uint8_t len = pack(msg, payload);
    return encode(static_cast<uint8_t>(messageType(msg)), seq, payload, len, out, outCapacity);
}

// ---- Units and conversion (host side) ----------------------------------

//! current F32 A = currentMa / 1000
inline float currentAmps(int16_t currentMa) {
    return static_cast<float>(currentMa) / 1000.0f;
}
//! temperature F32 degC = tempDeciC / 10
inline float temperatureC(int16_t tempDeciC) {
    return static_cast<float>(tempDeciC) / 10.0f;
}
//! duty I16 (telemetry) = dutyPct as signed percent (negative = reversed polarity)
inline int16_t dutyPercent(int8_t dutyPct) {
    return static_cast<int16_t>(dutyPct);
}

}  // namespace DriverBoardProtocol
}  // namespace Components

#endif
