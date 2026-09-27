// ======================================================================
// \title  DataRecorderCfg.hpp
// \brief  Compile-time sizes and defaults of the DataRecorder
//         (cycle-m-plan 01-normative section 6).
//
// Per-stream tables are indexed by the RecorderStream ordinal (TLM = 0,
// EVT = 1). A9-2 appends BURST and makes NUM_STREAMS 3.
// ======================================================================

#ifndef Components_DataRecorderCfg_HPP
#define Components_DataRecorderCfg_HPP

#include <cstdint>

#include "config/FppConstantsAc.hpp"

namespace Components {
namespace DataRecorderCfg {

//! Streams recorded (TLM, EVT).
constexpr uint32_t NUM_STREAMS = 2;

//! Static ring slots per stream; SET_RING_SLOTS accepts 1..RING_SLOTS_MAX.
constexpr uint16_t RING_SLOTS_MAX = 32;
//! Ring slot payload capacity: one Fw::ComBuffer.
constexpr uint16_t SLOT_BYTES = FW_COM_BUFFER_MAX_SIZE;

//! Defaults, TLM then EVT.
constexpr uint8_t RING_SLOTS[NUM_STREAMS] = {32, 32};
constexpr uint8_t FLUSH_RECORDS[NUM_STREAMS] = {16, 8};
constexpr uint16_t FLUSH_INTERVAL_S[NUM_STREAMS] = {60, 10};
constexpr uint32_t CAPACITY_BYTES[NUM_STREAMS] = {8388608, 2097152};
constexpr uint32_t RETENTION_S[NUM_STREAMS] = {604800, 2592000};

//! Fixed per stream (not commandable).
constexpr uint32_t SEGMENT_MAX_BYTES[NUM_STREAMS] = {32768, 16384};
constexpr uint32_t SEGMENT_MAX_S[NUM_STREAMS] = {3600, 3600};

//! Command limits.
constexpr uint16_t FLUSH_INTERVAL_MAX_S = 3600;
constexpr uint32_t RETENTION_MIN_S = 60;
constexpr uint32_t RETENTION_MAX_S = 2592000;

//! Shared.
constexpr uint64_t RESERVE_BYTES = 16777216;  //!< SD space kept free of both streams' capacity
constexpr uint32_t STAGE_BYTES = 4096;        //!< one write per stream per tick at most this size
constexpr uint32_t SCAN_BUDGET = 32;          //!< directory reads / exists probes per stream per tick
constexpr uint32_t LIST_MAX_EVENTS = 16;      //!< SegmentInfo events per LIST_SEGMENTS
constexpr uint32_t LIST_MAX_PROBES = 64;      //!< sequence numbers probed per LIST_SEGMENTS / DOWNLINK_NEWEST

//! Configuration record payload (01-normative 5.5).
constexpr uint8_t CONFIG_LAYOUT = 1;
constexpr uint16_t CONFIG_PAYLOAD_SIZE = 2 + NUM_STREAMS * 12;

//! Path buffers ("/rec/tlm/00000042.bin" is 21 characters).
constexpr uint32_t PATH_MAX_CHARS = 32;
//! Directory entry name buffer; longer names are never segment names.
constexpr uint32_t NAME_MAX_CHARS = 64;

}  // namespace DataRecorderCfg
}  // namespace Components

#endif
