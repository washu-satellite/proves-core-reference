// ======================================================================
// \title  PersistedRecordFile.cpp
// \brief  Atomic load/store of a self-validating record to a file.
// ======================================================================

#include "PROVESFlightControllerReference/Components/PersistedRecord/PersistedRecordFile.hpp"

#include "Os/File.hpp"
#include "Os/FileSystem.hpp"

namespace Components {
namespace PersistedRecord {

namespace {

//! One byte of slack so a file larger than any legal record is read back
//! oversize and rejected by decode() rather than silently truncated to a
//! valid-looking prefix.
constexpr uint32_t READ_BUFFER_SIZE = MAX_RECORD_SIZE + 1;

//! \brief Read and decode the record at path, with no temp-file fallback.
Status readRecord(const char* path,
                  const uint8_t* magic,
                  uint8_t* payloadOut,
                  uint16_t payloadCap,
                  uint16_t& payloadLenOut) {
    Os::File file;
    Os::File::Status fileStatus = file.open(path, Os::File::OPEN_READ);
    if (fileStatus != Os::File::OP_OK) {
        (void)file.close();
        if (fileStatus == Os::File::DOESNT_EXIST) {
            return Status::MISSING;
        }
        // Zephyr's file layer collapses every open failure, missing file
        // included, into OTHER_ERROR; stat the path to tell the two apart so
        // an absent file stays a silent first-boot default on both targets.
        return Os::FileSystem::exists(path) ? Status::OPEN_ERROR : Status::MISSING;
    }

    U8 buffer[READ_BUFFER_SIZE];
    FwSizeType size = static_cast<FwSizeType>(sizeof(buffer));
    fileStatus = file.read(buffer, size, Os::File::WaitType::WAIT);
    (void)file.close();
    if (fileStatus != Os::File::OP_OK) {
        return Status::READ_ERROR;
    }

    return decode(magic, buffer, static_cast<uint32_t>(size), payloadOut, payloadCap, payloadLenOut);
}

}  // namespace

Status load(const char* path,
            const char* tempPath,
            const uint8_t* magic,
            uint8_t* payloadOut,
            uint16_t payloadCap,
            uint16_t& payloadLenOut) {
    if (path == nullptr || magic == nullptr) {
        return Status::INVALID_ARGUMENT;
    }

    const Status status = readRecord(path, magic, payloadOut, payloadCap, payloadLenOut);
    if (status != Status::MISSING || tempPath == nullptr) {
        return status;
    }

    // Target absent: a previous store may have flushed the temporary file and
    // then failed to rename it. Anything but a fully valid temporary record is
    // reported as MISSING, i.e. the consumer's first-boot default.
    const Status tempStatus = readRecord(tempPath, magic, payloadOut, payloadCap, payloadLenOut);
    return (tempStatus == Status::OK) ? Status::OK : Status::MISSING;
}

Status store(const char* path,
             const char* tempPath,
             const uint8_t* magic,
             const uint8_t* payload,
             uint16_t payloadLen) {
    if (path == nullptr || tempPath == nullptr || magic == nullptr) {
        return Status::INVALID_ARGUMENT;
    }

    U8 record[MAX_RECORD_SIZE];
    const uint32_t recordSize = encode(magic, payload, payloadLen, record, MAX_RECORD_SIZE);
    if (recordSize == 0) {
        return Status::INVALID_ARGUMENT;
    }

    Os::File file;
    // OVERWRITE truncates a temporary left behind by an earlier failed store.
    Os::File::Status fileStatus = file.open(tempPath, Os::File::OPEN_CREATE, Os::File::OVERWRITE);
    if (fileStatus != Os::File::OP_OK) {
        (void)file.close();
        return Status::OPEN_ERROR;
    }

    FwSizeType size = static_cast<FwSizeType>(recordSize);
    fileStatus = file.write(record, size, Os::File::WaitType::WAIT);
    if (fileStatus != Os::File::OP_OK || size != static_cast<FwSizeType>(recordSize)) {
        (void)file.close();
        return Status::WRITE_ERROR;
    }

    // write(WAIT) flushes but discards the flush status, so ask explicitly:
    // without this a failed fs_sync would be reported as a successful store.
    fileStatus = file.flush();
    if (fileStatus != Os::File::OP_OK) {
        (void)file.close();
        return Status::SYNC_ERROR;
    }
    (void)file.close();

    if (Os::FileSystem::rename(tempPath, path) != Os::FileSystem::OP_OK) {
        return Status::RENAME_ERROR;
    }
    return Status::OK;
}

}  // namespace PersistedRecord
}  // namespace Components
