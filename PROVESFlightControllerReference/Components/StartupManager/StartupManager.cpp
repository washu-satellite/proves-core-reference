// ======================================================================
// \title  StartupManager.cpp
// \author starchmd
// \brief  cpp file for StartupManager component implementation class
// ======================================================================

#include "PROVESFlightControllerReference/Components/StartupManager/StartupManager.hpp"

#include <cstdio>

#include "PROVESFlightControllerReference/Components/PersistedRecord/PersistedRecordFile.hpp"
#include <zephyr/drivers/rtc.h>

namespace Components {

namespace {
// Record-type magic values. A file left by an older image carries no magic at
// all, so it fails validation here rather than being misread (REQ-SM-008).
constexpr U8 BOOT_COUNT_MAGIC[4] = {'S', 'B', 'C', '1'};
constexpr U8 QUIESCENCE_MAGIC[4] = {'S', 'Q', 'S', '1'};

// Boot count payload: the count as a little-endian U64. FwSizeType is widened
// to 64 bits on disk so the format does not depend on the target's word size.
constexpr U16 BOOT_COUNT_PAYLOAD_SIZE = 8;

// Quiescence start payload, mirroring Fw::Time::SERIALIZED_SIZE (11 bytes):
//   [0..1]  timeBase  U16 little-endian (FwTimeBaseStoreType)
//   [2]     context   U8              (FwTimeContextStoreType)
//   [3..6]  seconds   U32 little-endian
//   [7..10] useconds  U32 little-endian
constexpr U16 QUIESCENCE_PAYLOAD_SIZE = 11;

// Longest path plus the ".tmp" suffix and a terminator. The parameter defaults
// are well inside this; an overlong path is reported as a store failure rather
// than silently truncated.
constexpr FwSizeType MAX_TEMP_PATH_SIZE = 64;

//! Derive the staging path for an atomic replace: "<path>.tmp".
//! \return false when the result would not fit in cap bytes
bool tempPathFor(const char* path, char* out, FwSizeType cap) {
    const int written = snprintf(out, static_cast<size_t>(cap), "%s.tmp", path);
    return written > 0 && static_cast<FwSizeType>(written) < cap;
}

void encodeU64LE(U64 value, U8* out) {
    for (U8 i = 0; i < 8; i++) {
        out[i] = static_cast<U8>((value >> (8 * i)) & 0xFFU);
    }
}

U64 decodeU64LE(const U8* in) {
    U64 value = 0;
    for (U8 i = 0; i < 8; i++) {
        value |= static_cast<U64>(in[i]) << (8 * i);
    }
    return value;
}

void encodeU32LE(U32 value, U8* out) {
    out[0] = static_cast<U8>(value & 0xFFU);
    out[1] = static_cast<U8>((value >> 8) & 0xFFU);
    out[2] = static_cast<U8>((value >> 16) & 0xFFU);
    out[3] = static_cast<U8>((value >> 24) & 0xFFU);
}

U32 decodeU32LE(const U8* in) {
    return static_cast<U32>(in[0]) | (static_cast<U32>(in[1]) << 8) | (static_cast<U32>(in[2]) << 16) |
           (static_cast<U32>(in[3]) << 24);
}

void encodeTime(const Fw::Time& time, U8* out) {
    const U16 timeBase = static_cast<U16>(time.getTimeBase());
    out[0] = static_cast<U8>(timeBase & 0xFFU);
    out[1] = static_cast<U8>((timeBase >> 8) & 0xFFU);
    out[2] = static_cast<U8>(time.getContext());
    encodeU32LE(time.getSeconds(), &out[3]);
    encodeU32LE(time.getUSeconds(), &out[7]);
}

//! Decode a persisted quiescence start time.
//! \return false when the record is CRC-valid but carries a useconds field
//!         outside the [0, 999999] contract Fw::Time::set asserts on. Without
//!         this check a corrupt file (e.g. a partial flash write leaving 0xFF
//!         bytes) panics downstream Fw::Time::add in a boot loop.
bool decodeTime(const U8* in, Fw::Time& time) {
    const U32 seconds = decodeU32LE(&in[3]);
    const U32 useconds = decodeU32LE(&in[7]);
    if (useconds >= 1000000) {
        return false;
    }
    const U16 timeBase = static_cast<U16>(static_cast<U16>(in[0]) | (static_cast<U16>(in[1]) << 8));
    time.set(static_cast<TimeBase::T>(timeBase), static_cast<FwTimeContextStoreType>(in[2]), seconds, useconds);
    return true;
}
}  // namespace

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

StartupManager ::StartupManager(const char* const compName) : StartupManagerComponentBase(compName) {}

StartupManager ::~StartupManager() {}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

FwSizeType StartupManager ::get_boot_count(bool increment) {
    // Read the boot count file path from parameter and assert that it is either valid or the default value
    Fw::ParamValid is_valid;
    auto boot_count_file = this->paramGet_BOOT_COUNT_FILE(is_valid);
    FW_ASSERT(is_valid == Fw::ParamValid::VALID || is_valid == Fw::ParamValid::DEFAULT);

    const char* const path = boot_count_file.toChar();
    char temp_path[MAX_TEMP_PATH_SIZE];
    const bool temp_ok = tempPathFor(path, temp_path, MAX_TEMP_PATH_SIZE);

    // Load the persisted boot count. A missing file is the first boot: count 0,
    // silently. Anything present that fails validation falls back to the same
    // defined default and warns exactly once (REQ-SM-008).
    FwSizeType boot_count = 0;
    U8 payload[BOOT_COUNT_PAYLOAD_SIZE] = {0};
    U16 payload_len = 0;
    const PersistedRecord::Status status = PersistedRecord::load(path, temp_ok ? temp_path : nullptr, BOOT_COUNT_MAGIC,
                                                                 payload, BOOT_COUNT_PAYLOAD_SIZE, payload_len);

    if (status == PersistedRecord::Status::OK && payload_len == BOOT_COUNT_PAYLOAD_SIZE) {
        boot_count = static_cast<FwSizeType>(decodeU64LE(payload));
    } else if (status != PersistedRecord::Status::MISSING) {
        this->log_WARNING_LO_BootCountUpdateFailure();
    }

    boot_count = FW_MAX(1, increment ? boot_count + 1 : boot_count);

    // Only a counted boot rewrites the file. GET_BOOT_COUNT reads without
    // rewriting an identical value, which is unobservable flash wear.
    if (increment) {
        U8 out[BOOT_COUNT_PAYLOAD_SIZE];
        encodeU64LE(static_cast<U64>(boot_count), out);
        const bool stored = temp_ok && PersistedRecord::store(path, temp_path, BOOT_COUNT_MAGIC, out,
                                                              BOOT_COUNT_PAYLOAD_SIZE) == PersistedRecord::Status::OK;
        if (!stored) {
            this->log_WARNING_LO_BootCountUpdateFailure();
        }
    }
    return boot_count;
}

Fw::Time StartupManager ::update_quiescence_start() {
    Fw::ParamValid is_valid;
    auto time_file = this->paramGet_QUIESCENCE_START_FILE(is_valid);
    FW_ASSERT(is_valid == Fw::ParamValid::VALID || is_valid == Fw::ParamValid::DEFAULT);

    const char* const path = time_file.toChar();
    char temp_path[MAX_TEMP_PATH_SIZE];
    const bool temp_ok = tempPathFor(path, temp_path, MAX_TEMP_PATH_SIZE);

    U8 payload[QUIESCENCE_PAYLOAD_SIZE] = {0};
    U16 payload_len = 0;
    const PersistedRecord::Status status = PersistedRecord::load(path, temp_ok ? temp_path : nullptr, QUIESCENCE_MAGIC,
                                                                 payload, QUIESCENCE_PAYLOAD_SIZE, payload_len);

    // A valid record is the single quiescence start time for the whole mission
    // and is returned untouched: the file is not rewritten on later boots.
    if (status == PersistedRecord::Status::OK && payload_len == QUIESCENCE_PAYLOAD_SIZE) {
        Fw::Time stored;
        if (decodeTime(payload, stored)) {
            return stored;
        }
    }

    // Anything present that fails validation warns once; a missing file is the
    // first boot and stays silent.
    if (status != PersistedRecord::Status::MISSING) {
        this->log_WARNING_LO_QuiescenceFileInitFailure();
    }

    // Restart quiescence from now and persist it for future reads.
    const Fw::Time time = this->getTime();
    U8 out[QUIESCENCE_PAYLOAD_SIZE];
    encodeTime(time, out);
    const bool stored = temp_ok && PersistedRecord::store(path, temp_path, QUIESCENCE_MAGIC, out,
                                                          QUIESCENCE_PAYLOAD_SIZE) == PersistedRecord::Status::OK;
    if (!stored) {
        this->log_WARNING_LO_QuiescenceFileInitFailure();
    }
    return time;
}

Fw::Time StartupManager ::get_uptime() {
    uint32_t seconds = k_uptime_seconds();
    Fw::Time time(TimeBase::TB_PROC_TIME, 0, static_cast<U32>(seconds), 0);
    return time;
}

void StartupManager ::sequenceStarted_handler(FwIndexType portNum, const Fw::StringBase& fileName) {
    // Reads in the file name of the start-up sequence from the sequenceStarted port and logs it.
    this->m_sequence_file = fileName;
}

void StartupManager ::completeSequence_handler(FwIndexType portNum,
                                               FwOpcodeType opCode,
                                               U32 cmdSeq,
                                               const Fw::CmdResponse& response) {
    // Emits a log event indicating the completion of the start-up sequence, and whether it was successful or not
    // based on the information in m_sequence_file and the command response.

    if (this->m_sequence_file == "//seq/startup.bin") {
        if (response == Fw::CmdResponse::OK) {
            this->log_ACTIVITY_LO_StartupSequenceFinished();
        } else {
            this->log_WARNING_LO_StartupSequenceFailed(response);
        }
    }
}

void StartupManager ::run_handler(FwIndexType portNum, U32 context) {
    Fw::ParamValid is_valid;

    // On the first call, update the boot count, set the quiescence start time, and dispatch the start-up sequence
    if (this->m_boot_count == 0) {
        this->m_boot_count = this->get_boot_count(true);
        this->m_quiescence_start = this->update_quiescence_start();

        Fw::ParamString first_sequence = this->paramGet_STARTUP_SEQUENCE_FILE(is_valid);
        FW_ASSERT(is_valid == Fw::ParamValid::VALID || is_valid == Fw::ParamValid::DEFAULT);
        this->runSequence_out(0, first_sequence);
    }

    // Calculate the quiescence end time based on the quiescence period parameter
    Fw::TimeIntervalValue quiescence_period = this->paramGet_QUIESCENCE_TIME(is_valid);
    FW_ASSERT(is_valid == Fw::ParamValid::VALID || is_valid == Fw::ParamValid::DEFAULT);
    Fw::Time quiescence_interval(this->m_quiescence_start.getTimeBase(), quiescence_period.get_seconds(),
                                 quiescence_period.get_useconds());
    Fw::Time end_time = Fw::Time::add(this->m_quiescence_start, quiescence_interval);

    // Are we waiting for quiescence?
    if (this->m_waiting) {
        // Check if the system is armed or if this is not the first boot. In both cases, we skip waiting.
        bool armed = this->paramGet_ARMED(is_valid);
        FW_ASSERT(is_valid == Fw::ParamValid::VALID || is_valid == Fw::ParamValid::DEFAULT);

        Fw::Time current_time =
            (end_time.getTimeBase() == TimeBase::TB_PROC_TIME) ? this->get_uptime() : this->getTime();

        // If not armed or this is not the first boot, we skip waiting
        if (!armed || end_time <= current_time) {
            this->m_waiting = false;
            this->cmdResponse_out(this->m_stored_opcode, this->m_stored_sequence, Fw::CmdResponse::OK);
        }
    }
    this->tlmWrite_QuiescenceEndTime(
        Fw::TimeValue(end_time.getTimeBase(), end_time.getContext(), end_time.getSeconds(), end_time.getUSeconds()));
    this->tlmWrite_BootCount(this->m_boot_count);
}

// ----------------------------------------------------------------------
// Handler implementations for commands
// ----------------------------------------------------------------------

void StartupManager ::WAIT_FOR_QUIESCENCE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->m_stored_opcode = opCode;
    this->m_stored_sequence = cmdSeq;
    this->m_waiting = true;
}

void StartupManager ::GET_BOOT_COUNT_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    FwSizeType boot_count = this->get_boot_count(false);
    this->log_ACTIVITY_LO_CurrentBootCount(static_cast<I64>(boot_count));
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

}  // namespace Components
