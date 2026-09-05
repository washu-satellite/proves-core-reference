// ======================================================================
// \title  SequenceNumberStore.hpp
// \brief  Persistence of the anti-replay sequence number as a PersistedRecord.
//
// This header intentionally contains NO F Prime / Svc / Zephyr / mbedTLS
// includes so it can be linked into host (gtest) unit tests. Authenticate.cpp
// itself pulls in <psa/crypto.h>, Fw::Buffer and ComCfg::FrameContext and so
// cannot be compiled on the host; keeping the whole load/store decision here
// means the component's mapping is a single branch and the interesting
// behaviour (AUTH013) is testable off-target.
//
// Record layout: magic "ASN1", PersistedRecord header/CRC, 4-byte payload
// holding the sequence number as a little-endian uint32_t.
// ======================================================================

#ifndef Components_Authenticate_SequenceNumberStore_HPP
#define Components_Authenticate_SequenceNumberStore_HPP

#include <cstdint>

namespace Components {
namespace SequenceNumberStore {

//! Target file for the persisted sequence number. Single leading slash matches
//! the project convention. Distinct from the retired "//sequence_number.txt"
//! blob (a bare big-endian U32 with no magic, version or CRC), which is left
//! unread rather than migrated: it carries nothing that identifies it, so the
//! first boot of this image is a clean first boot and the ground resyncs
//! silently through the replay window.
constexpr const char* SEQUENCE_FILE_PATH = "/sequence_number.bin";
//! Staging file for the atomic replace.
constexpr const char* SEQUENCE_TEMP_PATH = "/sequence_number.tmp";

//! Size of the persisted payload: the sequence number, little-endian.
constexpr uint16_t SEQUENCE_PAYLOAD_SIZE = 4;

//! Outcome of a load, already reduced to the three cases the component acts on.
enum class LoadResult {
    LOADED,      //!< A valid record was read; value holds the stored number.
    FIRST_BOOT,  //!< No file present; value is 0 and no event is warranted.
    CORRUPT      //!< A present file failed validation or could not be read.
};

//! \brief Load the persisted sequence number.
//!
//! \param value    receives the stored number on LOADED, and 0 otherwise
//! \param status   receives the underlying PersistedRecord::Status as an
//!                 integer, for the component's warning event
//! \return LOADED, FIRST_BOOT or CORRUPT
LoadResult load(uint32_t& value, uint32_t& status);

//! \brief Atomically replace the persisted sequence number.
//!
//! \param value  the number to store
//! \param status receives the underlying PersistedRecord::Status as an integer
//! \return true when the record was stored; on false the previous record is
//!         still intact and valid
bool store(uint32_t value, uint32_t& status);

}  // namespace SequenceNumberStore
}  // namespace Components

#endif
