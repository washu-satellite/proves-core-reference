// ======================================================================
// \title  DriverBoardProtocol.hpp
// \brief  Pure wire codec for the SCALAR driver-board link (RP2350 <-> STM32L031).
//
// This header intentionally contains NO F Prime / Svc / Zephyr includes so it
// can be linked into host (gtest) unit tests and, in spirit, ported to the
// STM32 side. It uses only <cstddef> and <cstdint>.
//
// The normative spec is docs/sdd.md ("Wire protocol", a verbatim copy of
// docs-site/dev-loop/cycles/cycle-e-plan/02-protocol.md). Frame layout:
//
//   offset  size  field
//   0       2     SYNC   = 0x5C 0xA1
//   2       1     TYPE   (bit 7 set = board -> host)
//   3       1     SEQ    per direction, wraps; the receiver counts gaps
//   4       1     LEN    payload length, 0..32
//   5       LEN   PAYLOAD little-endian integers (DriverBoardMessages.hpp)
//   5+LEN   2     CRC16  CRC-16/CCITT-FALSE over TYPE..PAYLOAD, big-endian
//
// Parser design: one 39-byte buffer, fed a byte at a time. The parse state is
// a function of the buffered bytes alone (SYNC at [0..1], LEN at [4], CRC at
// [5+LEN..]), so "discard to the next 0x5C" on a rejection is literally
// dropping the first buffered byte and looking again: bytes that a truncated
// or length-corrupted candidate swallowed are re-examined and a valid frame
// among them is still delivered. One rejection is counted per contiguous
// stretch of undeliverable bytes (a corrupt frame, the garbage between two
// frames, a false SYNC's failed candidate), not per byte.
//
// No heap, no static objects, no exceptions. sizeof(Parser) <= 64.
// ======================================================================

#ifndef Components_DriverBoardProtocol_DriverBoardProtocol_HPP
#define Components_DriverBoardProtocol_DriverBoardProtocol_HPP

#include <cstddef>
#include <cstdint>

namespace Components {
namespace DriverBoardProtocol {

// ---- Framing constants (02 "Frame") ----------------------------------

constexpr uint8_t SYNC0 = 0x5C;
constexpr uint8_t SYNC1 = 0xA1;
constexpr size_t SYNC_SIZE = 2;
constexpr size_t HEADER_SIZE = 5;  //!< SYNC(2) TYPE SEQ LEN
constexpr size_t CRC_SIZE = 2;
constexpr size_t MAX_PAYLOAD = 32;
constexpr size_t MIN_FRAME = HEADER_SIZE + CRC_SIZE;                //!< 7
constexpr size_t MAX_FRAME = HEADER_SIZE + MAX_PAYLOAD + CRC_SIZE;  //!< 39
constexpr size_t OFFSET_TYPE = 2;
constexpr size_t OFFSET_SEQ = 3;
constexpr size_t OFFSET_LEN = 4;
constexpr size_t OFFSET_PAYLOAD = 5;
//! TYPE bit that marks a board -> host message.
constexpr uint8_t BOARD_TO_HOST_BIT = 0x80;

//! Every TYPE code in the spec's two message tables.
enum class Type : uint8_t {
    // host -> board
    HEARTBEAT = 0x01,
    HK_REQUEST = 0x02,
    ARM = 0x03,
    DISARM = 0x04,
    PULSE = 0x05,
    ABORT = 0x06,
    PING = 0x07,
    TIME_SYNC = 0x08,
    STREAM_START = 0x09,
    STREAM_STOP = 0x0A,
    // board -> host
    ACK = 0x81,
    HK = 0x82,
    PONG = 0x87,
    SAMPLE = 0x88,
    FAULT = 0x8F,
};

//! Why the last counted rejection happened.
enum class Reject : uint8_t {
    NONE = 0,    //!< nothing rejected since the counters were reset
    SYNC = 1,    //!< bytes outside any frame, or 0x5C not followed by 0xA1
    LENGTH = 2,  //!< LEN > MAX_PAYLOAD
    CRC = 3,     //!< CRC mismatch
};

//! One decoded frame, copied out of the parser by value.
struct Frame {
    uint8_t type;
    uint8_t seq;
    uint8_t len;                   //!< 0..MAX_PAYLOAD
    uint8_t payload[MAX_PAYLOAD];  //!< bytes beyond len are zero
};

//! Per-parser counters. All wrap at 2^32.
struct Stats {
    uint32_t accepted;  //!< frames delivered
    uint32_t rejected;  //!< contiguous stretches of undeliverable bytes
    uint32_t seqGaps;   //!< accepted frames whose SEQ != previous SEQ + 1 (mod 256)
    Reject lastReject;  //!< reason of the most recent counted rejection
};

//! \brief Build one frame: SYNC TYPE SEQ LEN PAYLOAD CRC.
//! \param type      TYPE byte (see Type)
//! \param seq       SEQ byte
//! \param payload   payload bytes, may be null when len == 0
//! \param len       payload length, 0..MAX_PAYLOAD
//! \param out       destination buffer
//! \param outCapacity size of out in bytes
//! \return the frame length (HEADER_SIZE + len + CRC_SIZE), or 0 if len > MAX_PAYLOAD,
//!         payload is null with len > 0, out is null, or outCapacity is too small
size_t encode(uint8_t type, uint8_t seq, const uint8_t* payload, uint8_t len, uint8_t* out, size_t outCapacity);

//! \brief Byte-at-a-time frame parser holding one frame of buffer.
class Parser {
  public:
    Parser();

    //! \brief Feed one received byte.
    //! \return true exactly when a frame became available; read it with frame()
    //!         before the next feed(). Note the byte that completes a rejected
    //!         candidate can complete a frame that the candidate had swallowed,
    //!         so true does not always mean "this byte was a CRC byte".
    bool feed(uint8_t byte);

    //! \brief Copy of the frame made available by the last feed() that returned true.
    //!        All-zero if no frame is available.
    Frame frame() const;

    //! \brief Counters since the last resetStats().
    const Stats& stats() const;

    //! \brief Drop buffered bytes and the SEQ history; counters are kept.
    void reset();

    //! \brief Zero the counters; buffered bytes are kept.
    void resetStats();

  private:
    //! Re-examine the buffer from its first byte until a frame is complete or more bytes are needed.
    bool settle();
    //! Remove the first n buffered bytes.
    void drop(size_t n);
    //! Count a rejection unless one is already open for this stretch of bytes.
    void reject(Reject why);
    //! Record an accepted frame of the given total length at the front of the buffer.
    void accept(size_t total);

    uint8_t m_buf[MAX_FRAME];
    uint8_t m_len;       //!< bytes held in m_buf
    uint8_t m_readyLen;  //!< length of the delivered frame at m_buf[0..], 0 if none
    bool m_discarding;   //!< a rejection has been counted for the current stretch
    bool m_haveSeq;      //!< m_lastSeq is valid
    uint8_t m_lastSeq;   //!< SEQ of the last accepted frame
    Stats m_stats;
};

}  // namespace DriverBoardProtocol
}  // namespace Components

#endif
