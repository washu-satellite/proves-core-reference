// ======================================================================
// \title  SequenceNumberStore.cpp
// \brief  Persistence of the anti-replay sequence number as a PersistedRecord.
// ======================================================================

#include "PROVESFlightControllerReference/Components/Authenticate/SequenceNumberStore.hpp"

#include "PROVESFlightControllerReference/Components/PersistedRecord/PersistedRecordFile.hpp"

namespace Components {
namespace SequenceNumberStore {

namespace {
namespace PR = Components::PersistedRecord;

// Record-type magic: "ASN1" (Authenticate Sequence Number, format version 1).
constexpr uint8_t SEQUENCE_MAGIC[4] = {'A', 'S', 'N', '1'};
}  // namespace

LoadResult load(uint32_t& value, uint32_t& status) {
    uint8_t payload[SEQUENCE_PAYLOAD_SIZE] = {0};
    uint16_t payloadLen = 0;

    const PR::Status result =
        PR::load(SEQUENCE_FILE_PATH, SEQUENCE_TEMP_PATH, SEQUENCE_MAGIC, payload, SEQUENCE_PAYLOAD_SIZE, payloadLen);
    status = static_cast<uint32_t>(result);
    value = 0;

    if (result == PR::Status::MISSING) {
        // First boot: baseline 0, no warning, and nothing written. The baseline
        // is already the default, so a write-back would only add a boot-time
        // writer to a file two Authenticate instances share.
        return LoadResult::FIRST_BOOT;
    }

    if (result != PR::Status::OK || payloadLen != SEQUENCE_PAYLOAD_SIZE) {
        // Truncated, wrong magic, bad length, bad CRC, unknown version, or a
        // file that is present but unreadable. Never adopt a value that failed
        // validation: fall back to the first-boot baseline (AUTH013).
        return LoadResult::CORRUPT;
    }

    value = static_cast<uint32_t>(payload[0]) | (static_cast<uint32_t>(payload[1]) << 8) |
            (static_cast<uint32_t>(payload[2]) << 16) | (static_cast<uint32_t>(payload[3]) << 24);
    return LoadResult::LOADED;
}

bool store(uint32_t value, uint32_t& status) {
    uint8_t payload[SEQUENCE_PAYLOAD_SIZE];
    payload[0] = static_cast<uint8_t>(value & 0xFFU);
    payload[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
    payload[2] = static_cast<uint8_t>((value >> 16) & 0xFFU);
    payload[3] = static_cast<uint8_t>((value >> 24) & 0xFFU);

    const PR::Status result =
        PR::store(SEQUENCE_FILE_PATH, SEQUENCE_TEMP_PATH, SEQUENCE_MAGIC, payload, SEQUENCE_PAYLOAD_SIZE);
    status = static_cast<uint32_t>(result);
    return result == PR::Status::OK;
}

}  // namespace SequenceNumberStore
}  // namespace Components
