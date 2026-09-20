// ======================================================================
// \title  SequenceNumberStore.hpp
// \brief  Persistence of the anti-replay sequence number as a PersistedRecord.
//
// This header intentionally contains NO F Prime / Svc / Zephyr / mbedTLS
// includes so it can be linked into host (gtest) unit tests, like the sibling
// Parser / Validator / Authenticator helpers. TcSecurityDeframer.cpp itself
// pulls in <psa/crypto.h>, Fw::Buffer and ComCfg::FrameContext and so cannot
// be compiled on the host; keeping the whole load/store decision here means
// the component's mapping is a single branch and the interesting behaviour
// (AUTH013) is testable off-target.
//
// Record layout: magic "ASN1", PersistedRecord header/CRC, 4-byte payload
// holding the sequence number as a little-endian uint32_t. The target path is
// the component's SEQ_NUM_FILE_PATH parameter; the staging file for the atomic
// replace is "<path>.tmp". Both deframer instances (LoRa, UART) share one path
// and therefore one staging file (pre-existing, as the two Authenticate
// instances did): a raced write fails the CRC on the next load and restarts at
// 0 with one warning rather than reading garbage.
// ======================================================================

#ifndef Components_TcSecurityDeframer_SequenceNumberStore_HPP
#define Components_TcSecurityDeframer_SequenceNumberStore_HPP

#include <cstdint>

namespace Components {
namespace SequenceNumberStore {

//! Longest target path plus the ".tmp" suffix and a terminator. The parameter
//! default is well inside this; an overlong path is reported as a failure
//! (PersistedRecord INVALID_ARGUMENT) rather than silently truncated.
constexpr uint32_t MAX_TEMP_PATH_SIZE = 64;

//! Size of the persisted payload: the sequence number, little-endian.
constexpr uint16_t SEQUENCE_PAYLOAD_SIZE = 4;

//! Outcome of a load, already reduced to the three cases the component acts on.
enum class LoadResult {
    LOADED,      //!< A valid record was read; value holds the stored number.
    FIRST_BOOT,  //!< No file present; value is 0 and no event is warranted.
    CORRUPT      //!< A present file failed validation or could not be read.
};

//! \brief Load the persisted sequence number from path.
//!
//! \param path     target file (the SEQ_NUM_FILE_PATH parameter)
//! \param value    receives the stored number on LOADED, and 0 otherwise
//! \param status   receives the underlying PersistedRecord::Status as an
//!                 integer, for the component's warning event
//! \return LOADED, FIRST_BOOT or CORRUPT
LoadResult load(const char* path, uint32_t& value, uint32_t& status);

//! \brief Atomically replace the persisted sequence number at path.
//!
//! \param path   target file (the SEQ_NUM_FILE_PATH parameter)
//! \param value  the number to store
//! \param status receives the underlying PersistedRecord::Status as an integer
//! \return true when the record was stored; on false the previous record is
//!         still intact and valid
bool store(const char* path, uint32_t value, uint32_t& status);

}  // namespace SequenceNumberStore
}  // namespace Components

#endif  // Components_TcSecurityDeframer_SequenceNumberStore_HPP
