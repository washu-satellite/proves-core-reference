// ======================================================================
// \title  PersistedRecordFile.hpp
// \brief  Atomic load/store of a self-validating record to a file.
//
// Thin layer over Os::File and Os::FileSystem. It holds no state, allocates
// nothing on the heap, and deliberately does no logging and no FW_ASSERT: the
// consuming component owns the event and command-response policy for every
// status returned here.
//
// Update protocol (PersistedRecord-4): encode into a stack buffer, write the
// whole record to tempPath, flush it to media, then rename tempPath over path.
// The target is never opened on the write path, so a failure at any step
// leaves the previous record intact. A failed update leaves the temporary file
// behind; the next store truncates it.
// ======================================================================

#ifndef Components_PersistedRecord_PersistedRecordFile_HPP
#define Components_PersistedRecord_PersistedRecordFile_HPP

#include "PROVESFlightControllerReference/Components/PersistedRecord/PersistedRecordCodec.hpp"

namespace Components {
namespace PersistedRecord {

//! \brief Load and validate the record stored at path.
//!
//! An absent file yields Status::MISSING so the consumer can apply a first-boot
//! default without raising a corruption warning (PersistedRecord-6).
//!
//! When path is absent and tempPath is non-null, the temporary file is read as
//! a fallback: it is the record a previous store wrote and flushed but failed
//! to rename into place. This is read-only — nothing is written at boot — and a
//! stale temporary beside a present target is ignored entirely.
//!
//! \param path          target file
//! \param tempPath      temporary file to fall back to, or null for no fallback
//! \param magic         MAGIC_SIZE-byte record-type identifier expected
//! \param payloadOut    receives the payload; written ONLY when OK is returned
//! \param payloadCap    capacity of payloadOut in bytes
//! \param payloadLenOut receives the payload length; written ONLY when OK
//! \return Status::OK, Status::MISSING, a codec failure status, or an I/O status.
Status load(const char* path,
            const char* tempPath,
            const uint8_t* magic,
            uint8_t* payloadOut,
            uint16_t payloadCap,
            uint16_t& payloadLenOut);

//! \brief Atomically replace the record stored at path.
//!
//! \param path       target file
//! \param tempPath   temporary file used as the staging area (must differ from path)
//! \param magic      MAGIC_SIZE-byte record-type identifier
//! \param payload    payload bytes (may be null only when payloadLen is 0)
//! \param payloadLen payload length, must be <= MAX_PAYLOAD_SIZE
//! \return Status::OK on success; OPEN_ERROR, WRITE_ERROR, SYNC_ERROR or
//!         RENAME_ERROR for the step that failed, in which case the target
//!         still holds the previous record.
Status store(const char* path, const char* tempPath, const uint8_t* magic, const uint8_t* payload, uint16_t payloadLen);

}  // namespace PersistedRecord
}  // namespace Components

#endif
