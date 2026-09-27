// ======================================================================
// \title  DataRecorder.cpp
// \brief  cpp file for the DataRecorder component implementation class
//
// Rules implemented here: cycle-m-plan/01-normative.md sections 3 (commands),
// 5 (files), 7 (behaviour); algorithms: cycle-m-plan/03-advisory.md section 3.
// ======================================================================

#include "PROVESFlightControllerReference/Components/DataRecorder/DataRecorder.hpp"

#include "Fw/Types/String.hpp"
#include "Os/File.hpp"
#include "Os/FileSystem.hpp"
#include "PROVESFlightControllerReference/Components/DataRecorder/SegmentCodec.hpp"
#include "PROVESFlightControllerReference/Components/PersistedRecord/PersistedRecordFile.hpp"

namespace Components {

namespace {

namespace Cfg = DataRecorderCfg;
namespace SC = SegmentCodec;
namespace PR = PersistedRecord;

//! Recorder root and stream directories (index = RecorderStream ordinal).
const char* const ROOT_DIR = "/rec";
const char* const STREAM_DIR[Cfg::NUM_STREAMS] = {"/rec/tlm", "/rec/evt"};
const char* const CONFIG_PATH = "/rec/config.bin";
const char* const CONFIG_TEMP_PATH = "/rec/config.tmp";

//! PersistedRecord magic of the configuration record, "DRC1".
const U8 CONFIG_MAGIC[PR::MAGIC_SIZE] = {0x44, 0x52, 0x43, 0x31};

//! ConfigCorrupt status for a CRC-valid record whose payload is invalid.
constexpr U8 CONFIG_PAYLOAD_INVALID = 255;

//! Segment names are 8 decimal digits + ".bin".
constexpr U32 SEQ_DIGITS = 8;
constexpr U32 SEGMENT_NAME_CHARS = SEQ_DIGITS + 4;

constexpr U64 U32_LIMIT = 0xFFFFFFFFULL;

U32 clampU32(U64 v) {
    return (v > U32_LIMIT) ? static_cast<U32>(U32_LIMIT) : static_cast<U32>(v);
}

void putLe16(U8* out, U16 v) {
    out[0] = static_cast<U8>(v & 0xFFu);
    out[1] = static_cast<U8>((v >> 8) & 0xFFu);
}

void putLe32(U8* out, U32 v) {
    for (U32 i = 0; i < 4; i++) {
        out[i] = static_cast<U8>((v >> (8 * i)) & 0xFFu);
    }
}

U16 getLe16(const U8* in) {
    return static_cast<U16>(static_cast<U16>(in[0]) | (static_cast<U16>(in[1]) << 8));
}

U32 getLe32(const U8* in) {
    U32 v = 0;
    for (U32 i = 0; i < 4; i++) {
        v |= static_cast<U32>(in[i]) << (8 * i);
    }
    return v;
}

//! Seconds from `then` to `now`, negative differences counted as 0.
U64 elapsed(U32 now, U32 then) {
    return (now > then) ? static_cast<U64>(now - then) : 0;
}

Components::RecorderStream streamEnum(U32 s) {
    return Components::RecorderStream(static_cast<Components::RecorderStream::T>(s));
}

}  // namespace

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

DataRecorder::DataRecorder(const char* const compName)
    : DataRecorderComponentBase(compName), m_configLoaded(false), m_started(false) {
    for (U32 s = 0; s < Cfg::NUM_STREAMS; s++) {
        StreamState& st = this->m_streams[s];
        st.cfg.ringSlots = Cfg::RING_SLOTS[s];
        st.cfg.flushRecords = Cfg::FLUSH_RECORDS[s];
        st.cfg.flushIntervalS = Cfg::FLUSH_INTERVAL_S[s];
        st.cfg.capacityBytes = Cfg::CAPACITY_BYTES[s];
        st.cfg.retentionS = Cfg::RETENTION_S[s];
        (void)st.ring.setCapacity(st.cfg.ringSlots);
        st.scan = SCAN_IDLE;
        st.scanCount = 0;
        st.scanBytes = 0;
        st.scanMin1 = 0;
        st.scanMin2 = 0;
        st.scanMax = 0;
        st.segmentsOnDisk = 0;
        st.bytesOnDisk = 0;
        st.nextSeq = 1;
        st.segOpen = false;
        st.segSeq = 0;
        st.segBytes = 0;
        st.segTicks = 0;
        st.oldest = SegmentRef{false, false, false, 0, 0};
        st.second = SegmentRef{false, false, false, 0, 0};
        st.find = FIND_DONE;
        st.findCursor = 0;
        st.recordsOnDisk = 0;
        st.writeFailures = 0;
        st.ticksSinceFlush = 0;
    }
    for (U32 i = 0; i < Cfg::STAGE_BYTES; i++) {
        this->m_stage[i] = 0;
    }
}

DataRecorder::~DataRecorder() {}

// ----------------------------------------------------------------------
// Input ports
// ----------------------------------------------------------------------

void DataRecorder::tlmIn_handler(FwIndexType portNum, Fw::ComBuffer& data, U32 context) {
    (void)portNum;
    (void)context;
    this->record(RecorderStream::TLM, data);
}

void DataRecorder::evtIn_handler(FwIndexType portNum, Fw::ComBuffer& data, U32 context) {
    (void)portNum;
    (void)context;
    this->record(RecorderStream::EVT, data);
}

void DataRecorder::record(U32 s, Fw::ComBuffer& data) {
    // Runs under the component lock taken by the guarded port: copy only.
    const FwSizeType size = data.getSize();
    if (size == 0 || size > Cfg::SLOT_BYTES) {
        return;
    }
    (void)this->m_streams[s].ring.push(data.getBuffAddr(), static_cast<U16>(size));
}

void DataRecorder::schedIn_handler(FwIndexType portNum, U32 context) {
    (void)portNum;
    (void)context;
    Os::ScopeLock fileGuard(this->m_fileLock);
    const U32 now = this->getTime().getSeconds();
    if (!this->m_started) {
        this->loadConfigOnce();
        this->m_started = true;
    }
    for (U32 s = 0; s < Cfg::NUM_STREAMS; s++) {
        if (this->scanning(s)) {
            this->scanStep(s);
        } else {
            this->flushStep(s, now);
            this->rotationStep(s);
            this->retentionStep(s, now);
        }
    }
    this->writeTelemetry(now);
}

// ----------------------------------------------------------------------
// Commands
// ----------------------------------------------------------------------

void DataRecorder::SET_RING_SLOTS_cmdHandler(FwOpcodeType opCode,
                                             U32 cmdSeq,
                                             const Components::RecorderStream& stream,
                                             U8 slots) {
    Fw::CmdResponse response = Fw::CmdResponse::VALIDATION_ERROR;
    U32 s = 0;
    if (validStream(stream, s)) {
        Os::ScopeLock fileGuard(this->m_fileLock);
        this->loadConfigOnce();
        StreamState& st = this->m_streams[s];
        if (slots < 1 || slots > Cfg::RING_SLOTS_MAX || slots < st.cfg.flushRecords) {
            response = this->rejected(ConfigField::RING_SLOTS, slots, Cfg::RING_SLOTS_MAX);
        } else {
            st.cfg.ringSlots = slots;
            this->lock();
            (void)st.ring.setCapacity(slots);
            this->unLock();
            this->log_ACTIVITY_HI_ConfigApplied(streamEnum(s), ConfigField::RING_SLOTS, slots);
            response = Fw::CmdResponse::OK;
        }
    }
    this->cmdResponse_out(opCode, cmdSeq, response);
}

void DataRecorder::SET_CAPACITY_cmdHandler(FwOpcodeType opCode,
                                           U32 cmdSeq,
                                           const Components::RecorderStream& stream,
                                           U32 bytes) {
    Fw::CmdResponse response = Fw::CmdResponse::VALIDATION_ERROR;
    U32 s = 0;
    if (validStream(stream, s)) {
        Os::ScopeLock fileGuard(this->m_fileLock);
        this->loadConfigOnce();
        // L = min(2^32 - 1, total - RESERVE_BYTES - capacity of the other streams); 0 if unknown or negative.
        U64 limit = 0;
        FwSizeType totalBytes = 0;
        FwSizeType freeBytes = 0;
        if (Os::FileSystem::getFreeSpace("/", totalBytes, freeBytes) == Os::FileSystem::OP_OK) {
            U64 others = 0;
            for (U32 o = 0; o < Cfg::NUM_STREAMS; o++) {
                if (o != s) {
                    others += this->m_streams[o].cfg.capacityBytes;
                }
            }
            const U64 total = static_cast<U64>(totalBytes);
            if (total > Cfg::RESERVE_BYTES + others) {
                limit = total - Cfg::RESERVE_BYTES - others;
            }
        }
        const U32 maxBytes = clampU32(limit);
        if (bytes < Cfg::SEGMENT_MAX_BYTES[s] || bytes > maxBytes) {
            response = this->rejected(ConfigField::CAPACITY_BYTES, bytes, maxBytes);
        } else {
            this->m_streams[s].cfg.capacityBytes = bytes;
            this->log_ACTIVITY_HI_ConfigApplied(streamEnum(s), ConfigField::CAPACITY_BYTES, bytes);
            response = Fw::CmdResponse::OK;
        }
    }
    this->cmdResponse_out(opCode, cmdSeq, response);
}

void DataRecorder::SET_RETENTION_S_cmdHandler(FwOpcodeType opCode,
                                              U32 cmdSeq,
                                              const Components::RecorderStream& stream,
                                              U32 seconds) {
    Fw::CmdResponse response = Fw::CmdResponse::VALIDATION_ERROR;
    U32 s = 0;
    if (validStream(stream, s)) {
        Os::ScopeLock fileGuard(this->m_fileLock);
        this->loadConfigOnce();
        if (seconds < Cfg::RETENTION_MIN_S || seconds > Cfg::RETENTION_MAX_S) {
            response = this->rejected(ConfigField::RETENTION_S, seconds, Cfg::RETENTION_MAX_S);
        } else {
            this->m_streams[s].cfg.retentionS = seconds;
            this->log_ACTIVITY_HI_ConfigApplied(streamEnum(s), ConfigField::RETENTION_S, seconds);
            response = Fw::CmdResponse::OK;
        }
    }
    this->cmdResponse_out(opCode, cmdSeq, response);
}

void DataRecorder::SET_FLUSH_cmdHandler(FwOpcodeType opCode,
                                        U32 cmdSeq,
                                        const Components::RecorderStream& stream,
                                        U8 records,
                                        U16 intervalS) {
    Fw::CmdResponse response = Fw::CmdResponse::VALIDATION_ERROR;
    U32 s = 0;
    if (validStream(stream, s)) {
        Os::ScopeLock fileGuard(this->m_fileLock);
        this->loadConfigOnce();
        StreamState& st = this->m_streams[s];
        if (records < 1 || records > st.cfg.ringSlots) {
            response = this->rejected(ConfigField::FLUSH_RECORDS, records, st.cfg.ringSlots);
        } else if (intervalS < 1 || intervalS > Cfg::FLUSH_INTERVAL_MAX_S) {
            response = this->rejected(ConfigField::FLUSH_INTERVAL_S, intervalS, Cfg::FLUSH_INTERVAL_MAX_S);
        } else {
            st.cfg.flushRecords = records;
            st.cfg.flushIntervalS = intervalS;
            this->log_ACTIVITY_HI_ConfigApplied(streamEnum(s), ConfigField::FLUSH_RECORDS, records);
            this->log_ACTIVITY_HI_ConfigApplied(streamEnum(s), ConfigField::FLUSH_INTERVAL_S, intervalS);
            response = Fw::CmdResponse::OK;
        }
    }
    this->cmdResponse_out(opCode, cmdSeq, response);
}

void DataRecorder::SAVE_CONFIG_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    Fw::CmdResponse response = Fw::CmdResponse::OK;
    {
        Os::ScopeLock fileGuard(this->m_fileLock);
        this->loadConfigOnce();
        U8 payload[Cfg::CONFIG_PAYLOAD_SIZE];
        payload[0] = Cfg::CONFIG_LAYOUT;
        payload[1] = static_cast<U8>(Cfg::NUM_STREAMS);
        for (U32 s = 0; s < Cfg::NUM_STREAMS; s++) {
            const StreamConfig& c = this->m_streams[s].cfg;
            U8* out = &payload[2 + 12 * s];
            out[0] = c.ringSlots;
            out[1] = c.flushRecords;
            putLe16(&out[2], c.flushIntervalS);
            putLe32(&out[4], c.capacityBytes);
            putLe32(&out[8], c.retentionS);
        }
        (void)Os::FileSystem::createDirectory(ROOT_DIR);
        if (PR::store(CONFIG_PATH, CONFIG_TEMP_PATH, CONFIG_MAGIC, payload, Cfg::CONFIG_PAYLOAD_SIZE) !=
            PR::Status::OK) {
            response = Fw::CmdResponse::EXECUTION_ERROR;
        }
    }
    this->cmdResponse_out(opCode, cmdSeq, response);
}

void DataRecorder::LIST_SEGMENTS_cmdHandler(FwOpcodeType opCode,
                                            U32 cmdSeq,
                                            const Components::RecorderStream& stream,
                                            U32 fromSeq) {
    Fw::CmdResponse response = Fw::CmdResponse::VALIDATION_ERROR;
    U32 s = 0;
    if (validStream(stream, s)) {
        Os::ScopeLock fileGuard(this->m_fileLock);
        if (this->scanning(s)) {
            response = Fw::CmdResponse::BUSY;
        } else {
            const StreamState& st = this->m_streams[s];
            U64 seq = fromSeq;
            if (st.oldest.present && st.oldest.seq > seq) {
                seq = st.oldest.seq;
            }
            U32 events = 0;
            for (U32 probes = 0; probes < Cfg::LIST_MAX_PROBES && events < Cfg::LIST_MAX_EVENTS && seq < st.nextSeq;
                 probes++) {
                char path[Cfg::PATH_MAX_CHARS];
                segmentPath(s, static_cast<U32>(seq), path);
                if (Os::FileSystem::exists(path)) {
                    FwSizeType size = 0;
                    if (Os::FileSystem::getFileSize(path, size) != Os::FileSystem::OP_OK) {
                        size = 0;
                    }
                    U32 openS = 0;
                    if (this->readHeader(path, openS) != HEADER_VALID) {
                        openS = 0;
                    }
                    this->log_ACTIVITY_HI_SegmentInfo(streamEnum(s), static_cast<U32>(seq), clampU32(size), openS);
                    events++;
                }
                seq++;
            }
            response = Fw::CmdResponse::OK;
        }
    }
    this->cmdResponse_out(opCode, cmdSeq, response);
}

void DataRecorder::DELETE_SEGMENT_cmdHandler(FwOpcodeType opCode,
                                             U32 cmdSeq,
                                             const Components::RecorderStream& stream,
                                             U32 seq) {
    Fw::CmdResponse response = Fw::CmdResponse::VALIDATION_ERROR;
    U32 s = 0;
    if (validStream(stream, s)) {
        Os::ScopeLock fileGuard(this->m_fileLock);
        StreamState& st = this->m_streams[s];
        char path[Cfg::PATH_MAX_CHARS];
        segmentPath(s, seq, path);
        if (this->scanning(s)) {
            response = Fw::CmdResponse::BUSY;
        } else if ((st.segOpen && st.segSeq == seq) || !Os::FileSystem::exists(path)) {
            response = Fw::CmdResponse::VALIDATION_ERROR;
        } else {
            FwSizeType size = 0;
            if (Os::FileSystem::getFileSize(path, size) != Os::FileSystem::OP_OK) {
                size = 0;
            }
            if (Os::FileSystem::removeFile(path) != Os::FileSystem::OP_OK) {
                response = Fw::CmdResponse::EXECUTION_ERROR;
            } else {
                st.segmentsOnDisk = (st.segmentsOnDisk > 0) ? st.segmentsOnDisk - 1 : 0;
                st.bytesOnDisk = (st.bytesOnDisk > size) ? st.bytesOnDisk - size : 0;
                this->forgetSegment(s, seq);
                this->log_ACTIVITY_LO_SegmentDeleted(streamEnum(s), seq, DeleteReason::COMMAND);
                response = Fw::CmdResponse::OK;
            }
        }
    }
    this->cmdResponse_out(opCode, cmdSeq, response);
}

void DataRecorder::CLOSE_SEGMENT_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, const Components::RecorderStream& stream) {
    Fw::CmdResponse response = Fw::CmdResponse::VALIDATION_ERROR;
    U32 s = 0;
    if (validStream(stream, s)) {
        Os::ScopeLock fileGuard(this->m_fileLock);
        if (this->scanning(s)) {
            response = Fw::CmdResponse::BUSY;
        } else {
            response = this->closeSegment(s, this->getTime().getSeconds());
        }
    }
    this->cmdResponse_out(opCode, cmdSeq, response);
}

void DataRecorder::DOWNLINK_NEWEST_cmdHandler(FwOpcodeType opCode,
                                              U32 cmdSeq,
                                              const Components::RecorderStream& stream) {
    Fw::CmdResponse response = Fw::CmdResponse::VALIDATION_ERROR;
    U32 s = 0;
    if (validStream(stream, s)) {
        Os::ScopeLock fileGuard(this->m_fileLock);
        if (this->scanning(s)) {
            response = Fw::CmdResponse::BUSY;
        } else {
            response = this->closeSegment(s, this->getTime().getSeconds());
        }
        if (response == Fw::CmdResponse::OK) {
            // Newest present segment: probe down from nextSeq - 1.
            const StreamState& st = this->m_streams[s];
            char path[Cfg::PATH_MAX_CHARS];
            bool found = false;
            U32 seq = st.nextSeq;
            for (U32 probes = 0; probes < Cfg::LIST_MAX_PROBES && seq > 0 && st.segmentsOnDisk > 0 && !found;
                 probes++) {
                seq--;
                segmentPath(s, seq, path);
                found = Os::FileSystem::exists(path);
            }
            if (!found || !this->isConnected_sendFileOut_OutputPort(0)) {
                response = Fw::CmdResponse::EXECUTION_ERROR;
            } else {
                FwSizeType size = 0;
                if (Os::FileSystem::getFileSize(path, size) != Os::FileSystem::OP_OK) {
                    size = 0;
                }
                U32 openS = 0;
                if (this->readHeader(path, openS) != HEADER_VALID) {
                    openS = 0;
                }
                const Fw::String file(path);
                const Svc::SendFileResponse sent = this->sendFileOut_out(0, file, file, 0, 0);
                if (!(sent.get_status() == Svc::SendFileStatus::STATUS_OK)) {
                    response = Fw::CmdResponse::EXECUTION_ERROR;
                } else {
                    this->log_ACTIVITY_HI_SegmentInfo(streamEnum(s), seq, clampU32(size), openS);
                }
            }
        }
    }
    this->cmdResponse_out(opCode, cmdSeq, response);
}

// ----------------------------------------------------------------------
// Configuration
// ----------------------------------------------------------------------

Fw::CmdResponse DataRecorder::rejected(ConfigField::T field, U32 value, U32 max) {
    this->log_WARNING_LO_ConfigRejected(field, value, max);
    return Fw::CmdResponse::VALIDATION_ERROR;
}

bool DataRecorder::validConfig(U32 s, const StreamConfig& cfg) {
    return cfg.ringSlots >= 1 && cfg.ringSlots <= Cfg::RING_SLOTS_MAX && cfg.flushRecords >= 1 &&
           cfg.flushRecords <= cfg.ringSlots && cfg.flushIntervalS >= 1 &&
           cfg.flushIntervalS <= Cfg::FLUSH_INTERVAL_MAX_S && cfg.capacityBytes >= Cfg::SEGMENT_MAX_BYTES[s] &&
           cfg.retentionS >= Cfg::RETENTION_MIN_S && cfg.retentionS <= Cfg::RETENTION_MAX_S;
}

void DataRecorder::loadConfigOnce() {
    if (this->m_configLoaded) {
        return;
    }
    this->m_configLoaded = true;
    U8 payload[PR::MAX_PAYLOAD_SIZE];
    U16 len = 0;
    const PR::Status status =
        PR::load(CONFIG_PATH, CONFIG_TEMP_PATH, CONFIG_MAGIC, payload, static_cast<U16>(sizeof(payload)), len);
    if (status == PR::Status::MISSING) {
        return;  // first boot: defaults, no event
    }
    if (status != PR::Status::OK) {
        this->log_WARNING_HI_ConfigCorrupt(static_cast<U8>(status));
        return;
    }
    StreamConfig parsed[Cfg::NUM_STREAMS];
    bool valid = (len == Cfg::CONFIG_PAYLOAD_SIZE) && (payload[0] == Cfg::CONFIG_LAYOUT) &&
                 (payload[1] == static_cast<U8>(Cfg::NUM_STREAMS));
    for (U32 s = 0; valid && s < Cfg::NUM_STREAMS; s++) {
        const U8* in = &payload[2 + 12 * s];
        parsed[s].ringSlots = in[0];
        parsed[s].flushRecords = in[1];
        parsed[s].flushIntervalS = getLe16(&in[2]);
        parsed[s].capacityBytes = getLe32(&in[4]);
        parsed[s].retentionS = getLe32(&in[8]);
        valid = validConfig(s, parsed[s]);
    }
    if (!valid) {
        this->log_WARNING_HI_ConfigCorrupt(CONFIG_PAYLOAD_INVALID);
        return;
    }
    for (U32 s = 0; s < Cfg::NUM_STREAMS; s++) {
        this->applyConfig(s, parsed[s]);
    }
}

void DataRecorder::applyConfig(U32 s, const StreamConfig& cfg) {
    StreamState& st = this->m_streams[s];
    st.cfg = cfg;
    this->lock();
    (void)st.ring.setCapacity(cfg.ringSlots);
    this->unLock();
}

// ----------------------------------------------------------------------
// Boot scan (01-normative 7.6)
// ----------------------------------------------------------------------

bool DataRecorder::scanning(U32 s) const {
    return this->m_streams[s].scan != SCAN_READY;
}

void DataRecorder::scanStep(U32 s) {
    StreamState& st = this->m_streams[s];
    if (st.scan == SCAN_IDLE) {
        st.scanCount = 0;
        st.scanBytes = 0;
        st.scanMin1 = 0;
        st.scanMin2 = 0;
        st.scanMax = 0;
        Os::FileSystem::Status fsStatus = Os::FileSystem::createDirectory(ROOT_DIR);
        if (fsStatus == Os::FileSystem::OP_OK) {
            fsStatus = Os::FileSystem::createDirectory(STREAM_DIR[s]);
        }
        if (fsStatus != Os::FileSystem::OP_OK) {
            this->scanFailed(s, static_cast<U32>(fsStatus));
            return;
        }
        const Os::Directory::Status dirStatus = this->m_dir[s].open(STREAM_DIR[s], Os::Directory::READ);
        if (dirStatus != Os::Directory::OP_OK) {
            this->scanFailed(s, static_cast<U32>(dirStatus));
            return;
        }
        st.scan = SCAN_READING;
    }
    for (U32 i = 0; i < Cfg::SCAN_BUDGET; i++) {
        char name[Cfg::NAME_MAX_CHARS];
        name[0] = '\0';
        const Os::Directory::Status dirStatus = this->m_dir[s].read(name, static_cast<FwSizeType>(sizeof(name)));
        if (dirStatus == Os::Directory::NO_MORE_FILES) {
            this->m_dir[s].close();
            this->finishScan(s);
            return;
        }
        if (dirStatus != Os::Directory::OP_OK) {
            this->m_dir[s].close();
            this->scanFailed(s, static_cast<U32>(dirStatus));
            return;
        }
        U32 seq = 0;
        if (parseSegmentName(name, seq)) {
            char path[Cfg::PATH_MAX_CHARS];
            segmentPath(s, seq, path);
            FwSizeType size = 0;
            if (Os::FileSystem::getFileSize(path, size) != Os::FileSystem::OP_OK) {
                size = 0;
            }
            if (st.scanCount == 0 || seq < st.scanMin1) {
                st.scanMin2 = st.scanMin1;
                st.scanMin1 = seq;
            } else if (st.scanCount == 1 || seq < st.scanMin2) {
                st.scanMin2 = seq;
            }
            if (st.scanCount == 0 || seq > st.scanMax) {
                st.scanMax = seq;
            }
            st.scanCount++;
            st.scanBytes += static_cast<U64>(size);
        }
    }
}

void DataRecorder::scanFailed(U32 s, U32 status) {
    StreamState& st = this->m_streams[s];
    st.scan = SCAN_IDLE;
    st.scanCount = 0;
    st.scanBytes = 0;
    this->log_WARNING_HI_DirectoryScanFailed(streamEnum(s), status);
}

void DataRecorder::finishScan(U32 s) {
    StreamState& st = this->m_streams[s];
    st.segmentsOnDisk = st.scanCount;
    st.bytesOnDisk = st.scanBytes;
    st.nextSeq = (st.scanCount == 0) ? 1 : st.scanMax + 1;
    st.oldest = SegmentRef{st.scanCount >= 1, false, false, st.scanMin1, 0};
    st.second = SegmentRef{st.scanCount >= 2, false, false, st.scanMin2, 0};
    st.find = FIND_DONE;
    st.findCursor = 0;
    this->loadHeader(s, st.oldest);
    this->loadHeader(s, st.second);
    st.scan = SCAN_READY;
}

// ----------------------------------------------------------------------
// Flush, rotation, retention (01-normative 7.3 - 7.5, 7.7)
// ----------------------------------------------------------------------

void DataRecorder::flushStep(U32 s, U32 now) {
    StreamState& st = this->m_streams[s];
    st.ticksSinceFlush++;
    this->lock();
    const U16 count = st.ring.count();
    this->unLock();
    if (flushDue(count, st.cfg.flushRecords, st.ticksSinceFlush, st.cfg.flushIntervalS)) {
        (void)this->writeBatch(s, now);
    }
}

Fw::CmdResponse DataRecorder::closeSegment(U32 s, U32 now) {
    StreamState& st = this->m_streams[s];
    this->lock();
    const U16 count = st.ring.count();
    this->unLock();
    Fw::CmdResponse response = Fw::CmdResponse::OK;
    if (count > 0 && this->writeBatch(s, now) == WRITE_FAILED) {
        response = Fw::CmdResponse::EXECUTION_ERROR;
    }
    st.segOpen = false;
    return response;
}

DataRecorder::WriteResult DataRecorder::writeBatch(U32 s, U32 now) {
    StreamState& st = this->m_streams[s];
    const U32 segmentMax = Cfg::SEGMENT_MAX_BYTES[s];
    bool newSegment = !st.segOpen;
    U32 segmentStart = newSegment ? 0 : st.segBytes;
    U32 used = newSegment ? SC::HEADER_SIZE : 0;
    U16 staged = 0;

    // Stage the oldest whole records that fit, under the component lock (copies only).
    this->lock();
    const U32 firstId = st.ring.oldestId();
    const U16 count = st.ring.count();
    for (U16 i = 0; i < count; i++) {
        const U8* data = nullptr;
        U16 len = 0;
        if (!st.ring.peek(i, data, len)) {
            break;
        }
        const U32 recordBytes = static_cast<U32>(len) + SC::RECORD_OVERHEAD;
        if (segmentStart + used + recordBytes > segmentMax) {
            if (staged == 0 && !newSegment) {
                // The oldest record does not fit the open segment: close it, start a new one.
                newSegment = true;
                segmentStart = 0;
                used = SC::HEADER_SIZE;
            }
            if (segmentStart + used + recordBytes > segmentMax) {
                break;
            }
        }
        if (used + recordBytes > Cfg::STAGE_BYTES) {
            break;
        }
        used += SC::encodeRecord(data, len, &this->m_stage[used], Cfg::STAGE_BYTES - used);
        staged++;
    }
    this->unLock();
    if (staged == 0) {
        return WRITE_NOTHING;
    }

    char path[Cfg::PATH_MAX_CHARS];
    U32 seq = st.segSeq;
    if (newSegment) {
        st.segOpen = false;
        // Next free number: never reuse a name that exists (counted as a segment).
        seq = st.nextSeq;
        U32 probes = 0;
        while (true) {
            segmentPath(s, seq, path);
            if (!Os::FileSystem::exists(path)) {
                break;
            }
            FwSizeType size = 0;
            if (Os::FileSystem::getFileSize(path, size) != Os::FileSystem::OP_OK) {
                size = 0;
            }
            this->addSegment(s, seq, static_cast<U64>(size), false, 0);
            seq++;
            st.nextSeq = seq;
            probes++;
            if (probes >= Cfg::SCAN_BUDGET) {
                return WRITE_NOTHING;  // resume next tick
            }
        }
        SC::Header header;
        header.stream = static_cast<U8>(s);
        header.bootCount = 0xFFFF;  // no boot-count source is wired (reserved)
        header.openSeconds = now;
        header.openUseconds = 0;
        (void)SC::encodeHeader(header, this->m_stage, Cfg::STAGE_BYTES);
    } else {
        segmentPath(s, seq, path);
    }

    // One open, one write, one explicit flush, closed before returning.
    U32 failure = 0;
    Os::File file;
    Os::File::Status status = file.open(path, Os::File::OPEN_APPEND);
    if (status != Os::File::OP_OK) {
        failure = static_cast<U32>(status);
    } else {
        FwSizeType size = static_cast<FwSizeType>(used);
        status = file.write(this->m_stage, size, Os::File::WaitType::WAIT);
        if (status != Os::File::OP_OK) {
            failure = static_cast<U32>(status);
        } else if (size != static_cast<FwSizeType>(used)) {
            failure = static_cast<U32>(Os::File::BAD_SIZE);
        } else {
            status = file.flush();
            if (status != Os::File::OP_OK) {
                failure = static_cast<U32>(status);
            }
        }
    }
    file.close();

    if (failure != 0) {
        // Records stay in the ring; the segment is closed; the next attempt starts a new one.
        st.writeFailures++;
        FwSizeType size = 0;
        const bool present =
            Os::FileSystem::exists(path) && (Os::FileSystem::getFileSize(path, size) == Os::FileSystem::OP_OK);
        if (newSegment) {
            if (present) {
                this->addSegment(s, seq, static_cast<U64>(size), false, 0);
                st.nextSeq = seq + 1;
            }
        } else if (present && static_cast<U64>(size) > st.segBytes) {
            st.bytesOnDisk += static_cast<U64>(size) - st.segBytes;
        }
        st.segOpen = false;
        this->log_WARNING_HI_SegmentWriteFailed(streamEnum(s), failure);
        return WRITE_FAILED;
    }

    // Success: drop the written records (those the producer dropped meanwhile are already gone).
    this->lock();
    const U32 goneSince = st.ring.oldestId() - firstId;
    if (goneSince < staged) {
        st.ring.pop(static_cast<U16>(staged - goneSince));
    }
    this->unLock();
    st.recordsOnDisk += staged;
    st.ticksSinceFlush = 0;
    if (newSegment) {
        st.segOpen = true;
        st.segSeq = seq;
        st.segBytes = used;
        st.segTicks = 0;
        st.nextSeq = seq + 1;
        this->addSegment(s, seq, used, true, now);
        this->log_ACTIVITY_LO_SegmentOpened(streamEnum(s), seq);
    } else {
        st.segBytes += used;
        st.bytesOnDisk += used;
    }
    this->log_WARNING_HI_SegmentWriteFailed_ThrottleClear();
    return WRITE_OK;
}

void DataRecorder::rotationStep(U32 s) {
    StreamState& st = this->m_streams[s];
    if (st.segOpen) {
        st.segTicks++;
        if (st.segTicks >= Cfg::SEGMENT_MAX_S[s]) {
            st.segOpen = false;
        }
    }
}

void DataRecorder::retentionStep(U32 s, U32 now) {
    StreamState& st = this->m_streams[s];
    this->findStep(s);
    if (!st.oldest.present || (st.segOpen && st.oldest.seq == st.segSeq)) {
        return;  // nothing on disk, or the oldest is the open segment
    }
    DeleteReason::T reason = DeleteReason::CAPACITY;
    if (st.bytesOnDisk <= st.cfg.capacityBytes) {
        // Age: needs the next present segment T; the newest segment is never deleted for age.
        if (st.find != FIND_DONE || !st.second.present) {
            return;
        }
        this->loadHeader(s, st.oldest);
        this->loadHeader(s, st.second);
        if (!st.oldest.hdrKnown || !st.second.hdrKnown) {
            return;  // header unreadable now; retry next tick
        }
        bool expired = false;
        if (!st.oldest.hdrValid) {
            expired = true;
        } else if (st.second.hdrValid) {
            expired = elapsed(now, st.second.openS) > st.cfg.retentionS;
        } else {
            expired = elapsed(now, st.oldest.openS) >
                      static_cast<U64>(st.cfg.retentionS) + static_cast<U64>(Cfg::SEGMENT_MAX_S[s]);
        }
        if (!expired) {
            return;
        }
        reason = DeleteReason::AGE;
    }

    const U32 seq = st.oldest.seq;
    char path[Cfg::PATH_MAX_CHARS];
    segmentPath(s, seq, path);
    FwSizeType size = 0;
    if (Os::FileSystem::getFileSize(path, size) != Os::FileSystem::OP_OK) {
        size = 0;
    }
    const Os::FileSystem::Status removed = Os::FileSystem::removeFile(path);
    if (removed != Os::FileSystem::OP_OK && removed != Os::FileSystem::DOESNT_EXIST) {
        st.writeFailures++;
        this->log_WARNING_HI_SegmentWriteFailed(streamEnum(s), static_cast<U32>(removed));
        return;  // retried next tick
    }
    st.segmentsOnDisk = (st.segmentsOnDisk > 0) ? st.segmentsOnDisk - 1 : 0;
    st.bytesOnDisk = (st.bytesOnDisk > size) ? st.bytesOnDisk - size : 0;
    this->forgetSegment(s, seq);
    if (removed == Os::FileSystem::OP_OK) {
        this->log_ACTIVITY_LO_SegmentDeleted(streamEnum(s), seq, reason);
    }
}

void DataRecorder::findStep(U32 s) {
    StreamState& st = this->m_streams[s];
    if (st.find == FIND_OLDEST && st.segmentsOnDisk == 0) {
        st.oldest.present = false;
        st.second.present = false;
        st.find = FIND_DONE;
    }
    if (st.find == FIND_SECOND && st.segmentsOnDisk <= 1) {
        st.second.present = false;
        st.find = FIND_DONE;
    }
    U32 probes = 0;
    while (st.find != FIND_DONE && probes < Cfg::SCAN_BUDGET) {
        SegmentRef& ref = (st.find == FIND_OLDEST) ? st.oldest : st.second;
        if (st.findCursor >= st.nextSeq) {
            ref.present = false;
            if (st.find == FIND_OLDEST) {
                st.second.present = false;
            }
            st.find = FIND_DONE;
            break;
        }
        char path[Cfg::PATH_MAX_CHARS];
        segmentPath(s, st.findCursor, path);
        probes++;
        if (Os::FileSystem::exists(path)) {
            ref = SegmentRef{true, false, false, st.findCursor, 0};
            st.find = (st.find == FIND_OLDEST) ? FIND_SECOND : FIND_DONE;
            if (st.find == FIND_SECOND && st.segmentsOnDisk <= 1) {
                st.second.present = false;
                st.find = FIND_DONE;
            }
        }
        st.findCursor++;
    }
}

void DataRecorder::addSegment(U32 s, U32 seq, U64 size, bool hdrKnown, U32 openS) {
    StreamState& st = this->m_streams[s];
    st.segmentsOnDisk++;
    st.bytesOnDisk += size;
    if (st.find != FIND_DONE) {
        return;  // the running search reaches this number
    }
    const SegmentRef ref = SegmentRef{true, hdrKnown, hdrKnown, seq, openS};
    if (!st.oldest.present) {
        st.oldest = ref;
    } else if (!st.second.present) {
        st.second = ref;
    }
}

void DataRecorder::forgetSegment(U32 s, U32 seq) {
    StreamState& st = this->m_streams[s];
    if (st.oldest.present && st.oldest.seq == seq) {
        if (st.find == FIND_DONE && st.second.present) {
            st.oldest = st.second;
            st.second.present = false;
            st.find = FIND_SECOND;
            st.findCursor = st.oldest.seq + 1;
        } else {
            st.oldest.present = false;
            st.second.present = false;
            st.find = FIND_OLDEST;
            st.findCursor = seq + 1;
        }
    } else if (st.second.present && st.second.seq == seq) {
        st.second.present = false;
        st.find = FIND_SECOND;
        st.findCursor = seq + 1;
    }
}

void DataRecorder::loadHeader(U32 s, SegmentRef& ref) {
    if (!ref.present || ref.hdrKnown) {
        return;
    }
    char path[Cfg::PATH_MAX_CHARS];
    segmentPath(s, ref.seq, path);
    U32 openS = 0;
    const HeaderRead result = this->readHeader(path, openS);
    if (result == HEADER_UNREADABLE) {
        return;
    }
    ref.hdrKnown = true;
    ref.hdrValid = (result == HEADER_VALID);
    ref.openS = ref.hdrValid ? openS : 0;
}

DataRecorder::HeaderRead DataRecorder::readHeader(const char* path, U32& openS) {
    Os::File file;
    if (file.open(path, Os::File::OPEN_READ) != Os::File::OP_OK) {
        file.close();
        return HEADER_UNREADABLE;
    }
    U8 bytes[SC::HEADER_SIZE];
    FwSizeType size = static_cast<FwSizeType>(sizeof(bytes));
    const Os::File::Status status = file.read(bytes, size, Os::File::WaitType::WAIT);
    file.close();
    if (status != Os::File::OP_OK) {
        return HEADER_UNREADABLE;
    }
    SC::Header header;
    if (SC::decodeHeader(bytes, static_cast<U32>(size), header) != SC::Status::OK) {
        return HEADER_INVALID;
    }
    openS = header.openSeconds;
    return HEADER_VALID;
}

// ----------------------------------------------------------------------
// Telemetry
// ----------------------------------------------------------------------

void DataRecorder::writeTelemetry(U32 now) {
    U32 stored[Cfg::NUM_STREAMS];
    U32 dropped[Cfg::NUM_STREAMS];
    this->lock();
    for (U32 s = 0; s < Cfg::NUM_STREAMS; s++) {
        stored[s] = this->m_streams[s].ring.pushed();
        dropped[s] = this->m_streams[s].ring.dropped();
    }
    this->unLock();
    U32 age[Cfg::NUM_STREAMS];
    for (U32 s = 0; s < Cfg::NUM_STREAMS; s++) {
        const SegmentRef& oldest = this->m_streams[s].oldest;
        age[s] = (oldest.present && oldest.hdrKnown && oldest.hdrValid) ? clampU32(elapsed(now, oldest.openS)) : 0;
    }
    const StreamState& tlm = this->m_streams[RecorderStream::TLM];
    const StreamState& evt = this->m_streams[RecorderStream::EVT];
    this->tlmWrite_TlmRecordsStored(stored[RecorderStream::TLM]);
    this->tlmWrite_TlmRecordsOnDisk(tlm.recordsOnDisk);
    this->tlmWrite_TlmBytesOnDisk(clampU32(tlm.bytesOnDisk));
    this->tlmWrite_TlmSegmentsOnDisk(tlm.segmentsOnDisk);
    this->tlmWrite_TlmOldestRecordAgeS(age[RecorderStream::TLM]);
    this->tlmWrite_TlmRingDropped(dropped[RecorderStream::TLM]);
    this->tlmWrite_TlmWriteFailures(tlm.writeFailures);
    this->tlmWrite_EvtRecordsStored(stored[RecorderStream::EVT]);
    this->tlmWrite_EvtRecordsOnDisk(evt.recordsOnDisk);
    this->tlmWrite_EvtBytesOnDisk(clampU32(evt.bytesOnDisk));
    this->tlmWrite_EvtSegmentsOnDisk(evt.segmentsOnDisk);
    this->tlmWrite_EvtOldestRecordAgeS(age[RecorderStream::EVT]);
    this->tlmWrite_EvtRingDropped(dropped[RecorderStream::EVT]);
    this->tlmWrite_EvtWriteFailures(evt.writeFailures);
}

// ----------------------------------------------------------------------
// Names and paths
// ----------------------------------------------------------------------

bool DataRecorder::validStream(const Components::RecorderStream& stream, U32& s) {
    s = static_cast<U32>(static_cast<Components::RecorderStream::T>(stream));
    return s < Cfg::NUM_STREAMS;
}

void DataRecorder::segmentPath(U32 s, U32 seq, char* out) {
    // "<stream dir>/NNNNNNNN.bin" into a PATH_MAX_CHARS buffer (no printf).
    U32 n = 0;
    for (const char* p = STREAM_DIR[s]; *p != '\0' && n < Cfg::PATH_MAX_CHARS - 1; p++) {
        out[n++] = *p;
    }
    out[n++] = '/';
    U32 value = seq % 100000000u;
    for (U32 i = 0; i < SEQ_DIGITS; i++) {
        out[n + SEQ_DIGITS - 1 - i] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }
    n += SEQ_DIGITS;
    out[n++] = '.';
    out[n++] = 'b';
    out[n++] = 'i';
    out[n++] = 'n';
    out[n] = '\0';
}

bool DataRecorder::parseSegmentName(const char* name, U32& seq) {
    U32 value = 0;
    for (U32 i = 0; i < SEQ_DIGITS; i++) {
        if (name[i] < '0' || name[i] > '9') {
            return false;
        }
        value = value * 10 + static_cast<U32>(name[i] - '0');
    }
    if (name[8] != '.' || name[9] != 'b' || name[10] != 'i' || name[11] != 'n' || name[SEGMENT_NAME_CHARS] != '\0') {
        return false;
    }
    seq = value;
    return true;
}

}  // namespace Components
