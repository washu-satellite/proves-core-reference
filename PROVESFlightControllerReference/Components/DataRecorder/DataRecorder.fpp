module Components {

    @ Stream a record belongs to; the segment header stream byte (A9-2 appends BURST = 2)
    enum RecorderStream : U8 {
        TLM = 0
        EVT = 1
    }

    @ Configuration field named in ConfigRejected / ConfigApplied
    enum ConfigField : U8 {
        RING_SLOTS = 0
        FLUSH_RECORDS = 1
        FLUSH_INTERVAL_S = 2
        CAPACITY_BYTES = 3
        RETENTION_S = 4
    }

    @ Why a segment was deleted
    enum DeleteReason : U8 {
        CAPACITY = 0
        AGE = 1
        COMMAND = 2
    }

    @ Records the telemetry and event packet streams into a static RAM ring per
    @ stream and flushes them at 1 Hz to CRC-framed segment files under /rec/,
    @ with retention by capacity and age and retrieval through file downlink.
    @ Interface and file formats: docs-site/dev-loop/cycles/cycle-m-plan/01-normative.md.
    passive component DataRecorder {

        ###############################################################################
        # Inputs and outputs                                                          #
        ###############################################################################

        @ Telemetry packets (stream TLM); copied into the ring, no file I/O
        guarded input port tlmIn: Fw.Com

        @ Event packets (stream EVT); copied into the ring, no file I/O
        guarded input port evtIn: Fw.Com

        @ 1 Hz tick: boot scan, flush, rotation, retention, telemetry
        sync input port schedIn: Svc.Sched

        @ File downlink request, used only by DOWNLINK_NEWEST
        output port sendFileOut: Svc.SendFileRequest

        ###############################################################################
        # Commands (opcodes base + 0..8, in this order)                               #
        ###############################################################################

        @ Set the usable ring slots of a stream (1..32, >= its flush count); shrinks at once
        sync command SET_RING_SLOTS(
            stream: RecorderStream
            slots: U8
        )

        @ Set a stream's disk capacity in bytes (SEGMENT_MAX_BYTES..free-space limit)
        sync command SET_CAPACITY(
            stream: RecorderStream
            bytes: U32
        )

        @ Set a stream's retention in seconds (60..2592000)
        sync command SET_RETENTION_S(
            stream: RecorderStream
            seconds: U32
        )

        @ Set a stream's flush count (1..ring slots) and interval in seconds (1..3600)
        sync command SET_FLUSH(
            stream: RecorderStream
            records: U8
            intervalS: U16
        )

        @ Persist the configuration of both streams to /rec/config.bin
        sync command SAVE_CONFIG()

        @ Emit one SegmentInfo per segment of the stream from fromSeq on (at most 16)
        sync command LIST_SEGMENTS(
            stream: RecorderStream
            fromSeq: U32
        )

        @ Delete one closed segment of the stream
        sync command DELETE_SEGMENT(
            stream: RecorderStream
            seq: U32
        )

        @ Write the stream's ring out and close its open segment
        sync command CLOSE_SEGMENT(
            stream: RecorderStream
        )

        @ Close the stream's segment and downlink the newest segment file
        sync command DOWNLINK_NEWEST(
            stream: RecorderStream
        )

        ###############################################################################
        # Telemetry (7 per stream, all written on every tick)                        #
        ###############################################################################

        @ TLM records accepted into the ring since boot
        telemetry TlmRecordsStored: U32
        @ TLM records written to segments since boot
        telemetry TlmRecordsOnDisk: U32
        @ Sum of the TLM segment file sizes
        telemetry TlmBytesOnDisk: U32
        @ TLM segment files on disk
        telemetry TlmSegmentsOnDisk: U32
        @ Seconds since the oldest TLM segment was opened (0 when none or invalid)
        telemetry TlmOldestRecordAgeS: U32
        @ TLM records dropped from a full ring (cumulative)
        telemetry TlmRingDropped: U32
        @ Failed TLM file operations (cumulative)
        telemetry TlmWriteFailures: U32

        @ EVT records accepted into the ring since boot
        telemetry EvtRecordsStored: U32
        @ EVT records written to segments since boot
        telemetry EvtRecordsOnDisk: U32
        @ Sum of the EVT segment file sizes
        telemetry EvtBytesOnDisk: U32
        @ EVT segment files on disk
        telemetry EvtSegmentsOnDisk: U32
        @ Seconds since the oldest EVT segment was opened (0 when none or invalid)
        telemetry EvtOldestRecordAgeS: U32
        @ EVT records dropped from a full ring (cumulative)
        telemetry EvtRingDropped: U32
        @ Failed EVT file operations (cumulative)
        telemetry EvtWriteFailures: U32

        ###############################################################################
        # Events                                                                      #
        ###############################################################################

        @ A new segment file was created by its first successful write
        event SegmentOpened(stream: RecorderStream, seq: U32) \
            severity activity low \
            format "{} segment {} opened"

        @ A segment write, flush or delete failed (status: Os status ordinal)
        event SegmentWriteFailed(stream: RecorderStream, status: U32) \
            severity warning high \
            format "{} segment file operation failed, status {}" \
            throttle 5

        @ A segment was deleted
        event SegmentDeleted(stream: RecorderStream, seq: U32, reason: DeleteReason) \
            severity activity low \
            format "{} segment {} deleted ({})"

        @ A configuration command was rejected
        event ConfigRejected(field: ConfigField, value: U32, max: U32) \
            severity warning low \
            format "{} value {} rejected (limit {})"

        @ /rec/config.bin is invalid (PersistedRecord status, 255 = bad payload); defaults in force
        event ConfigCorrupt(status: U8) \
            severity warning high \
            format "Recorder configuration corrupt (status {}); defaults in force" \
            throttle 5

        @ One segment listed by LIST_SEGMENTS or sent by DOWNLINK_NEWEST
        event SegmentInfo(stream: RecorderStream, seq: U32, bytes: U32, openTimeS: U32) \
            severity activity high \
            format "{} segment {}: {} bytes, opened at {} s"

        @ The boot scan of a stream directory failed; it restarts next tick
        event DirectoryScanFailed(stream: RecorderStream, status: U32) \
            severity warning high \
            format "{} directory scan failed, status {}" \
            throttle 5

        @ A configuration field was accepted (in force from the next tick)
        event ConfigApplied(stream: RecorderStream, field: ConfigField, value: U32) \
            severity activity high \
            format "{} {} set to {}"

        ###############################################################################
        # Standard AC Ports: Required for Channels, Events, and Commands              #
        ###############################################################################
        @ Port for requesting the current time
        time get port timeCaller

        @ Port for sending command registrations
        command reg port cmdRegOut

        @ Port for receiving commands
        command recv port cmdIn

        @ Port for sending command responses
        command resp port cmdResponseOut

        @ Port for sending textual representation of events
        text event port logTextOut

        @ Port for sending events to downlink
        event port logOut

        @ Port for sending telemetry channels to downlink
        telemetry port tlmOut
    }
}
