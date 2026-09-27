// ======================================================================
// \title  DataRecorder.hpp
// \brief  hpp file for the DataRecorder component implementation class
//
// Records the TLM and EVT packet streams into a static RAM ring per stream
// (tlmIn / evtIn: copy only, no file I/O) and, on the 1 Hz schedIn, runs the
// boot scan, the flush to CRC-framed segment files under /rec/, segment
// rotation and retention. Interface, formats and rules:
// docs-site/dev-loop/cycles/cycle-m-plan/01-normative.md; overview in
// docs/sdd.md.
//
// Locking: the component lock (guarded-port mutex) protects the rings only
// and is held for copies, never across an Os call, a port call, an event or a
// telemetry write. m_fileLock serialises all file work and configuration
// between schedIn and the commands; order is m_fileLock, then component lock.
// ======================================================================

#ifndef Components_DataRecorder_HPP
#define Components_DataRecorder_HPP

#include "Os/Directory.hpp"
#include "Os/Mutex.hpp"
#include "PROVESFlightControllerReference/Components/DataRecorder/DataRecorderCfg.hpp"
#include "PROVESFlightControllerReference/Components/DataRecorder/DataRecorderComponentAc.hpp"
#include "PROVESFlightControllerReference/Components/DataRecorder/PacketRing.hpp"

namespace Components {

class DataRecorder : public DataRecorderComponentBase {
  public:
    //! Construct DataRecorder object; no configure call is needed before the first schedIn
    explicit DataRecorder(const char* const compName);

    //! Destroy DataRecorder object
    ~DataRecorder();

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for typed input ports
    // ----------------------------------------------------------------------

    //! Telemetry packet: copy into the TLM ring (called under the component lock)
    void tlmIn_handler(FwIndexType portNum, Fw::ComBuffer& data, U32 context) override;

    //! Event packet: copy into the EVT ring (called under the component lock)
    void evtIn_handler(FwIndexType portNum, Fw::ComBuffer& data, U32 context) override;

    //! 1 Hz tick: scan, flush, rotation, retention, telemetry
    void schedIn_handler(FwIndexType portNum, U32 context) override;

    // ----------------------------------------------------------------------
    // Handler implementations for commands
    // ----------------------------------------------------------------------

    void SET_RING_SLOTS_cmdHandler(FwOpcodeType opCode,
                                   U32 cmdSeq,
                                   const Components::RecorderStream& stream,
                                   U8 slots) override;
    void SET_CAPACITY_cmdHandler(FwOpcodeType opCode,
                                 U32 cmdSeq,
                                 const Components::RecorderStream& stream,
                                 U32 bytes) override;
    void SET_RETENTION_S_cmdHandler(FwOpcodeType opCode,
                                    U32 cmdSeq,
                                    const Components::RecorderStream& stream,
                                    U32 seconds) override;
    void SET_FLUSH_cmdHandler(FwOpcodeType opCode,
                              U32 cmdSeq,
                              const Components::RecorderStream& stream,
                              U8 records,
                              U16 intervalS) override;
    void SAVE_CONFIG_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void LIST_SEGMENTS_cmdHandler(FwOpcodeType opCode,
                                  U32 cmdSeq,
                                  const Components::RecorderStream& stream,
                                  U32 fromSeq) override;
    void DELETE_SEGMENT_cmdHandler(FwOpcodeType opCode,
                                   U32 cmdSeq,
                                   const Components::RecorderStream& stream,
                                   U32 seq) override;
    void CLOSE_SEGMENT_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, const Components::RecorderStream& stream) override;
    void DOWNLINK_NEWEST_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, const Components::RecorderStream& stream) override;

    // ----------------------------------------------------------------------
    // Types
    // ----------------------------------------------------------------------

    //! Commandable per-stream configuration (the 01-normative 5.5 payload fields)
    struct StreamConfig {
        U8 ringSlots;
        U8 flushRecords;
        U16 flushIntervalS;
        U32 capacityBytes;
        U32 retentionS;
    };

    //! Boot-scan state of one stream directory
    enum ScanState : U8 {
        SCAN_IDLE,     //!< not started, or restarting after a failure
        SCAN_READING,  //!< directory open, entries being read across ticks
        SCAN_READY     //!< counts and oldest/second known; file work allowed
    };

    //! Search for the oldest / second-oldest present segment by exists() probes
    enum FindState : U8 { FIND_DONE, FIND_OLDEST, FIND_SECOND };

    //! What retention knows about one segment file
    struct SegmentRef {
        bool present;
        bool hdrKnown;
        bool hdrValid;
        U32 seq;
        U32 openS;
    };

    //! Result of one flush batch
    enum WriteResult : U8 { WRITE_NOTHING, WRITE_OK, WRITE_FAILED };

    //! Result of reading a segment header
    enum HeaderRead : U8 { HEADER_VALID, HEADER_INVALID, HEADER_UNREADABLE };

    //! Everything the recorder keeps for one stream
    struct StreamState {
        //! Records not yet on disk (component lock)
        PacketRing<DataRecorderCfg::RING_SLOTS_MAX, DataRecorderCfg::SLOT_BYTES> ring;
        // ---- the rest is touched only under m_fileLock ----
        StreamConfig cfg;
        ScanState scan;
        U32 scanCount;
        U64 scanBytes;
        U32 scanMin1;
        U32 scanMin2;
        U32 scanMax;
        U32 segmentsOnDisk;
        U64 bytesOnDisk;
        U32 nextSeq;
        bool segOpen;
        U32 segSeq;
        U32 segBytes;
        U32 segTicks;
        SegmentRef oldest;
        SegmentRef second;
        FindState find;
        U32 findCursor;
        U32 recordsOnDisk;
        U32 writeFailures;
        U32 ticksSinceFlush;
    };

    // ----------------------------------------------------------------------
    // Helpers (all but record() run under m_fileLock)
    // ----------------------------------------------------------------------

    void record(U32 s, Fw::ComBuffer& data);
    void loadConfigOnce();
    void applyConfig(U32 s, const StreamConfig& cfg);
    void scanStep(U32 s);
    void scanFailed(U32 s, U32 status);
    void finishScan(U32 s);
    void flushStep(U32 s, U32 now);
    void rotationStep(U32 s);
    void retentionStep(U32 s, U32 now);
    void findStep(U32 s);
    void loadHeader(U32 s, SegmentRef& ref);
    HeaderRead readHeader(const char* path, U32& openS);
    void addSegment(U32 s, U32 seq, U64 size, bool hdrKnown, U32 openS);
    void forgetSegment(U32 s, U32 seq);
    WriteResult writeBatch(U32 s, U32 now);
    Fw::CmdResponse closeSegment(U32 s, U32 now);
    void writeTelemetry(U32 now);
    Fw::CmdResponse rejected(ConfigField::T field, U32 value, U32 max);
    bool scanning(U32 s) const;
    static bool validStream(const Components::RecorderStream& stream, U32& s);
    static bool validConfig(U32 s, const StreamConfig& cfg);
    static void segmentPath(U32 s, U32 seq, char* out);
    static bool parseSegmentName(const char* name, U32& seq);

    // ----------------------------------------------------------------------
    // Members (all statically sized)
    // ----------------------------------------------------------------------

    StreamState m_streams[DataRecorderCfg::NUM_STREAMS];
    Os::Directory m_dir[DataRecorderCfg::NUM_STREAMS];  //!< boot-scan directory, open across ticks while reading
    U8 m_stage[DataRecorderCfg::STAGE_BYTES];           //!< one flush batch, framed
    Os::Mutex m_fileLock;                               //!< serialises file work and configuration
    bool m_configLoaded;                                //!< /rec/config.bin applied (or defaults kept)
    bool m_started;                                     //!< first schedIn has run
};

}  // namespace Components

#endif
