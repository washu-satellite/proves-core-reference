// ======================================================================
// \title  DataRecorderComponentAc.hpp (host-test stub)
// \brief  Stand-in for the fpp-generated DataRecorder component base (Cycle M).
//
// Declares the base-class surface fixed by
// docs-site/dev-loop/cycles/cycle-m-plan/01-normative.md sections 1-4 and
// records every outgoing effect (telemetry writes, events with arguments,
// command responses, sendFileOut calls) into public members so host tests can
// assert on them, the role the autocoded TesterBase plays in a full F' UT.
//
// Properties of the real base reproduced here:
//   * tlmIn / evtIn are guarded input ports: tlmIn_handlerBase and
//     evtIn_handlerBase take the lock, call the handler, release the lock.
//     schedIn and every command are sync: their *Base entry points call the
//     handler with no lock taken.
//   * lock()/unLock() are the guarded-port mutex; lockDepth counts it.
//     Any telemetry write, event, command response or sendFileOut call made
//     with lockDepth > 0 sets portCallWhileLocked (review amendment 4).
//   * Event throttles (01-normative section 4): SegmentWriteFailed,
//     ConfigCorrupt and DirectoryScanFailed record at most 5 calls until the
//     matching *_ThrottleClear(); every call is still counted in *Calls.
//   * getTime() returns `now`, which tests set.
//   * sendFileOut returns `sendFileResponse` (default STATUS_OK).
//
// Enum and struct shapes mirror the generated F' 4.3 classes: enum T, member
// e, implicit conversion both ways; command handlers take enums as const&.
// ======================================================================

#ifndef UnitTestSupport_DataRecorderComponentAc_HPP
#define UnitTestSupport_DataRecorderComponentAc_HPP

#include <map>
#include <string>
#include <vector>

#include "../../../FpTypesStub.hpp"
#include "../../../Fw/Com/ComBuffer.hpp"
#include "../../../Fw/Time/Time.hpp"
#include "../../../Fw/Types/String.hpp"

namespace Svc {

//! Mirrors the generated Svc.SendFileStatus enum (FileDownlinkPorts.fpp:4-9).
class SendFileStatus {
  public:
    enum T { STATUS_OK = 0, STATUS_ERROR = 1, STATUS_INVALID = 2, STATUS_BUSY = 3 };
    SendFileStatus() : e(STATUS_OK) {}
    SendFileStatus(T e1) : e(e1) {}         // NOLINT(runtime/explicit) -- mirrors generated code
    operator T() const { return this->e; }  // NOLINT(runtime/explicit) -- enables switch/case
    bool operator==(T e1) const { return this->e == e1; }
    bool operator!=(T e1) const { return this->e != e1; }
    T e;
};

//! Mirrors the generated Svc.SendFileResponse struct (FileDownlinkPorts.fpp:12-15).
class SendFileResponse {
  public:
    SendFileResponse() : m_status(SendFileStatus::STATUS_OK), m_context(0) {}
    SendFileResponse(const SendFileStatus& status, U32 context) : m_status(status), m_context(context) {}
    SendFileStatus get_status() const { return this->m_status; }
    U32 get_context() const { return this->m_context; }
    void set_status(const SendFileStatus& status) { this->m_status = status; }
    void set_context(U32 context) { this->m_context = context; }

  private:
    SendFileStatus m_status;
    U32 m_context;
};

}  // namespace Svc

namespace Components {

//! enum RecorderStream : U8 { TLM = 0, EVT = 1 } (01-normative section 2).
class RecorderStream {
  public:
    enum T { TLM = 0, EVT = 1 };
    enum { NUM_CONSTANTS = 2 };
    RecorderStream() : e(TLM) {}
    RecorderStream(T e1) : e(e1) {}         // NOLINT(runtime/explicit) -- mirrors generated code
    operator T() const { return this->e; }  // NOLINT(runtime/explicit) -- enables switch/case
    bool operator==(T e1) const { return this->e == e1; }
    bool operator!=(T e1) const { return this->e != e1; }
    T e;
};

//! enum ConfigField : U8 (01-normative section 2).
class ConfigField {
  public:
    enum T { RING_SLOTS = 0, FLUSH_RECORDS = 1, FLUSH_INTERVAL_S = 2, CAPACITY_BYTES = 3, RETENTION_S = 4 };
    enum { NUM_CONSTANTS = 5 };
    ConfigField() : e(RING_SLOTS) {}
    ConfigField(T e1) : e(e1) {}            // NOLINT(runtime/explicit) -- mirrors generated code
    operator T() const { return this->e; }  // NOLINT(runtime/explicit) -- enables switch/case
    bool operator==(T e1) const { return this->e == e1; }
    bool operator!=(T e1) const { return this->e != e1; }
    T e;
};

//! enum DeleteReason : U8 (01-normative section 2).
class DeleteReason {
  public:
    enum T { CAPACITY = 0, AGE = 1, COMMAND = 2 };
    enum { NUM_CONSTANTS = 3 };
    DeleteReason() : e(CAPACITY) {}
    DeleteReason(T e1) : e(e1) {}           // NOLINT(runtime/explicit) -- mirrors generated code
    operator T() const { return this->e; }  // NOLINT(runtime/explicit) -- enables switch/case
    bool operator==(T e1) const { return this->e == e1; }
    bool operator!=(T e1) const { return this->e != e1; }
    T e;
};

class DataRecorderComponentBase {
  public:
    //! Event throttle of SegmentWriteFailed, ConfigCorrupt, DirectoryScanFailed.
    static constexpr U32 EVENT_THROTTLE = 5;

    //! Opcodes relative to the instance base, in declaration order (section 3).
    enum {
        OPCODE_SET_RING_SLOTS = 0x0,
        OPCODE_SET_CAPACITY = 0x1,
        OPCODE_SET_RETENTION_S = 0x2,
        OPCODE_SET_FLUSH = 0x3,
        OPCODE_SAVE_CONFIG = 0x4,
        OPCODE_LIST_SEGMENTS = 0x5,
        OPCODE_DELETE_SEGMENT = 0x6,
        OPCODE_CLOSE_SEGMENT = 0x7,
        OPCODE_DOWNLINK_NEWEST = 0x8
    };

    struct CmdResponseRecord {
        FwOpcodeType opCode;
        U32 cmdSeq;
        Fw::CmdResponse response;
    };
    struct StreamSeq {
        RecorderStream::T stream;
        U32 seq;
    };
    struct StreamStatus {
        RecorderStream::T stream;
        U32 status;
    };
    struct SegmentDeletedRecord {
        RecorderStream::T stream;
        U32 seq;
        DeleteReason::T reason;
    };
    struct ConfigRejectedRecord {
        ConfigField::T field;
        U32 value;
        U32 max;
    };
    struct SegmentInfoRecord {
        RecorderStream::T stream;
        U32 seq;
        U32 bytes;
        U32 openTimeS;
    };
    struct ConfigAppliedRecord {
        RecorderStream::T stream;
        ConfigField::T field;
        U32 value;
    };
    struct SendFileRecord {
        std::string source;
        std::string dest;
        U32 offset;
        U32 length;
    };

    explicit DataRecorderComponentBase(const char* const compName) : compName(compName) {}
    virtual ~DataRecorderComponentBase() {}

    // ---- handlers implemented by the component (private overrides there) ----
    virtual void tlmIn_handler(FwIndexType portNum, Fw::ComBuffer& data, U32 context) = 0;
    virtual void evtIn_handler(FwIndexType portNum, Fw::ComBuffer& data, U32 context) = 0;
    virtual void schedIn_handler(FwIndexType portNum, U32 context) = 0;

    virtual void SET_RING_SLOTS_cmdHandler(FwOpcodeType opCode,
                                           U32 cmdSeq,
                                           const Components::RecorderStream& stream,
                                           U8 slots) = 0;
    virtual void SET_CAPACITY_cmdHandler(FwOpcodeType opCode,
                                         U32 cmdSeq,
                                         const Components::RecorderStream& stream,
                                         U32 bytes) = 0;
    virtual void SET_RETENTION_S_cmdHandler(FwOpcodeType opCode,
                                            U32 cmdSeq,
                                            const Components::RecorderStream& stream,
                                            U32 seconds) = 0;
    virtual void SET_FLUSH_cmdHandler(FwOpcodeType opCode,
                                      U32 cmdSeq,
                                      const Components::RecorderStream& stream,
                                      U8 records,
                                      U16 intervalS) = 0;
    virtual void SAVE_CONFIG_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) = 0;
    virtual void LIST_SEGMENTS_cmdHandler(FwOpcodeType opCode,
                                          U32 cmdSeq,
                                          const Components::RecorderStream& stream,
                                          U32 fromSeq) = 0;
    virtual void DELETE_SEGMENT_cmdHandler(FwOpcodeType opCode,
                                           U32 cmdSeq,
                                           const Components::RecorderStream& stream,
                                           U32 seq) = 0;
    virtual void CLOSE_SEGMENT_cmdHandler(FwOpcodeType opCode,
                                          U32 cmdSeq,
                                          const Components::RecorderStream& stream) = 0;
    virtual void DOWNLINK_NEWEST_cmdHandler(FwOpcodeType opCode,
                                            U32 cmdSeq,
                                            const Components::RecorderStream& stream) = 0;

    // ---- port entry points, as the generated base invokes the handlers ----

    //! Guarded input port: lock, handler, unlock.
    void tlmIn_handlerBase(FwIndexType portNum, Fw::ComBuffer& data, U32 context) {
        this->lock();
        this->tlmIn_handler(portNum, data, context);
        this->unLock();
    }

    //! Guarded input port: lock, handler, unlock.
    void evtIn_handlerBase(FwIndexType portNum, Fw::ComBuffer& data, U32 context) {
        this->lock();
        this->evtIn_handler(portNum, data, context);
        this->unLock();
    }

    //! Sync input port: the handler runs with no lock taken.
    void schedIn_handlerBase(FwIndexType portNum, U32 context) { this->schedIn_handler(portNum, context); }

    // ---- test-controlled inputs ----
    Fw::Time now;                            //!< returned by getTime()
    Svc::SendFileResponse sendFileResponse;  //!< returned by sendFileOut_out
    bool sendFileOutConnected = true;        //!< isConnected_sendFileOut_OutputPort

    // ---- recorded outgoing effects, public for test inspection ----
    std::string compName;
    //! Telemetry history per channel name (TlmRecordsStored ... EvtWriteFailures).
    std::map<std::string, std::vector<U32>> tlm;
    std::vector<CmdResponseRecord> cmdResponses;
    std::vector<SendFileRecord> sendFileOutCalls;

    std::vector<StreamSeq> eventsSegmentOpened;
    std::vector<StreamStatus> eventsSegmentWriteFailed;  //!< throttled: at most 5 until cleared
    U32 segmentWriteFailedCalls = 0;
    std::vector<SegmentDeletedRecord> eventsSegmentDeleted;
    std::vector<ConfigRejectedRecord> eventsConfigRejected;
    std::vector<U8> eventsConfigCorrupt;  //!< throttled: at most 5 until cleared
    U32 configCorruptCalls = 0;
    std::vector<SegmentInfoRecord> eventsSegmentInfo;
    std::vector<StreamStatus> eventsDirectoryScanFailed;  //!< throttled: at most 5 until cleared
    U32 directoryScanFailedCalls = 0;
    std::vector<ConfigAppliedRecord> eventsConfigApplied;

    // ---- lock bookkeeping ----
    I32 lockDepth = 0;
    U32 lockCalls = 0;
    bool lockUnderflow = false;
    //! Set if a telemetry write, event, command response or sendFileOut call
    //! happened while the component lock was held.
    bool portCallWhileLocked = false;

    //! Events recorded (after throttling), for "no event" assertions.
    size_t totalEvents() const {
        return this->eventsSegmentOpened.size() + this->eventsSegmentWriteFailed.size() +
               this->eventsSegmentDeleted.size() + this->eventsConfigRejected.size() +
               this->eventsConfigCorrupt.size() + this->eventsSegmentInfo.size() +
               this->eventsDirectoryScanFailed.size() + this->eventsConfigApplied.size();
    }

    //! Telemetry writes recorded, over every channel.
    size_t totalTlmWrites() const {
        size_t n = 0;
        for (std::map<std::string, std::vector<U32>>::const_iterator it = this->tlm.begin(); it != this->tlm.end();
             ++it) {
            n += it->second.size();
        }
        return n;
    }

    //! Forget every recorded effect (not the lock bookkeeping or throttles).
    void clearHistory() {
        this->tlm.clear();
        this->cmdResponses.clear();
        this->sendFileOutCalls.clear();
        this->eventsSegmentOpened.clear();
        this->eventsSegmentWriteFailed.clear();
        this->eventsSegmentDeleted.clear();
        this->eventsConfigRejected.clear();
        this->eventsConfigCorrupt.clear();
        this->eventsSegmentInfo.clear();
        this->eventsDirectoryScanFailed.clear();
        this->eventsConfigApplied.clear();
    }

  protected:
    // ---- guarded-port mutex stand-in ----
    virtual void lock() {
        this->lockDepth++;
        this->lockCalls++;
    }
    virtual void unLock() {
        if (this->lockDepth == 0) {
            this->lockUnderflow = true;
        } else {
            this->lockDepth--;
        }
    }

    // ---- time ----
    Fw::Time getTime() { return this->now; }

    // ---- output port ----
    bool isConnected_sendFileOut_OutputPort(FwIndexType portNum) const {
        (void)portNum;
        return this->sendFileOutConnected;
    }
    Svc::SendFileResponse sendFileOut_out(FwIndexType portNum,
                                          const Fw::StringBase& sourceFileName,
                                          const Fw::StringBase& destFileName,
                                          U32 offset,
                                          U32 length) {
        (void)portNum;
        this->checkUnlocked();
        this->sendFileOutCalls.push_back(
            SendFileRecord{sourceFileName.toChar(), destFileName.toChar(), offset, length});
        return this->sendFileResponse;
    }

    // ---- command response ----
    void cmdResponse_out(FwOpcodeType opCode, U32 cmdSeq, const Fw::CmdResponse& response) {
        this->checkUnlocked();
        this->cmdResponses.push_back(CmdResponseRecord{opCode, cmdSeq, response});
    }

    // ---- telemetry (01-normative section 4: 7 per stream, all U32) ----
    void tlmWrite_TlmRecordsStored(U32 v, Fw::Time t = Fw::Time()) { this->record("TlmRecordsStored", v, t); }
    void tlmWrite_TlmRecordsOnDisk(U32 v, Fw::Time t = Fw::Time()) { this->record("TlmRecordsOnDisk", v, t); }
    void tlmWrite_TlmBytesOnDisk(U32 v, Fw::Time t = Fw::Time()) { this->record("TlmBytesOnDisk", v, t); }
    void tlmWrite_TlmSegmentsOnDisk(U32 v, Fw::Time t = Fw::Time()) { this->record("TlmSegmentsOnDisk", v, t); }
    void tlmWrite_TlmOldestRecordAgeS(U32 v, Fw::Time t = Fw::Time()) { this->record("TlmOldestRecordAgeS", v, t); }
    void tlmWrite_TlmRingDropped(U32 v, Fw::Time t = Fw::Time()) { this->record("TlmRingDropped", v, t); }
    void tlmWrite_TlmWriteFailures(U32 v, Fw::Time t = Fw::Time()) { this->record("TlmWriteFailures", v, t); }
    void tlmWrite_EvtRecordsStored(U32 v, Fw::Time t = Fw::Time()) { this->record("EvtRecordsStored", v, t); }
    void tlmWrite_EvtRecordsOnDisk(U32 v, Fw::Time t = Fw::Time()) { this->record("EvtRecordsOnDisk", v, t); }
    void tlmWrite_EvtBytesOnDisk(U32 v, Fw::Time t = Fw::Time()) { this->record("EvtBytesOnDisk", v, t); }
    void tlmWrite_EvtSegmentsOnDisk(U32 v, Fw::Time t = Fw::Time()) { this->record("EvtSegmentsOnDisk", v, t); }
    void tlmWrite_EvtOldestRecordAgeS(U32 v, Fw::Time t = Fw::Time()) { this->record("EvtOldestRecordAgeS", v, t); }
    void tlmWrite_EvtRingDropped(U32 v, Fw::Time t = Fw::Time()) { this->record("EvtRingDropped", v, t); }
    void tlmWrite_EvtWriteFailures(U32 v, Fw::Time t = Fw::Time()) { this->record("EvtWriteFailures", v, t); }

    // ---- events (01-normative section 4) ----
    void log_ACTIVITY_LO_SegmentOpened(const RecorderStream& stream, U32 seq) {
        this->checkUnlocked();
        this->eventsSegmentOpened.push_back(StreamSeq{stream.e, seq});
    }
    void log_WARNING_HI_SegmentWriteFailed(const RecorderStream& stream, U32 status) {
        this->checkUnlocked();
        this->segmentWriteFailedCalls++;
        if (this->m_segmentWriteFailedThrottle < EVENT_THROTTLE) {
            this->m_segmentWriteFailedThrottle++;
            this->eventsSegmentWriteFailed.push_back(StreamStatus{stream.e, status});
        }
    }
    void log_WARNING_HI_SegmentWriteFailed_ThrottleClear() { this->m_segmentWriteFailedThrottle = 0; }
    void log_ACTIVITY_LO_SegmentDeleted(const RecorderStream& stream, U32 seq, const DeleteReason& reason) {
        this->checkUnlocked();
        this->eventsSegmentDeleted.push_back(SegmentDeletedRecord{stream.e, seq, reason.e});
    }
    void log_WARNING_LO_ConfigRejected(const ConfigField& field, U32 value, U32 max) {
        this->checkUnlocked();
        this->eventsConfigRejected.push_back(ConfigRejectedRecord{field.e, value, max});
    }
    void log_WARNING_HI_ConfigCorrupt(U8 status) {
        this->checkUnlocked();
        this->configCorruptCalls++;
        if (this->m_configCorruptThrottle < EVENT_THROTTLE) {
            this->m_configCorruptThrottle++;
            this->eventsConfigCorrupt.push_back(status);
        }
    }
    void log_WARNING_HI_ConfigCorrupt_ThrottleClear() { this->m_configCorruptThrottle = 0; }
    void log_ACTIVITY_HI_SegmentInfo(const RecorderStream& stream, U32 seq, U32 bytes, U32 openTimeS) {
        this->checkUnlocked();
        this->eventsSegmentInfo.push_back(SegmentInfoRecord{stream.e, seq, bytes, openTimeS});
    }
    void log_WARNING_HI_DirectoryScanFailed(const RecorderStream& stream, U32 status) {
        this->checkUnlocked();
        this->directoryScanFailedCalls++;
        if (this->m_directoryScanFailedThrottle < EVENT_THROTTLE) {
            this->m_directoryScanFailedThrottle++;
            this->eventsDirectoryScanFailed.push_back(StreamStatus{stream.e, status});
        }
    }
    void log_WARNING_HI_DirectoryScanFailed_ThrottleClear() { this->m_directoryScanFailedThrottle = 0; }
    void log_ACTIVITY_HI_ConfigApplied(const RecorderStream& stream, const ConfigField& field, U32 value) {
        this->checkUnlocked();
        this->eventsConfigApplied.push_back(ConfigAppliedRecord{stream.e, field.e, value});
    }

  private:
    void checkUnlocked() {
        if (this->lockDepth > 0) {
            this->portCallWhileLocked = true;
        }
    }
    void record(const char* channel, U32 value, const Fw::Time& t) {
        (void)t;
        this->checkUnlocked();
        this->tlm[channel].push_back(value);
    }

    U32 m_segmentWriteFailedThrottle = 0;
    U32 m_configCorruptThrottle = 0;
    U32 m_directoryScanFailedThrottle = 0;
};

}  // namespace Components

#endif
