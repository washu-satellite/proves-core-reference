// ======================================================================
// \title  TcFrameCorrectorCodec.hpp
// \brief  Pure-logic single-bit corrector for CCSDS TC transfer frames.
//
// This header intentionally contains NO F Prime / Svc / Zephyr includes so it
// can be linked into host (gtest) unit tests. It uses only <cstdint>.
//
// Frame layout assumed (CCSDS TC transfer frame, lib/fprime/Svc/Ccsds/Types/
// Types.fpp:45-53):
//   offset  size  field
//   0       2     flagsAndScId   2b version | 1b bypass | 1b ctrl | 2b rsvd | 10b SCID
//   2       2     vcIdAndLength  6b virtual channel | 10b (frame length - 1)
//   4       1     frameSequenceNum
//   5       n     data
//   5+n     2     fecf           CRC-16/CCITT-FALSE over bytes [0, len-2)
//
// Correction theory (verified numerically 2026-09-05, see the component sdd):
//   The CRC is affine, so for a received frame the value
//       S = crc16Ccitt(frame, len-2) XOR BE16(frame + len-2)
//   depends only on the error pattern, not on the original frame. CRC-CCITT's
//   generator factors as (x+1)(x^15+x+1), which makes every single-bit error
//   syndrome unique and non-zero for frames up to 4095 bytes, makes the 16
//   FECF-bit syndromes exactly the powers of two (disjoint from every data-bit
//   syndrome), and makes every two-bit error syndrome distinct from all of
//   them. So S identifies at most one flipped bit, and two-bit errors can
//   never be mis-corrected into a "valid" frame.
//
//   The syndrome of the LAST data bit is the generator 0x1021; the syndrome of
//   each earlier bit is the previous one multiplied by x modulo the generator.
//   Walking that recurrence costs O(8*len) 16-bit shifts and no CRC passes, so
//   the corrector never searches candidates by re-running the CRC. Brute force
//   over candidates belongs in the test oracle only: this code runs in the
//   Zephyr system workqueue (LoRa RX callback context).
// ======================================================================

#ifndef Components_TcFrameCorrector_TcFrameCorrectorCodec_HPP
#define Components_TcFrameCorrector_TcFrameCorrectorCodec_HPP

#include <cstdint>

namespace Components {
namespace TcFrameCorrection {

//! Size of the CCSDS TC frame header (TCHeader, Types.fpp:45-50).
constexpr uint32_t TC_HEADER_SIZE = 5;
//! Size of the CCSDS TC frame trailer / FECF (TCTrailer, Types.fpp:51-53).
constexpr uint32_t TC_TRAILER_SIZE = 2;
//! Shortest frame the corrector will touch: header + trailer, no data.
constexpr uint32_t MIN_FRAME_BYTES = TC_HEADER_SIZE + TC_TRAILER_SIZE;
//! Longest frame the corrector will touch: the LoRa maximum packet payload
//! (lib/fprime-zephyr/fprime-zephyr/Drv/LoRa/LoRa.hpp:19 MAX_PACKET_SIZE).
//! Bounds the syndrome walk at 2000 iterations.
constexpr uint32_t MAX_FRAME_BYTES = 252;

//! Mask selecting the 10-bit frame-length subfield of vcIdAndLength
//! (Svc::Ccsds::TCSubfields::FrameLengthMask, Types.fpp:65).
constexpr uint16_t FRAME_LENGTH_MASK = 0x03FF;

//! Outcome of a correction attempt.
enum class Result : uint8_t {
    PASS_THROUGH,  //!< Frame outside [MIN_FRAME_BYTES, MAX_FRAME_BYTES]; bytes untouched.
    VALID,         //!< FECF already checks out; bytes untouched.
    CORRECTED,     //!< Exactly one bit flipped back; bitIndexOut names it.
    UNCORRECTABLE  //!< Two or more errors (or a failed post-check); bytes untouched.
};

//! \brief CRC-16/CCITT-FALSE: polynomial 0x1021, init 0xFFFF, MSB-first, no
//!        reflection, xorout 0. Matches Svc::Ccsds::Utils::CRC16::compute
//!        (lib/fprime/Svc/Ccsds/Utils/CRC16.hpp:43-49, which wraps the
//!        table-driven update_crc_ccitt in Utils/Hash/libcrc/lib_crc.c:132-144).
//!        Forwards to Crc16::ccitt (Components/Crc16/Crc16.hpp): bitwise, no table, no static objects.
//! \param data buffer to checksum (null is treated as empty)
//! \param len  number of bytes to checksum
//! \return the CRC-16 value; 0xFFFF for an empty buffer
uint16_t crc16Ccitt(const uint8_t* data, uint32_t len);

//! \brief Correct a single bit error in a CCSDS TC transfer frame, in place.
//!
//! The whole buffer is taken to be exactly one frame. Cost is bounded at
//! 8*MAX_FRAME_BYTES shift steps plus at most two CRC passes; nothing is
//! allocated and nothing is copied.
//!
//! When CORRECTED is returned the frame has been repaired in place and passes
//! the post-check (expected header token, declared length == len, FECF valid).
//! In every other case the buffer is byte-identical to the input: a speculative
//! flip whose post-check fails is undone before returning UNCORRECTABLE.
//!
//! \param frame         frame bytes, modified in place only on CORRECTED
//! \param len           number of valid bytes in frame
//! \param expectedToken expected value of the first two bytes, big-endian
//!                      (the bypass flag OR the spacecraft ID; 0x2044 in flight)
//! \param bitIndexOut   receives the corrected bit position, counted MSB-first
//!                      from the start of the frame; written only on CORRECTED
//! \return the outcome; see Result
Result correctSingleBit(uint8_t* frame, uint32_t len, uint16_t expectedToken, uint16_t& bitIndexOut);

}  // namespace TcFrameCorrection
}  // namespace Components

#endif
