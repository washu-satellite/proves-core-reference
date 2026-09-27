// ======================================================================
// \title  test_DataRecorder_Component.cpp
// \brief  Component-level host tests for DataRecorder (Cycle M row A8-2).
//
// Compiles the real DataRecorder.cpp against the stubs in support/: the
// recording DataRecorderComponentAc.hpp (guarded tlmIn/evtIn take the lock;
// every telemetry write, event, command response and sendFileOut call is
// recorded; portCallWhileLocked flags one made under the lock), the in-memory
// Os::File / Os::FileSystem / Os::Directory with operation counters and fault
// injection, and Fw::ComBuffer.
//
// Written from docs-site/dev-loop/cycles/cycle-m-plan/01-normative.md
// sections 1-9 and 12 and the pass criteria of DataRecorder-3..6, -8..-11,
// DH-L2-03 and DH-L2-04 (02-requirements.md). Only results are asserted: the
// files the recorder leaves in the fake filesystem (decoded with the codec),
// the recorded telemetry/events/responses, and the fake's counters.
//
// Every test also checks the harm-table rows of section 12 in TearDown: no
// Os call and no port call while the component lock is held (amendment 4),
// no Os::File open when a handler returns, and no path outside /rec/ created
// or changed.
// ======================================================================

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "Os/Directory.hpp"
#include "Os/File.hpp"
#include "Os/FileSystem.hpp"
#include "PROVESFlightControllerReference/Components/DataRecorder/DataRecorder.hpp"
#include "PROVESFlightControllerReference/Components/DataRecorder/SegmentCodec.hpp"
#include "PROVESFlightControllerReference/Components/PersistedRecord/PersistedRecordCodec.hpp"

namespace {

using Components::ConfigField;
using Components::DataRecorder;
using Components::DeleteReason;
using Components::RecorderStream;
using Base = Components::DataRecorderComponentBase;
using Bytes = std::vector<U8>;
using Rsp = Fw::CmdResponse;

namespace SC = Components::SegmentCodec;
namespace PR = Components::PersistedRecord;

// ---- section 6 constants (results, not the implementation's names) ----
constexpr U32 TLM_FLUSH_RECORDS = 16;
constexpr U32 EVT_FLUSH_RECORDS = 8;
constexpr U32 EVT_FLUSH_INTERVAL_S = 10;
constexpr U32 TLM_SEGMENT_MAX_BYTES = 32768;
constexpr U32 EVT_SEGMENT_MAX_BYTES = 16384;
constexpr U32 SEGMENT_MAX_S = 3600;
constexpr U32 TLM_CAPACITY = 8388608;
constexpr U32 EVT_CAPACITY = 2097152;
constexpr U64 RESERVE_BYTES = 16777216;
constexpr U64 TOTAL_BYTES = 4294967296ULL;  // the fake's default totalBytes
constexpr U32 STAGE_BYTES = 4096;
constexpr U32 SCAN_BUDGET = 32;
constexpr U32 RETENTION_MIN = 60;
constexpr U32 RETENTION_MAX = 2592000;
constexpr U32 RING_SLOTS_MAX = 32;
constexpr U32 FLUSH_INTERVAL_MAX = 3600;

constexpr U32 NOW0 = 1000000;  // flight seconds at boot in every test
constexpr FwOpcodeType OPCODE = 0x10080000;

const char* const CONFIG_PATH = "/rec/config.bin";
const U8 CONFIG_MAGIC[4] = {0x44, 0x52, 0x43, 0x31};  // "DRC1"

//! Section 5.5: the defaults' 26-byte payload.
const Bytes DEFAULT_CONFIG = {0x01, 0x02, 0x20, 0x10, 0x3C, 0x00, 0x00, 0x00, 0x80, 0x00, 0x80, 0x3A, 0x09,
                              0x00, 0x20, 0x08, 0x0A, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x8D, 0x27, 0x00};

const char* const CHANNELS[14] = {"TlmRecordsStored",    "TlmRecordsOnDisk", "TlmBytesOnDisk",    "TlmSegmentsOnDisk",
                                  "TlmOldestRecordAgeS", "TlmRingDropped",   "TlmWriteFailures",  "EvtRecordsStored",
                                  "EvtRecordsOnDisk",    "EvtBytesOnDisk",   "EvtSegmentsOnDisk", "EvtOldestRecordAgeS",
                                  "EvtRingDropped",      "EvtWriteFailures"};

// ----------------------------------------------------------------------
// Byte helpers
// ----------------------------------------------------------------------

//! A recognisable payload: 4-byte big-endian tag, then a pattern, total len.
Bytes payload(U32 tag, U16 len = 20) {
    Bytes out(len);
    for (U16 i = 0; i < len; i++) {
        out[i] = static_cast<U8>((tag * 29u + i * 7u + 3u) & 0xFFu);
    }
    for (U16 i = 0; i < 4 && i < len; i++) {
        out[i] = static_cast<U8>((tag >> (8 * (3 - i))) & 0xFFu);
    }
    return out;
}

void putLe16(Bytes& b, U16 v) {
    b.push_back(static_cast<U8>(v & 0xFF));
    b.push_back(static_cast<U8>(v >> 8));
}

void putLe32(Bytes& b, U32 v) {
    for (int i = 0; i < 4; i++) {
        b.push_back(static_cast<U8>((v >> (8 * i)) & 0xFF));
    }
}

struct StreamConfig {
    U8 ringSlots;
    U8 flushRecords;
    U16 flushIntervalS;
    U32 capacityBytes;
    U32 retentionS;
};

//! Section 5.5 payload for the given per-stream values.
Bytes configPayload(const StreamConfig& tlm, const StreamConfig& evt, U8 layout = 1, U8 streamCount = 2) {
    Bytes b = {layout, streamCount};
    for (const StreamConfig* s : {&tlm, &evt}) {
        b.push_back(s->ringSlots);
        b.push_back(s->flushRecords);
        putLe16(b, s->flushIntervalS);
        putLe32(b, s->capacityBytes);
        putLe32(b, s->retentionS);
    }
    return b;
}

const StreamConfig TLM_DEFAULTS = {32, 16, 60, TLM_CAPACITY, 604800};
const StreamConfig EVT_DEFAULTS = {32, 8, 10, EVT_CAPACITY, 2592000};

//! A whole PersistedRecord file (magic DRC1) around the payload.
Bytes configRecord(const Bytes& payloadBytes) {
    U8 buf[PR::MAX_RECORD_SIZE];
    const uint32_t n =
        PR::encode(CONFIG_MAGIC, payloadBytes.data(), static_cast<uint16_t>(payloadBytes.size()), buf, sizeof(buf));
    EXPECT_GT(n, 0u);
    return Bytes(buf, buf + n);
}

const char* streamDir(RecorderStream::T s) {
    return (s == RecorderStream::TLM) ? "/rec/tlm" : "/rec/evt";
}

std::string segmentPath(RecorderStream::T s, U32 seq) {
    char name[32];
    std::snprintf(name, sizeof(name), "/%08u.bin", static_cast<unsigned>(seq));
    return std::string(streamDir(s)) + name;
}

//! seq of a direct child "<dir>/NNNNNNNN.bin", or -1.
long long segmentSeqOf(RecorderStream::T s, const std::string& path) {
    const std::string prefix = std::string(streamDir(s)) + "/";
    if (path.size() != prefix.size() + 12 || path.compare(0, prefix.size(), prefix) != 0) {
        return -1;
    }
    const std::string name = path.substr(prefix.size());
    if (name.compare(8, 4, ".bin") != 0) {
        return -1;
    }
    long long v = 0;
    for (int i = 0; i < 8; i++) {
        if (name[i] < '0' || name[i] > '9') {
            return -1;
        }
        v = v * 10 + (name[i] - '0');
    }
    return v;
}

Bytes segmentHeader(RecorderStream::T s, U32 openSeconds) {
    SC::Header h;
    h.stream = static_cast<uint8_t>(s);
    h.bootCount = 0xFFFF;
    h.openSeconds = openSeconds;
    h.openUseconds = 0;
    Bytes out(SC::HEADER_SIZE);
    EXPECT_EQ(SC::encodeHeader(h, out.data(), static_cast<uint32_t>(out.size())), SC::HEADER_SIZE);
    return out;
}

struct Decoded {
    bool headerOk = false;
    SC::Header header;
    std::vector<Bytes> records;
    SC::Status stop = SC::Status::OK;
};

Decoded decodeBytes(const Bytes& b) {
    Decoded d;
    d.headerOk = (SC::decodeHeader(b.data(), static_cast<uint32_t>(b.size()), d.header) == SC::Status::OK);
    if (!d.headerOk) {
        return d;
    }
    uint32_t off = SC::HEADER_SIZE;
    while (true) {
        const uint8_t* p = nullptr;
        uint16_t len = 0;
        uint32_t consumed = 0;
        const SC::Status st = SC::decodeRecord(b.data() + off, static_cast<uint32_t>(b.size()) - off, p, len, consumed);
        if (st != SC::Status::OK || consumed == 0) {
            d.stop = st;
            return d;
        }
        d.records.push_back(Bytes(p, p + len));
        off += consumed;
    }
}

// ----------------------------------------------------------------------
// Fixture
// ----------------------------------------------------------------------

class DataRecorderTest : public ::testing::Test {
  public:
    std::unique_ptr<DataRecorder> rec;
    bool osCallWhileLocked = false;
    std::string osCallWhileLockedKey;

    // Paths outside /rec/ seeded before every test; they must be untouched.
    std::map<std::string, Bytes> outsideFiles;

    static Os::Test::FileSystemState& fs() { return Os::Test::fileSystem(); }

    void SetUp() override {
        this->freshFileSystem();
        this->boot();
    }

    void TearDown() override {
        // Harm table (01-normative section 12) and review amendment 4.
        EXPECT_FALSE(this->osCallWhileLocked) << "Os call '" << this->osCallWhileLockedKey << "' under the lock";
        if (this->rec) {
            EXPECT_FALSE(this->rec->portCallWhileLocked) << "port call, event or telemetry under the lock";
            EXPECT_EQ(this->rec->lockDepth, 0);
            EXPECT_FALSE(this->rec->lockUnderflow);
        }
        EXPECT_EQ(fs().openHandles, 0) << "an Os::File is still open";
        for (const auto& kv : fs().files) {
            if (kv.first.compare(0, 5, "/rec/") == 0) {
                continue;
            }
            const auto it = this->outsideFiles.find(kv.first);
            ASSERT_NE(it, this->outsideFiles.end()) << "created outside /rec/: " << kv.first;
            EXPECT_EQ(kv.second, it->second) << "changed outside /rec/: " << kv.first;
        }
        for (const auto& kv : this->outsideFiles) {
            EXPECT_EQ(fs().files.count(kv.first), 1u) << "removed outside /rec/: " << kv.first;
        }
        for (const std::string& d : fs().directories) {
            EXPECT_TRUE(d == "/other" || d == "/rec" || d.compare(0, 5, "/rec/") == 0)
                << "directory created outside /rec/: " << d;
        }
    }

    //! Empty fake filesystem plus the untouchable outside files and the lock hook.
    void freshFileSystem() {
        this->rec.reset();
        Os::Test::resetFileSystem();
        this->outsideFiles.clear();
        this->outsideFiles["/prmDb.dat"] = Bytes{0x01, 0x02, 0x03, 0x04};
        this->outsideFiles["/other/keep.bin"] = Bytes{0x09, 0x09};
        for (const auto& kv : this->outsideFiles) {
            fs().files[kv.first] = kv.second;
        }
        fs().directories.insert("/other");
        fs().onOperation = [this](const std::string& key) {
            if (this->rec && this->rec->lockDepth > 0) {
                this->osCallWhileLocked = true;
                this->osCallWhileLockedKey = key;
            }
        };
    }

    //! A new instance over the current fake filesystem (a reboot).
    void boot(U32 nowS = NOW0) {
        this->rec.reset();
        this->rec.reset(new DataRecorder("dataRecorder"));
        this->setNow(nowS);
    }

    void setNow(U32 s) { this->rec->now = Fw::Time(TimeBase::TB_WORKSTATION_TIME, 0, s, 0); }
    U32 nowS() const { return this->rec->now.getSeconds(); }

    Base& base() { return *this->rec; }

    void tick(U32 n = 1) {
        for (U32 i = 0; i < n; i++) {
            this->base().schedIn_handlerBase(0, 0);
            ASSERT_EQ(fs().openHandles, 0) << "file left open after schedIn";
        }
    }

    void push(RecorderStream::T s, const Bytes& p) {
        Fw::ComBuffer buf(p.data(), p.size());
        if (s == RecorderStream::TLM) {
            this->base().tlmIn_handlerBase(0, buf, 0);
        } else {
            this->base().evtIn_handlerBase(0, buf, 0);
        }
        ASSERT_EQ(fs().openHandles, 0) << "file left open after an input handler";
    }

    //! Push count payloads tagged first..first+count-1; returns them.
    std::vector<Bytes> pushMany(RecorderStream::T s, U32 first, U32 count, U16 len = 20) {
        std::vector<Bytes> out;
        for (U32 i = 0; i < count; i++) {
            out.push_back(payload(first + i, len));
            this->push(s, out.back());
        }
        return out;
    }

    //! Run one command and return its single response.
    Rsp::T command(const std::function<void()>& send) {
        const size_t before = this->rec->cmdResponses.size();
        send();
        EXPECT_EQ(fs().openHandles, 0) << "file left open after a command";
        EXPECT_EQ(this->rec->cmdResponses.size(), before + 1) << "expected exactly one command response";
        if (this->rec->cmdResponses.size() != before + 1) {
            return Rsp::EXECUTION_ERROR;
        }
        return this->rec->cmdResponses.back().response.value();
    }

    Rsp::T setRingSlots(RecorderStream::T s, U8 slots) {
        return this->command([&] { this->base().SET_RING_SLOTS_cmdHandler(OPCODE + 0, 1, s, slots); });
    }
    Rsp::T setCapacity(RecorderStream::T s, U32 bytes) {
        return this->command([&] { this->base().SET_CAPACITY_cmdHandler(OPCODE + 1, 1, s, bytes); });
    }
    Rsp::T setRetention(RecorderStream::T s, U32 seconds) {
        return this->command([&] { this->base().SET_RETENTION_S_cmdHandler(OPCODE + 2, 1, s, seconds); });
    }
    Rsp::T setFlush(RecorderStream::T s, U8 records, U16 intervalS) {
        return this->command([&] { this->base().SET_FLUSH_cmdHandler(OPCODE + 3, 1, s, records, intervalS); });
    }
    Rsp::T saveConfig() {
        return this->command([&] { this->base().SAVE_CONFIG_cmdHandler(OPCODE + 4, 1); });
    }
    Rsp::T listSegments(RecorderStream::T s, U32 fromSeq) {
        return this->command([&] { this->base().LIST_SEGMENTS_cmdHandler(OPCODE + 5, 1, s, fromSeq); });
    }
    Rsp::T deleteSegment(RecorderStream::T s, U32 seq) {
        return this->command([&] { this->base().DELETE_SEGMENT_cmdHandler(OPCODE + 6, 1, s, seq); });
    }
    Rsp::T closeSegment(RecorderStream::T s) {
        return this->command([&] { this->base().CLOSE_SEGMENT_cmdHandler(OPCODE + 7, 1, s); });
    }
    Rsp::T downlinkNewest(RecorderStream::T s) {
        return this->command([&] { this->base().DOWNLINK_NEWEST_cmdHandler(OPCODE + 8, 1, s); });
    }

    //! True while the stream's boot scan is incomplete (LIST_SEGMENTS answers BUSY).
    //! fromSeq = 0xFFFFFFFF lists nothing, and the probe's own response is dropped.
    bool scanning(RecorderStream::T s) {
        const size_t events = this->rec->eventsSegmentInfo.size();
        const Rsp::T r = this->listSegments(s, 0xFFFFFFFFu);
        this->rec->cmdResponses.pop_back();
        EXPECT_EQ(this->rec->eventsSegmentInfo.size(), events) << "LIST_SEGMENTS from 0xFFFFFFFF listed a segment";
        return r == Rsp::BUSY;
    }

    //! Tick until both scans are complete; returns the ticks taken.
    U32 tickUntilScanned(U32 maxTicks = 50) {
        for (U32 t = 1; t <= maxTicks; t++) {
            this->tick();
            if (!this->scanning(RecorderStream::TLM) && !this->scanning(RecorderStream::EVT)) {
                return t;
            }
        }
        ADD_FAILURE() << "boot scan did not complete in " << maxTicks << " ticks";
        return maxTicks;
    }

    U32 lastTlm(const char* channel) {
        const auto it = this->rec->tlm.find(channel);
        if (it == this->rec->tlm.end() || it->second.empty()) {
            ADD_FAILURE() << "channel " << channel << " never written";
            return 0xDEADBEEF;
        }
        return it->second.back();
    }

    U32 op(const char* key) { return fs().opCounts[key]; }

    //! Seed a segment file: a valid header with openSeconds, padded to size bytes.
    void seedSegment(RecorderStream::T s, U32 seq, U32 openSeconds, U32 size) {
        fs().directories.insert("/rec");
        fs().directories.insert(streamDir(s));
        Bytes b = segmentHeader(s, openSeconds);
        const Bytes recBytes = payload(seq, 10);
        while (b.size() + recBytes.size() + SC::RECORD_OVERHEAD <= size) {
            U8 out[64];
            const uint32_t n =
                SC::encodeRecord(recBytes.data(), static_cast<uint16_t>(recBytes.size()), out, sizeof(out));
            b.insert(b.end(), out, out + n);
        }
        b.resize(size, 0x00);
        fs().files[segmentPath(s, seq)] = b;
    }

    //! Seed a segment file whose header is invalid.
    void seedGarbageSegment(RecorderStream::T s, U32 seq, U32 size) {
        fs().directories.insert("/rec");
        fs().directories.insert(streamDir(s));
        fs().files[segmentPath(s, seq)] = Bytes(size, 0x5A);
    }

    std::vector<U32> segmentSeqs(RecorderStream::T s) {
        std::vector<U32> out;
        for (const auto& kv : fs().files) {
            const long long seq = segmentSeqOf(s, kv.first);
            if (seq >= 0) {
                out.push_back(static_cast<U32>(seq));
            }
        }
        return out;  // std::map order == ascending seq for 8-digit names
    }

    Decoded decodeSegment(RecorderStream::T s, U32 seq) {
        const auto it = fs().files.find(segmentPath(s, seq));
        if (it == fs().files.end()) {
            ADD_FAILURE() << "no segment " << segmentPath(s, seq);
            return Decoded();
        }
        return decodeBytes(it->second);
    }

    //! Every record in every segment of the stream, in segment then record order.
    std::vector<Bytes> recordsOnDisk(RecorderStream::T s) {
        std::vector<Bytes> out;
        for (U32 seq : this->segmentSeqs(s)) {
            const Decoded d = this->decodeSegment(s, seq);
            out.insert(out.end(), d.records.begin(), d.records.end());
        }
        return out;
    }

    //! SAVE_CONFIG, then the 26-byte payload it wrote (empty on failure).
    Bytes savedConfig() {
        EXPECT_EQ(this->saveConfig(), Rsp::OK);
        const auto it = fs().files.find(CONFIG_PATH);
        if (it == fs().files.end()) {
            ADD_FAILURE() << "SAVE_CONFIG wrote no " << CONFIG_PATH;
            return Bytes();
        }
        U8 out[PR::MAX_PAYLOAD_SIZE];
        uint16_t len = 0;
        const PR::Status st = PR::decode(CONFIG_MAGIC, it->second.data(), static_cast<uint32_t>(it->second.size()), out,
                                         sizeof(out), len);
        EXPECT_EQ(st, PR::Status::OK) << "config.bin is not a valid DRC1 PersistedRecord";
        if (st != PR::Status::OK) {
            return Bytes();
        }
        return Bytes(out, out + len);
    }
};

// ======================================================================
// DataRecorder-3: input handlers do no I/O and emit nothing
// ======================================================================

TEST_F(DataRecorderTest, InputHandlersBeforeFirstTickDoNoIoAndEmitNothing) {
    RecordProperty("verifies", "DataRecorder-3");
    const std::map<std::string, U32> countsBefore = fs().opCounts;
    const std::map<std::string, Bytes> filesBefore = fs().files;
    for (U32 i = 0; i < 100; i++) {
        this->push(RecorderStream::TLM, payload(i, static_cast<U16>(1 + (i * 37) % 227)));
        this->push(RecorderStream::EVT, payload(1000 + i, static_cast<U16>(1 + (i * 53) % 227)));
    }
    EXPECT_EQ(fs().opCounts, countsBefore);
    EXPECT_EQ(fs().files, filesBefore);
    EXPECT_EQ(this->rec->totalEvents(), 0u);
    EXPECT_EQ(this->rec->totalTlmWrites(), 0u);
    EXPECT_TRUE(this->rec->cmdResponses.empty());
    EXPECT_TRUE(this->rec->sendFileOutCalls.empty());
}

TEST_F(DataRecorderTest, InputHandlersAfterScanDoNoIoAndEmitNothing) {
    RecordProperty("verifies", "DataRecorder-3");
    this->tickUntilScanned();
    this->rec->clearHistory();
    const std::map<std::string, U32> countsBefore = fs().opCounts;
    const std::map<std::string, Bytes> filesBefore = fs().files;
    for (U32 i = 0; i < 100; i++) {
        this->push(RecorderStream::TLM, payload(i, 227));
        this->push(RecorderStream::EVT, payload(1000 + i, 1));
    }
    // A 0-byte buffer is ignored (section 7.1).
    this->push(RecorderStream::TLM, Bytes());
    this->push(RecorderStream::EVT, Bytes());
    EXPECT_EQ(fs().opCounts, countsBefore);
    EXPECT_EQ(fs().files, filesBefore);
    EXPECT_EQ(this->rec->totalEvents(), 0u);
    EXPECT_EQ(this->rec->totalTlmWrites(), 0u);
    EXPECT_TRUE(this->rec->cmdResponses.empty());
    EXPECT_TRUE(this->rec->sendFileOutCalls.empty());
}

TEST_F(DataRecorderTest, ZeroByteInputIsNotRecorded) {
    RecordProperty("verifies", "DataRecorder-3");
    this->tickUntilScanned();
    this->push(RecorderStream::TLM, Bytes());
    this->tick();
    EXPECT_EQ(this->lastTlm("TlmRecordsStored"), 0u);
    this->push(RecorderStream::TLM, payload(1, 5));
    this->tick();
    EXPECT_EQ(this->lastTlm("TlmRecordsStored"), 1u);
}

// ======================================================================
// DataRecorder-4: flush on count or interval, whichever first
// ======================================================================

TEST_F(DataRecorderTest, TlmFifteenRecordsNoWriteSixteenOneWriteInPushOrder) {
    RecordProperty("verifies", "DataRecorder-4");
    this->tickUntilScanned();
    this->rec->clearHistory();
    std::vector<Bytes> pushed = this->pushMany(RecorderStream::TLM, 1, 15, 40);
    U32 writes = this->op("write");
    U32 flushes = this->op("flush");
    this->tick();
    EXPECT_EQ(this->op("write"), writes) << "15 records (< flushRecords 16) were written";
    EXPECT_EQ(this->op("flush"), flushes);
    EXPECT_TRUE(this->segmentSeqs(RecorderStream::TLM).empty());

    const std::vector<Bytes> more = this->pushMany(RecorderStream::TLM, 16, 1, 40);
    pushed.insert(pushed.end(), more.begin(), more.end());
    writes = this->op("write");
    flushes = this->op("flush");
    this->tick();
    EXPECT_EQ(this->op("write"), writes + 1) << "16 records must be written with exactly one write";
    EXPECT_EQ(this->op("flush"), flushes + 1) << "and exactly one explicit flush";
    ASSERT_EQ(this->segmentSeqs(RecorderStream::TLM), std::vector<U32>{1});
    const Decoded d = this->decodeSegment(RecorderStream::TLM, 1);
    ASSERT_TRUE(d.headerOk);
    EXPECT_EQ(d.header.stream, static_cast<uint8_t>(RecorderStream::TLM));
    EXPECT_EQ(d.header.bootCount, 0xFFFFu);
    EXPECT_EQ(d.header.openSeconds, this->nowS());
    EXPECT_EQ(d.stop, SC::Status::END);
    EXPECT_EQ(d.records, pushed);
    ASSERT_EQ(this->rec->eventsSegmentOpened.size(), 1u);
    EXPECT_EQ(this->rec->eventsSegmentOpened[0].stream, RecorderStream::TLM);
    EXPECT_EQ(this->rec->eventsSegmentOpened[0].seq, 1u);
}

TEST_F(DataRecorderTest, EvtRecordWrittenOnTenthTickAfterPreviousFlushAndNotBefore) {
    RecordProperty("verifies", "DataRecorder-4");
    this->tickUntilScanned();
    // Previous flush: flushRecords (8) EVT records are written on the next tick.
    std::vector<Bytes> pushed = this->pushMany(RecorderStream::EVT, 1, EVT_FLUSH_RECORDS);
    this->tick();
    ASSERT_EQ(this->recordsOnDisk(RecorderStream::EVT).size(), EVT_FLUSH_RECORDS) << "precondition: first flush";

    const Bytes late = payload(99);
    this->push(RecorderStream::EVT, late);
    pushed.push_back(late);
    const U32 writes = this->op("write");
    for (U32 t = 1; t < EVT_FLUSH_INTERVAL_S; t++) {
        this->tick();
        EXPECT_EQ(this->op("write"), writes) << "EVT record written on tick " << t << " after the previous flush";
        EXPECT_EQ(this->recordsOnDisk(RecorderStream::EVT).size(), EVT_FLUSH_RECORDS);
    }
    this->tick();
    EXPECT_EQ(this->op("write"), writes + 1) << "EVT record not written on the 10th tick";
    EXPECT_EQ(this->recordsOnDisk(RecorderStream::EVT), pushed);
}

// ======================================================================
// DataRecorder-5: retention by capacity and age (and DH-L2-03's capacity clause)
// ======================================================================

TEST_F(DataRecorderTest, OverCapacityDeletesOneOldestPerTickUntilUnderCapacity) {
    RecordProperty("verifies", "DataRecorder-5,DH-L2-03");
    for (U32 seq = 1; seq <= 6; seq++) {
        this->seedSegment(RecorderStream::TLM, seq, NOW0 - 600 + seq, 10000);
    }
    this->boot();
    this->tickUntilScanned();
    ASSERT_EQ(this->lastTlm("TlmBytesOnDisk"), 60000u);
    ASSERT_EQ(this->lastTlm("TlmSegmentsOnDisk"), 6u);
    this->rec->clearHistory();

    EXPECT_EQ(this->setCapacity(RecorderStream::TLM, TLM_SEGMENT_MAX_BYTES), Rsp::OK);
    ASSERT_EQ(this->rec->eventsConfigApplied.size(), 1u);
    EXPECT_EQ(this->rec->eventsConfigApplied[0].stream, RecorderStream::TLM);
    EXPECT_EQ(this->rec->eventsConfigApplied[0].field, ConfigField::CAPACITY_BYTES);
    EXPECT_EQ(this->rec->eventsConfigApplied[0].value, TLM_SEGMENT_MAX_BYTES);

    // 60000 -> 50000 -> 40000 -> 30000 <= 32768: three ticks, one deletion each.
    for (U32 k = 1; k <= 3; k++) {
        this->tick();
        ASSERT_EQ(this->rec->eventsSegmentDeleted.size(), k) << "tick " << k;
        EXPECT_EQ(this->rec->eventsSegmentDeleted[k - 1].stream, RecorderStream::TLM);
        EXPECT_EQ(this->rec->eventsSegmentDeleted[k - 1].seq, k);
        EXPECT_EQ(this->rec->eventsSegmentDeleted[k - 1].reason, DeleteReason::CAPACITY);
        EXPECT_EQ(this->lastTlm("TlmBytesOnDisk"), 60000u - 10000u * k);
    }
    this->tick(5);
    EXPECT_EQ(this->rec->eventsSegmentDeleted.size(), 3u) << "deleted below capacity";
    EXPECT_EQ(this->segmentSeqs(RecorderStream::TLM), (std::vector<U32>{4, 5, 6}));
    EXPECT_EQ(this->lastTlm("TlmBytesOnDisk"), 30000u);
    EXPECT_EQ(this->lastTlm("TlmSegmentsOnDisk"), 3u);
}

TEST_F(DataRecorderTest, AgeDeletesOldestOnFirstTickPastRetentionOfNextAndNotEarlier) {
    RecordProperty("verifies", "DataRecorder-5");
    this->seedSegment(RecorderStream::TLM, 1, NOW0 - 10, 200);
    this->seedSegment(RecorderStream::TLM, 2, NOW0, 200);
    this->seedSegment(RecorderStream::TLM, 3, NOW0 + 100, 200);
    this->boot();
    this->tickUntilScanned();
    ASSERT_EQ(this->setRetention(RecorderStream::TLM, RETENTION_MIN), Rsp::OK);
    this->rec->clearHistory();

    // Segment 1 expires when now - open(2) > 60.
    for (U32 s = NOW0 + 1; s <= NOW0 + 60; s += 59) {
        this->setNow(s);
        this->tick();
    }
    EXPECT_TRUE(this->rec->eventsSegmentDeleted.empty()) << "deleted at now - T <= retention";
    this->setNow(NOW0 + 61);
    this->tick();
    ASSERT_EQ(this->rec->eventsSegmentDeleted.size(), 1u);
    EXPECT_EQ(this->rec->eventsSegmentDeleted[0].seq, 1u);
    EXPECT_EQ(this->rec->eventsSegmentDeleted[0].reason, DeleteReason::AGE);

    // Segment 2 expires when now - open(3) > 60, i.e. at NOW0 + 161, not at + 160.
    this->setNow(NOW0 + 160);
    this->tick(3);
    EXPECT_EQ(this->rec->eventsSegmentDeleted.size(), 1u) << "segment 2 deleted early";
    this->setNow(NOW0 + 161);
    this->tick();
    ASSERT_EQ(this->rec->eventsSegmentDeleted.size(), 2u);
    EXPECT_EQ(this->rec->eventsSegmentDeleted[1].seq, 2u);
    EXPECT_EQ(this->rec->eventsSegmentDeleted[1].reason, DeleteReason::AGE);

    // Segment 3 is the newest: never deleted for age.
    this->setNow(NOW0 + 10 * RETENTION_MAX);
    this->tick(10);
    EXPECT_EQ(this->rec->eventsSegmentDeleted.size(), 2u);
    EXPECT_EQ(this->segmentSeqs(RecorderStream::TLM), std::vector<U32>{3});
}

TEST_F(DataRecorderTest, OldestSegmentWithInvalidHeaderIsDeleted) {
    RecordProperty("verifies", "DataRecorder-5");
    this->seedGarbageSegment(RecorderStream::TLM, 1, 100);
    this->seedSegment(RecorderStream::TLM, 2, NOW0, 200);
    this->boot();
    this->tickUntilScanned();
    this->tick(3);
    ASSERT_EQ(this->rec->eventsSegmentDeleted.size(), 1u);
    EXPECT_EQ(this->rec->eventsSegmentDeleted[0].stream, RecorderStream::TLM);
    EXPECT_EQ(this->rec->eventsSegmentDeleted[0].seq, 1u);
    EXPECT_EQ(this->rec->eventsSegmentDeleted[0].reason, DeleteReason::AGE);
    EXPECT_EQ(this->segmentSeqs(RecorderStream::TLM), std::vector<U32>{2});
}

TEST_F(DataRecorderTest, InvalidNextHeaderExpiresOldestAtRetentionPlusSegmentMaxS) {
    RecordProperty("verifies", "DataRecorder-5");
    this->seedSegment(RecorderStream::TLM, 1, NOW0, 200);
    this->seedGarbageSegment(RecorderStream::TLM, 2, 100);
    this->seedSegment(RecorderStream::TLM, 3, NOW0 + 10, 200);
    this->boot();
    this->tickUntilScanned();
    ASSERT_EQ(this->setRetention(RecorderStream::TLM, RETENTION_MIN), Rsp::OK);
    this->rec->clearHistory();
    this->setNow(NOW0 + RETENTION_MIN + SEGMENT_MAX_S);
    this->tick(2);
    EXPECT_TRUE(this->rec->eventsSegmentDeleted.empty()) << "deleted at now - S <= retention + SEGMENT_MAX_S";
    this->setNow(NOW0 + RETENTION_MIN + SEGMENT_MAX_S + 1);
    this->tick();
    ASSERT_GE(this->rec->eventsSegmentDeleted.size(), 1u);
    EXPECT_EQ(this->rec->eventsSegmentDeleted[0].seq, 1u);
    EXPECT_EQ(this->rec->eventsSegmentDeleted[0].reason, DeleteReason::AGE);
    // Segment 2 (invalid header) is now the oldest and goes on a later tick; 3 stays.
    this->tick(3);
    ASSERT_EQ(this->rec->eventsSegmentDeleted.size(), 2u);
    EXPECT_EQ(this->rec->eventsSegmentDeleted[1].seq, 2u);
    EXPECT_EQ(this->segmentSeqs(RecorderStream::TLM), std::vector<U32>{3});
}

TEST_F(DataRecorderTest, OpenSegmentIsNeverDeletedByRetention) {
    RecordProperty("verifies", "DataRecorder-5");
    this->seedSegment(RecorderStream::TLM, 1, NOW0 - 100, 200);
    this->boot();
    this->tickUntilScanned();
    ASSERT_EQ(this->setRetention(RecorderStream::TLM, RETENTION_MIN), Rsp::OK);
    // Segment 2 is opened now and stays open (no close, no rotation).
    this->pushMany(RecorderStream::TLM, 1, TLM_FLUSH_RECORDS);
    this->tick();
    ASSERT_EQ(this->segmentSeqs(RecorderStream::TLM), (std::vector<U32>{1, 2}));
    this->rec->clearHistory();
    this->setNow(NOW0 + 100000);
    this->tick(5);
    ASSERT_EQ(this->rec->eventsSegmentDeleted.size(), 1u);
    EXPECT_EQ(this->rec->eventsSegmentDeleted[0].seq, 1u);
    EXPECT_EQ(this->segmentSeqs(RecorderStream::TLM), std::vector<U32>{2});
    // More records still land in segment 2, which is still there.
    this->pushMany(RecorderStream::TLM, 100, TLM_FLUSH_RECORDS);
    this->tick();
    EXPECT_EQ(this->segmentSeqs(RecorderStream::TLM), std::vector<U32>{2});
    EXPECT_EQ(this->decodeSegment(RecorderStream::TLM, 2).records.size(), 2 * TLM_FLUSH_RECORDS);
}

// ======================================================================
// DataRecorder-6 and DH-L2-03: full ring drops the oldest and counts it
// ======================================================================

TEST_F(DataRecorderTest, RingDroppedRisesOnePerOverflowPushAndOnShrink) {
    RecordProperty("verifies", "DataRecorder-6");
    this->tickUntilScanned();
    fs().failOpenCreate = true;  // keep every record in the ring
    std::vector<Bytes> pushed = this->pushMany(RecorderStream::TLM, 1, RING_SLOTS_MAX);
    this->tick();
    ASSERT_EQ(this->lastTlm("TlmRingDropped"), 0u);
    for (U32 i = 1; i <= 5; i++) {
        pushed.push_back(payload(100 + i));
        this->push(RecorderStream::TLM, pushed.back());
        this->tick();
        EXPECT_EQ(this->lastTlm("TlmRingDropped"), i) << "overflow push " << i;
    }
    // Ring holds the newest 32 of 37; shrinking to 20 drops 12 more.
    ASSERT_EQ(this->setRingSlots(RecorderStream::TLM, 20), Rsp::OK);
    this->tick();
    EXPECT_EQ(this->lastTlm("TlmRingDropped"), 5u + 12u);
    fs().failOpenCreate = false;
    this->tick(5);
    const std::vector<Bytes> newest20(pushed.end() - 20, pushed.end());
    EXPECT_EQ(this->recordsOnDisk(RecorderStream::TLM), newest20);
    EXPECT_EQ(this->lastTlm("TlmRingDropped"), 17u);
}

TEST_F(DataRecorderTest, SetRingSlotsEveryInRangeValueHoldsExactlyNRecords) {
    RecordProperty("verifies", "DH-L2-03");
    constexpr U32 K = 3;
    for (U32 n = 1; n <= RING_SLOTS_MAX; n++) {
        SCOPED_TRACE("n = " + std::to_string(n));
        this->freshFileSystem();
        this->boot();
        this->tickUntilScanned();
        ASSERT_EQ(this->setFlush(RecorderStream::TLM, 1, 3600), Rsp::OK);  // so every n >= flushRecords
        this->rec->clearHistory();
        ASSERT_EQ(this->setRingSlots(RecorderStream::TLM, static_cast<U8>(n)), Rsp::OK);
        ASSERT_EQ(this->rec->eventsConfigApplied.size(), 1u);
        EXPECT_EQ(this->rec->eventsConfigApplied[0].stream, RecorderStream::TLM);
        EXPECT_EQ(this->rec->eventsConfigApplied[0].field, ConfigField::RING_SLOTS);
        EXPECT_EQ(this->rec->eventsConfigApplied[0].value, n);

        fs().failOpenCreate = true;
        this->tick();
        const U32 droppedBefore = this->lastTlm("TlmRingDropped");
        const std::vector<Bytes> pushed = this->pushMany(RecorderStream::TLM, 1, n + K, 10);
        this->tick();
        EXPECT_EQ(this->lastTlm("TlmRingDropped"), droppedBefore + K);

        fs().failOpenCreate = false;
        this->tick(3);
        const std::vector<Bytes> lastN(pushed.end() - n, pushed.end());
        EXPECT_EQ(this->recordsOnDisk(RecorderStream::TLM), lastN);
    }
}

// ======================================================================
// DataRecorder-8 and DH-L2-04: rejected configuration
// ======================================================================

struct RejectCase {
    const char* name;
    std::function<Rsp::T(DataRecorderTest*)> send;
    ConfigField::T field;
    U32 value;
    U32 max;
    std::function<void()> before;  // fault injection, may be empty
    std::function<void()> after;
};

TEST_F(DataRecorderTest, EveryOutOfRangeSetIsRejectedAndChangesNothing) {
    RecordProperty("verifies", "DataRecorder-8,DH-L2-04");
    this->tickUntilScanned();
    const std::vector<Bytes> tlmRecords = this->pushMany(RecorderStream::TLM, 1, 5);
    const std::vector<Bytes> evtRecords = this->pushMany(RecorderStream::EVT, 50, 3);
    const Bytes baseline = this->savedConfig();
    ASSERT_EQ(baseline, DEFAULT_CONFIG);

    // L = min(2^32 - 1, total - RESERVE_BYTES - capacity(other stream)).
    const U32 tlmL = static_cast<U32>(TOTAL_BYTES - RESERVE_BYTES - EVT_CAPACITY);
    const U32 evtL = static_cast<U32>(TOTAL_BYTES - RESERVE_BYTES - TLM_CAPACITY);
    const RecorderStream::T TLM = RecorderStream::TLM;
    const RecorderStream::T EVT = RecorderStream::EVT;

    const std::vector<RejectCase> cases = {
        {"RING_SLOTS 0", [&](DataRecorderTest* t) { return t->setRingSlots(TLM, 0); }, ConfigField::RING_SLOTS, 0, 32,
         nullptr, nullptr},
        {"RING_SLOTS 33", [&](DataRecorderTest* t) { return t->setRingSlots(EVT, 33); }, ConfigField::RING_SLOTS, 33,
         32, nullptr, nullptr},
        {"RING_SLOTS below flushRecords", [&](DataRecorderTest* t) { return t->setRingSlots(TLM, 15); },
         ConfigField::RING_SLOTS, 15, 32, nullptr, nullptr},
        {"RING_SLOTS below EVT flushRecords", [&](DataRecorderTest* t) { return t->setRingSlots(EVT, 7); },
         ConfigField::RING_SLOTS, 7, 32, nullptr, nullptr},
        {"CAPACITY below SEGMENT_MAX_BYTES TLM",
         [&](DataRecorderTest* t) { return t->setCapacity(TLM, TLM_SEGMENT_MAX_BYTES - 1); },
         ConfigField::CAPACITY_BYTES, TLM_SEGMENT_MAX_BYTES - 1, tlmL, nullptr, nullptr},
        {"CAPACITY below SEGMENT_MAX_BYTES EVT",
         [&](DataRecorderTest* t) { return t->setCapacity(EVT, EVT_SEGMENT_MAX_BYTES - 1); },
         ConfigField::CAPACITY_BYTES, EVT_SEGMENT_MAX_BYTES - 1, evtL, nullptr, nullptr},
        {"CAPACITY above L TLM", [&](DataRecorderTest* t) { return t->setCapacity(TLM, tlmL + 1); },
         ConfigField::CAPACITY_BYTES, tlmL + 1, tlmL, nullptr, nullptr},
        {"CAPACITY above L EVT", [&](DataRecorderTest* t) { return t->setCapacity(EVT, evtL + 1); },
         ConfigField::CAPACITY_BYTES, evtL + 1, evtL, nullptr, nullptr},
        {"CAPACITY with getFreeSpace failing",
         [&](DataRecorderTest* t) { return t->setCapacity(TLM, TLM_SEGMENT_MAX_BYTES); }, ConfigField::CAPACITY_BYTES,
         TLM_SEGMENT_MAX_BYTES, 0, [] { Os::Test::fileSystem().failGetFreeSpace = true; },
         [] { Os::Test::fileSystem().failGetFreeSpace = false; }},
        {"CAPACITY with total below the reserve",
         [&](DataRecorderTest* t) { return t->setCapacity(TLM, TLM_SEGMENT_MAX_BYTES); }, ConfigField::CAPACITY_BYTES,
         TLM_SEGMENT_MAX_BYTES, 0, [] { Os::Test::fileSystem().totalBytes = 1000000; },
         [] { Os::Test::fileSystem().totalBytes = TOTAL_BYTES; }},
        {"RETENTION 59", [&](DataRecorderTest* t) { return t->setRetention(TLM, RETENTION_MIN - 1); },
         ConfigField::RETENTION_S, RETENTION_MIN - 1, RETENTION_MAX, nullptr, nullptr},
        {"RETENTION 2592001", [&](DataRecorderTest* t) { return t->setRetention(EVT, RETENTION_MAX + 1); },
         ConfigField::RETENTION_S, RETENTION_MAX + 1, RETENTION_MAX, nullptr, nullptr},
        {"FLUSH records 0", [&](DataRecorderTest* t) { return t->setFlush(TLM, 0, 60); }, ConfigField::FLUSH_RECORDS, 0,
         32, nullptr, nullptr},
        {"FLUSH records above ringSlots", [&](DataRecorderTest* t) { return t->setFlush(EVT, 33, 10); },
         ConfigField::FLUSH_RECORDS, 33, 32, nullptr, nullptr},
        {"FLUSH interval 0", [&](DataRecorderTest* t) { return t->setFlush(TLM, 16, 0); },
         ConfigField::FLUSH_INTERVAL_S, 0, FLUSH_INTERVAL_MAX, nullptr, nullptr},
        {"FLUSH interval 3601", [&](DataRecorderTest* t) { return t->setFlush(EVT, 8, 3601); },
         ConfigField::FLUSH_INTERVAL_S, 3601, FLUSH_INTERVAL_MAX, nullptr, nullptr},
        {"FLUSH both bad reports records first", [&](DataRecorderTest* t) { return t->setFlush(TLM, 0, 0); },
         ConfigField::FLUSH_RECORDS, 0, 32, nullptr, nullptr},
    };

    for (const RejectCase& c : cases) {
        SCOPED_TRACE(c.name);
        this->rec->clearHistory();
        if (c.before) {
            c.before();
        }
        EXPECT_EQ(c.send(this), Rsp::VALIDATION_ERROR);
        if (c.after) {
            c.after();
        }
        ASSERT_EQ(this->rec->eventsConfigRejected.size(), 1u);
        EXPECT_EQ(this->rec->eventsConfigRejected[0].field, c.field);
        EXPECT_EQ(this->rec->eventsConfigRejected[0].value, c.value);
        EXPECT_EQ(this->rec->eventsConfigRejected[0].max, c.max);
        EXPECT_TRUE(this->rec->eventsConfigApplied.empty());
        EXPECT_EQ(this->savedConfig(), baseline) << "a rejected SET changed the configuration";
    }

    // The ring still holds exactly what was pushed: nothing dropped, nothing lost.
    this->tick();
    EXPECT_EQ(this->lastTlm("TlmRingDropped"), 0u);
    EXPECT_EQ(this->lastTlm("EvtRingDropped"), 0u);
    EXPECT_EQ(this->closeSegment(RecorderStream::TLM), Rsp::OK);
    EXPECT_EQ(this->closeSegment(RecorderStream::EVT), Rsp::OK);
    EXPECT_EQ(this->recordsOnDisk(RecorderStream::TLM), tlmRecords);
    EXPECT_EQ(this->recordsOnDisk(RecorderStream::EVT), evtRecords);
}

// ======================================================================
// DataRecorder-9: write failures lose nothing and do not reach producers
// ======================================================================

enum class Fault { OPEN, WRITE, SHORT_WRITE, FLUSH };

void setFault(Fault f, bool on) {
    Os::Test::FileSystemState& s = Os::Test::fileSystem();
    switch (f) {
        case Fault::OPEN:
            s.failOpenCreate = on;
            break;
        case Fault::WRITE:
            s.failWrite = on;
            break;
        case Fault::SHORT_WRITE:
            s.partialWrite = on;
            break;
        case Fault::FLUSH:
            s.failFlush = on;
            break;
    }
}

class DataRecorderWriteFailureTest : public DataRecorderTest {
  public:
    void run(Fault fault, U32 expectedStatus) {
        this->tickUntilScanned();
        const std::vector<Bytes> first = this->pushMany(RecorderStream::TLM, 1, TLM_FLUSH_RECORDS);
        this->tick();
        ASSERT_EQ(this->segmentSeqs(RecorderStream::TLM), std::vector<U32>{1}) << "precondition: segment 1 written";
        this->rec->clearHistory();

        setFault(fault, true);
        const std::vector<Bytes> kept = this->pushMany(RecorderStream::TLM, 100, TLM_FLUSH_RECORDS);
        for (U32 i = 1; i <= 10; i++) {
            this->tick();
            EXPECT_EQ(this->lastTlm("TlmWriteFailures"), i) << "failed attempt " << i;
            EXPECT_EQ(this->rec->eventsSegmentWriteFailed.size(), std::min<U32>(i, 5)) << "throttle after 5";
        }
        ASSERT_FALSE(this->rec->eventsSegmentWriteFailed.empty());
        EXPECT_EQ(this->rec->eventsSegmentWriteFailed[0].stream, RecorderStream::TLM);
        EXPECT_EQ(this->rec->eventsSegmentWriteFailed[0].status, expectedStatus);
        EXPECT_EQ(this->lastTlm("TlmRingDropped"), 0u);

        // Producers are unaffected while every attempt fails.
        const std::map<std::string, U32> countsBefore = fs().opCounts;
        this->pushMany(RecorderStream::EVT, 500, 3);
        EXPECT_EQ(fs().opCounts, countsBefore);

        const std::map<std::string, Bytes> beforeRecovery = fs().files;
        U32 highestBefore = 0;
        for (U32 seq : this->segmentSeqs(RecorderStream::TLM)) {
            highestBefore = std::max(highestBefore, seq);
        }
        setFault(fault, false);
        this->rec->clearHistory();
        this->tick();

        // No existing file truncated (segment 1 in particular is untouched).
        for (const auto& kv : beforeRecovery) {
            const auto it = fs().files.find(kv.first);
            ASSERT_NE(it, fs().files.end()) << "removed during recovery: " << kv.first;
            ASSERT_GE(it->second.size(), kv.second.size()) << "truncated: " << kv.first;
            EXPECT_TRUE(std::equal(kv.second.begin(), kv.second.end(), it->second.begin()))
                << "rewritten: " << kv.first;
        }
        EXPECT_EQ(fs().files[segmentPath(RecorderStream::TLM, 1)],
                  beforeRecovery.at(segmentPath(RecorderStream::TLM, 1)));
        EXPECT_TRUE(this->decodeSegment(RecorderStream::TLM, 1).records.size() >= first.size() &&
                    std::equal(first.begin(), first.end(), this->decodeSegment(RecorderStream::TLM, 1).records.begin()))
            << "segment 1 does not begin with the records written before the fault";

        // The kept records are written, in order, to a segment with a new number.
        const std::vector<U32> seqs = this->segmentSeqs(RecorderStream::TLM);
        ASSERT_FALSE(seqs.empty());
        const U32 newest = seqs.back();
        EXPECT_GT(newest, highestBefore);
        EXPECT_EQ(this->decodeSegment(RecorderStream::TLM, newest).records, kept);
        std::vector<U32> tlmOpened;
        for (const Base::StreamSeq& e : this->rec->eventsSegmentOpened) {
            if (e.stream == RecorderStream::TLM) {
                tlmOpened.push_back(e.seq);
            }
        }
        EXPECT_EQ(tlmOpened, std::vector<U32>{newest});
        EXPECT_EQ(this->lastTlm("TlmWriteFailures"), 10u);
    }
};

TEST_F(DataRecorderWriteFailureTest, OpenFailure) {
    RecordProperty("verifies", "DataRecorder-9");
    this->run(Fault::OPEN, Os::File::OTHER_ERROR);
}

TEST_F(DataRecorderWriteFailureTest, WriteFailure) {
    RecordProperty("verifies", "DataRecorder-9");
    this->run(Fault::WRITE, Os::File::OTHER_ERROR);
}

TEST_F(DataRecorderWriteFailureTest, ShortWriteReportsBadSize) {
    RecordProperty("verifies", "DataRecorder-9");
    this->run(Fault::SHORT_WRITE, Os::File::BAD_SIZE);
}

TEST_F(DataRecorderWriteFailureTest, FlushFailure) {
    RecordProperty("verifies", "DataRecorder-9");
    this->run(Fault::FLUSH, Os::File::OTHER_ERROR);
}

TEST_F(DataRecorderTest, OverflowWhileWritesFailDropsOnlyTheOldest) {
    RecordProperty("verifies", "DataRecorder-9");
    this->tickUntilScanned();
    fs().failWrite = true;
    const std::vector<Bytes> pushed = this->pushMany(RecorderStream::TLM, 1, 40, 10);
    this->tick(3);
    EXPECT_EQ(this->lastTlm("TlmRingDropped"), 8u);
    fs().failWrite = false;
    this->tick(3);
    // Only non-empty segments written after recovery hold records; failed opens
    // left empty files, which decode to nothing.
    const std::vector<Bytes> newest32(pushed.begin() + 8, pushed.end());
    EXPECT_EQ(this->recordsOnDisk(RecorderStream::TLM), newest32);
}

TEST_F(DataRecorderTest, InputsReturnNormallyWhileEveryOperationFails) {
    RecordProperty("verifies", "DataRecorder-9,DataRecorder-3");
    this->tickUntilScanned();
    fs().failOpenCreate = true;
    fs().failWrite = true;
    fs().failFlush = true;
    fs().failRemove = true;
    fs().failGetFreeSpace = true;
    fs().failCreateDirectory = true;
    for (U32 round = 0; round < 20; round++) {
        const std::map<std::string, U32> countsBefore = fs().opCounts;
        const size_t events = this->rec->totalEvents();
        const size_t tlmWrites = this->rec->totalTlmWrites();
        for (U32 i = 0; i < 100; i++) {
            this->push(RecorderStream::TLM, payload(round * 1000 + i, 50));
            this->push(RecorderStream::EVT, payload(round * 1000 + 500 + i, 50));
        }
        EXPECT_EQ(fs().opCounts, countsBefore) << "round " << round;
        EXPECT_EQ(this->rec->totalEvents(), events);
        EXPECT_EQ(this->rec->totalTlmWrites(), tlmWrites);
        this->tick();
    }
}

// ======================================================================
// DataRecorder-10: configuration record
// ======================================================================

TEST_F(DataRecorderTest, SaveConfigWritesDefaultsAsDrc1PersistedRecord) {
    RecordProperty("verifies", "DataRecorder-10");
    this->tickUntilScanned();
    EXPECT_EQ(this->savedConfig(), DEFAULT_CONFIG);
    const Bytes& file = fs().files[CONFIG_PATH];
    ASSERT_EQ(file.size(), PR::OVERHEAD + 26u);
    EXPECT_EQ(Bytes(file.begin(), file.begin() + 4), Bytes(CONFIG_MAGIC, CONFIG_MAGIC + 4));
    EXPECT_EQ(file[4], PR::FORMAT_VERSION);
    EXPECT_TRUE(this->rec->eventsConfigCorrupt.empty());
}

TEST_F(DataRecorderTest, SavedConfigIsAppliedByANewInstanceOnItsFirstTick) {
    RecordProperty("verifies", "DataRecorder-10");
    this->tickUntilScanned();
    this->rec->clearHistory();
    ASSERT_EQ(this->setFlush(RecorderStream::TLM, 4, 30), Rsp::OK);
    ASSERT_EQ(this->setRingSlots(RecorderStream::EVT, 20), Rsp::OK);
    ASSERT_EQ(this->setCapacity(RecorderStream::EVT, 3000000), Rsp::OK);
    ASSERT_EQ(this->setRetention(RecorderStream::TLM, 3600), Rsp::OK);
    // One ConfigApplied per field (SET_FLUSH: two); order within SET_FLUSH is not fixed.
    std::multiset<std::vector<U32>> applied;
    for (const Base::ConfigAppliedRecord& e : this->rec->eventsConfigApplied) {
        applied.insert({static_cast<U32>(e.stream), static_cast<U32>(e.field), e.value});
    }
    const std::multiset<std::vector<U32>> expectedApplied = {
        {RecorderStream::TLM, ConfigField::FLUSH_RECORDS, 4},
        {RecorderStream::TLM, ConfigField::FLUSH_INTERVAL_S, 30},
        {RecorderStream::EVT, ConfigField::RING_SLOTS, 20},
        {RecorderStream::EVT, ConfigField::CAPACITY_BYTES, 3000000},
        {RecorderStream::TLM, ConfigField::RETENTION_S, 3600}};
    EXPECT_EQ(applied, expectedApplied);

    StreamConfig tlm = TLM_DEFAULTS;
    StreamConfig evt = EVT_DEFAULTS;
    tlm.flushRecords = 4;
    tlm.flushIntervalS = 30;
    tlm.retentionS = 3600;
    evt.ringSlots = 20;
    evt.capacityBytes = 3000000;
    const Bytes expected = configPayload(tlm, evt);
    ASSERT_EQ(this->savedConfig(), expected);

    this->boot(NOW0 + 50);  // reboot over the same filesystem
    this->tick();           // the first tick applies /rec/config.bin
    EXPECT_TRUE(this->rec->eventsConfigCorrupt.empty());
    this->tickUntilScanned();
    // flushRecords 4 is in force: four TLM records are written on the next tick.
    const std::vector<Bytes> four = this->pushMany(RecorderStream::TLM, 1, 4);
    this->tick();
    EXPECT_EQ(this->recordsOnDisk(RecorderStream::TLM), four);
    EXPECT_EQ(this->savedConfig(), expected);
}

TEST_F(DataRecorderTest, MissingConfigGivesDefaultsAndNoEvent) {
    RecordProperty("verifies", "DataRecorder-10");
    this->tickUntilScanned();
    this->tick(3);
    EXPECT_TRUE(this->rec->eventsConfigCorrupt.empty());
    EXPECT_EQ(this->rec->configCorruptCalls, 0u);
    EXPECT_EQ(this->savedConfig(), DEFAULT_CONFIG);
}

//! Seed /rec/config.bin with raw bytes, boot, tick 3 times; count ConfigCorrupt
//! and read back the configuration in force.
struct LoadOutcome {
    size_t corruptEvents;
    std::vector<U8> statuses;
    Bytes config;
};

class DataRecorderConfigLoadTest : public DataRecorderTest {
  public:
    LoadOutcome loadFrom(const Bytes& fileBytes) {
        this->freshFileSystem();
        fs().directories.insert("/rec");
        fs().files[CONFIG_PATH] = fileBytes;
        this->boot();
        this->tick(3);
        LoadOutcome out;
        out.corruptEvents = this->rec->eventsConfigCorrupt.size();
        out.statuses = this->rec->eventsConfigCorrupt;
        out.config = this->savedConfig();
        return out;
    }

    Bytes nonDefaultRecord() {
        StreamConfig tlm = TLM_DEFAULTS;
        StreamConfig evt = EVT_DEFAULTS;
        tlm.flushRecords = 4;
        evt.retentionS = 7200;
        return configRecord(configPayload(tlm, evt));
    }
};

TEST_F(DataRecorderConfigLoadTest, ValidNonDefaultRecordLoadsWithoutEvent) {
    RecordProperty("verifies", "DataRecorder-10");
    StreamConfig tlm = TLM_DEFAULTS;
    StreamConfig evt = EVT_DEFAULTS;
    tlm.flushRecords = 4;
    evt.retentionS = 7200;
    const LoadOutcome o = this->loadFrom(this->nonDefaultRecord());
    EXPECT_EQ(o.corruptEvents, 0u);
    EXPECT_EQ(o.config, configPayload(tlm, evt));
}

TEST_F(DataRecorderConfigLoadTest, EverySingleByteCorruptionGivesDefaultsAndOneConfigCorrupt) {
    RecordProperty("verifies", "DataRecorder-10");
    const Bytes good = this->nonDefaultRecord();
    U32 failures = 0;
    for (size_t pos = 0; pos < good.size() && failures < 5; pos++) {
        for (unsigned mask = 1; mask < 256 && failures < 5; mask++) {
            Bytes bad = good;
            bad[pos] = static_cast<U8>(bad[pos] ^ mask);
            const LoadOutcome o = this->loadFrom(bad);
            if (o.corruptEvents != 1u || o.config != DEFAULT_CONFIG) {
                ADD_FAILURE() << "byte " << pos << " xor " << mask << ": " << o.corruptEvents
                              << " ConfigCorrupt events, defaults " << (o.config == DEFAULT_CONFIG ? "yes" : "no");
                failures++;
            }
        }
    }
}

TEST_F(DataRecorderConfigLoadTest, EveryTruncationGivesDefaultsAndOneConfigCorrupt) {
    RecordProperty("verifies", "DataRecorder-10");
    const Bytes good = this->nonDefaultRecord();
    for (size_t len = 0; len < good.size(); len++) {
        SCOPED_TRACE("length " + std::to_string(len));
        const LoadOutcome o = this->loadFrom(Bytes(good.begin(), good.begin() + len));
        EXPECT_EQ(o.corruptEvents, 1u);
        EXPECT_EQ(o.config, DEFAULT_CONFIG);
    }
}

TEST_F(DataRecorderConfigLoadTest, CorruptCrcReportsThePersistedRecordStatus) {
    RecordProperty("verifies", "DataRecorder-10");
    Bytes bad = this->nonDefaultRecord();
    bad.back() ^= 0x01;
    const LoadOutcome o = this->loadFrom(bad);
    ASSERT_EQ(o.corruptEvents, 1u);
    EXPECT_EQ(o.statuses[0], static_cast<U8>(PR::Status::BAD_CRC));
    EXPECT_EQ(o.config, DEFAULT_CONFIG);
}

TEST_F(DataRecorderConfigLoadTest, InvalidPayloadGivesDefaultsAndConfigCorrupt255) {
    RecordProperty("verifies", "DataRecorder-10");
    struct Variant {
        const char* name;
        Bytes payloadBytes;
    };
    auto with = [](const std::function<void(StreamConfig&, StreamConfig&)>& edit) {
        StreamConfig tlm = TLM_DEFAULTS;
        StreamConfig evt = EVT_DEFAULTS;
        edit(tlm, evt);
        return configPayload(tlm, evt);
    };
    Bytes shortPayload = DEFAULT_CONFIG;
    shortPayload.pop_back();
    const std::vector<Variant> variants = {
        {"25-byte payload", shortPayload},
        {"layout 2", configPayload(TLM_DEFAULTS, EVT_DEFAULTS, 2, 2)},
        {"layout 0", configPayload(TLM_DEFAULTS, EVT_DEFAULTS, 0, 2)},
        {"streamCount 1", configPayload(TLM_DEFAULTS, EVT_DEFAULTS, 1, 1)},
        {"streamCount 3", configPayload(TLM_DEFAULTS, EVT_DEFAULTS, 1, 3)},
        {"TLM ringSlots 0", with([](StreamConfig& t, StreamConfig&) { t.ringSlots = 0; })},
        {"EVT ringSlots 33", with([](StreamConfig&, StreamConfig& e) { e.ringSlots = 33; })},
        {"TLM flushRecords 0", with([](StreamConfig& t, StreamConfig&) { t.flushRecords = 0; })},
        {"TLM flushRecords > ringSlots", with([](StreamConfig& t, StreamConfig&) { t.ringSlots = 10; })},
        {"EVT flushRecords > ringSlots", with([](StreamConfig&, StreamConfig& e) {
             e.ringSlots = 12;
             e.flushRecords = 13;
         })},
        {"TLM flushIntervalS 0", with([](StreamConfig& t, StreamConfig&) { t.flushIntervalS = 0; })},
        {"EVT flushIntervalS 3601", with([](StreamConfig&, StreamConfig& e) { e.flushIntervalS = 3601; })},
        {"TLM capacity below SEGMENT_MAX_BYTES",
         with([](StreamConfig& t, StreamConfig&) { t.capacityBytes = TLM_SEGMENT_MAX_BYTES - 1; })},
        {"EVT capacity below SEGMENT_MAX_BYTES",
         with([](StreamConfig&, StreamConfig& e) { e.capacityBytes = EVT_SEGMENT_MAX_BYTES - 1; })},
        {"TLM retention 59", with([](StreamConfig& t, StreamConfig&) { t.retentionS = RETENTION_MIN - 1; })},
        {"EVT retention 2592001", with([](StreamConfig&, StreamConfig& e) { e.retentionS = RETENTION_MAX + 1; })},
    };
    for (const Variant& v : variants) {
        SCOPED_TRACE(v.name);
        const LoadOutcome o = this->loadFrom(configRecord(v.payloadBytes));
        ASSERT_EQ(o.corruptEvents, 1u);
        EXPECT_EQ(o.statuses[0], 255u);
        EXPECT_EQ(o.config, DEFAULT_CONFIG);
    }
    // A 27-byte payload is a wrong length too; its status may come from the
    // record layer (BAD_LENGTH) or the payload check (255).
    Bytes longPayload = DEFAULT_CONFIG;
    longPayload.push_back(0x00);
    const LoadOutcome o = this->loadFrom(configRecord(longPayload));
    EXPECT_EQ(o.corruptEvents, 1u);
    EXPECT_EQ(o.config, DEFAULT_CONFIG);
}

// Section 5.5: capacity in the record is checked only against its lower bound.
TEST_F(DataRecorderConfigLoadTest, StoredCapacityAboveTheFreeSpaceLimitIsAccepted) {
    StreamConfig tlm = TLM_DEFAULTS;
    tlm.capacityBytes = 0xFFFFFFFFu;
    const Bytes p = configPayload(tlm, EVT_DEFAULTS);
    const LoadOutcome o = this->loadFrom(configRecord(p));
    EXPECT_EQ(o.corruptEvents, 0u);
    EXPECT_EQ(o.config, p);
}

// Section 3: SAVE_CONFIG answers EXECUTION_ERROR, with no event, when the store fails.
TEST_F(DataRecorderTest, SaveConfigStoreFailureIsExecutionErrorWithoutEvent) {
    this->tickUntilScanned();
    this->rec->clearHistory();
    fs().failRename = true;
    EXPECT_EQ(this->saveConfig(), Rsp::EXECUTION_ERROR);
    fs().failRename = false;
    EXPECT_EQ(this->rec->totalEvents(), 0u);
}

// Section 3: a config command before the first tick loads the file first, so the
// first tick does not overwrite the command's value.
TEST_F(DataRecorderTest, ConfigCommandBeforeFirstTickLoadsTheFileFirst) {
    StreamConfig tlm = TLM_DEFAULTS;
    tlm.retentionS = 3600;
    this->freshFileSystem();
    fs().directories.insert("/rec");
    fs().files[CONFIG_PATH] = configRecord(configPayload(tlm, EVT_DEFAULTS));
    this->boot();
    ASSERT_EQ(this->setRetention(RecorderStream::EVT, 100), Rsp::OK);
    this->tickUntilScanned();
    StreamConfig evt = EVT_DEFAULTS;
    evt.retentionS = 100;
    EXPECT_EQ(this->savedConfig(), configPayload(tlm, evt));
}

// ======================================================================
// DataRecorder-11: boot scan
// ======================================================================

class DataRecorderScanTest : public DataRecorderTest, public ::testing::WithParamInterface<bool> {};

TEST_P(DataRecorderScanTest, CountsOnlySegmentNamesAndContinuesFromMaxPlusOne) {
    RecordProperty("verifies", "DataRecorder-11");
    fs().reverseDirectoryOrder = GetParam();
    this->seedSegment(RecorderStream::TLM, 3, NOW0 - 300, 100);
    this->seedSegment(RecorderStream::TLM, 7, NOW0 - 200, 200);
    this->seedSegment(RecorderStream::TLM, 12, NOW0 - 100, 300);
    const std::vector<std::string> others = {
        "/rec/tlm/readme.txt",   "/rec/tlm/1234567.bin",  "/rec/tlm/000000001.bin",   "/rec/tlm/0000000a.bin",
        "/rec/tlm/00000005.BIN", "/rec/tlm/00000004.tmp", "/rec/tlm/sub/00000099.bin"};
    fs().directories.insert("/rec/tlm/sub");
    std::map<std::string, Bytes> otherContent;
    for (size_t i = 0; i < others.size(); i++) {
        otherContent[others[i]] = Bytes(40 + i, static_cast<U8>(0x30 + i));
        fs().files[others[i]] = otherContent[others[i]];
    }
    this->boot();
    this->tickUntilScanned();
    EXPECT_EQ(this->lastTlm("TlmSegmentsOnDisk"), 3u);
    EXPECT_EQ(this->lastTlm("TlmBytesOnDisk"), 600u);
    EXPECT_EQ(this->lastTlm("EvtSegmentsOnDisk"), 0u);
    EXPECT_EQ(this->lastTlm("EvtBytesOnDisk"), 0u);

    this->rec->clearHistory();
    this->pushMany(RecorderStream::TLM, 1, TLM_FLUSH_RECORDS);
    this->tick();
    ASSERT_EQ(this->rec->eventsSegmentOpened.size(), 1u);
    EXPECT_EQ(this->rec->eventsSegmentOpened[0].seq, 13u);
    EXPECT_EQ(this->segmentSeqs(RecorderStream::TLM), (std::vector<U32>{3, 7, 12, 13}));

    // Age out 3, 7 and 12; the other names survive untouched.
    ASSERT_EQ(this->setRetention(RecorderStream::TLM, RETENTION_MIN), Rsp::OK);
    this->setNow(NOW0 + 100000);
    this->tick(10);
    EXPECT_EQ(this->segmentSeqs(RecorderStream::TLM), std::vector<U32>{13});
    for (const auto& kv : otherContent) {
        ASSERT_EQ(fs().files.count(kv.first), 1u) << "deleted: " << kv.first;
        EXPECT_EQ(fs().files[kv.first], kv.second) << "changed: " << kv.first;
    }
}

TEST_P(DataRecorderScanTest, ReadsAtMostTheBudgetPerStreamPerTick) {
    RecordProperty("verifies", "DataRecorder-11");
    fs().reverseDirectoryOrder = GetParam();
    for (U32 seq = 1; seq <= 70; seq++) {
        this->seedSegment(RecorderStream::TLM, seq, NOW0 - 1000 + seq, 30);
        this->seedSegment(RecorderStream::EVT, seq, NOW0 - 1000 + seq, 30);
    }
    this->boot();
    U32 ticks = 0;
    while (ticks < 20) {
        const U32 reads = this->op("dirRead");
        const U32 sizes = this->op("getFileSize");
        this->tick();
        ticks++;
        EXPECT_LE(this->op("dirRead") - reads, 2 * SCAN_BUDGET) << "tick " << ticks;
        EXPECT_LE(this->op("getFileSize") - sizes, 2 * SCAN_BUDGET) << "tick " << ticks;
        const bool tlmBusy = this->scanning(RecorderStream::TLM);
        const bool evtBusy = this->scanning(RecorderStream::EVT);
        if (ticks <= 2) {
            // 70 entries cannot be read in two ticks of 32.
            EXPECT_TRUE(tlmBusy) << "TLM scan finished in " << ticks << " ticks";
            EXPECT_TRUE(evtBusy) << "EVT scan finished in " << ticks << " ticks";
        }
        if (!tlmBusy && !evtBusy) {
            break;
        }
    }
    EXPECT_EQ(this->lastTlm("TlmSegmentsOnDisk"), 70u);
    EXPECT_EQ(this->lastTlm("TlmBytesOnDisk"), 70u * 30u);
    EXPECT_EQ(this->lastTlm("EvtSegmentsOnDisk"), 70u);
    EXPECT_EQ(this->lastTlm("EvtBytesOnDisk"), 70u * 30u);
    this->rec->clearHistory();
    this->pushMany(RecorderStream::EVT, 1, EVT_FLUSH_RECORDS);
    this->tick();
    ASSERT_EQ(this->rec->eventsSegmentOpened.size(), 1u);
    EXPECT_EQ(this->rec->eventsSegmentOpened[0].stream, RecorderStream::EVT);
    EXPECT_EQ(this->rec->eventsSegmentOpened[0].seq, 71u);
}

INSTANTIATE_TEST_SUITE_P(DirectoryOrder, DataRecorderScanTest, ::testing::Values(false, true));

TEST_F(DataRecorderTest, FirstSegmentIsNumberOneWhenTheDirectoryIsEmpty) {
    RecordProperty("verifies", "DataRecorder-11");
    this->tickUntilScanned();
    EXPECT_EQ(this->lastTlm("TlmSegmentsOnDisk"), 0u);
    this->pushMany(RecorderStream::TLM, 1, TLM_FLUSH_RECORDS);
    this->pushMany(RecorderStream::EVT, 1, EVT_FLUSH_RECORDS);
    this->tick();
    EXPECT_EQ(this->segmentSeqs(RecorderStream::TLM), std::vector<U32>{1});
    EXPECT_EQ(this->segmentSeqs(RecorderStream::EVT), std::vector<U32>{1});
}

TEST_F(DataRecorderTest, SkipsANumberWhoseFileAlreadyExists) {
    RecordProperty("verifies", "DataRecorder-11");
    this->seedSegment(RecorderStream::TLM, 3, NOW0 - 30, 100);
    this->seedSegment(RecorderStream::TLM, 12, NOW0 - 10, 100);
    this->boot();
    this->tickUntilScanned();
    // A file named 13 appears after the scan; it must not be overwritten.
    const Bytes intruder(77, 0xC3);
    fs().files[segmentPath(RecorderStream::TLM, 13)] = intruder;
    this->rec->clearHistory();
    this->pushMany(RecorderStream::TLM, 1, TLM_FLUSH_RECORDS);
    this->tick();
    EXPECT_EQ(fs().files[segmentPath(RecorderStream::TLM, 13)], intruder);
    ASSERT_EQ(this->rec->eventsSegmentOpened.size(), 1u);
    EXPECT_EQ(this->rec->eventsSegmentOpened[0].seq, 14u);
    EXPECT_EQ(this->decodeSegment(RecorderStream::TLM, 14).records.size(), TLM_FLUSH_RECORDS);
}

TEST_F(DataRecorderTest, DirectoryOpenFailureEmitsAndRestartsNextTick) {
    RecordProperty("verifies", "DataRecorder-11");
    this->seedSegment(RecorderStream::TLM, 5, NOW0 - 10, 150);
    this->boot();
    fs().failDirectoryOpen = true;
    this->tick();
    ASSERT_FALSE(this->rec->eventsDirectoryScanFailed.empty());
    bool sawTlm = false;
    for (const Base::StreamStatus& e : this->rec->eventsDirectoryScanFailed) {
        sawTlm = sawTlm || (e.stream == RecorderStream::TLM);
    }
    EXPECT_TRUE(sawTlm);
    EXPECT_TRUE(this->scanning(RecorderStream::TLM));
    const U32 opens = this->op("dirOpen");
    this->tick();
    EXPECT_GT(this->op("dirOpen"), opens) << "scan not restarted on the next tick";
    fs().failDirectoryOpen = false;
    this->tickUntilScanned();
    EXPECT_EQ(this->lastTlm("TlmSegmentsOnDisk"), 1u);
    EXPECT_EQ(this->lastTlm("TlmBytesOnDisk"), 150u);
}

TEST_F(DataRecorderTest, DirectoryReadFailureResetsCountsAndRestarts) {
    RecordProperty("verifies", "DataRecorder-11");
    for (U32 seq = 1; seq <= 40; seq++) {
        this->seedSegment(RecorderStream::TLM, seq, NOW0 - 100 + seq, 50);
    }
    this->boot();
    fs().dirReadFailAt = 35;
    U32 t = 0;
    while (this->rec->eventsDirectoryScanFailed.empty() && t < 10) {
        this->tick();
        t++;
    }
    ASSERT_FALSE(this->rec->eventsDirectoryScanFailed.empty()) << "read failure never reported";
    EXPECT_EQ(this->rec->eventsDirectoryScanFailed[0].stream, RecorderStream::TLM);
    fs().dirReadFailAt = -1;
    const U32 opens = this->op("dirOpen");
    this->tickUntilScanned();
    EXPECT_GT(this->op("dirOpen"), opens) << "scan did not restart";
    EXPECT_EQ(this->lastTlm("TlmSegmentsOnDisk"), 40u) << "counts not reset before the restart";
    EXPECT_EQ(this->lastTlm("TlmBytesOnDisk"), 40u * 50u);
}

// ======================================================================
// Pins of 01-normative sections 3, 4 and 7 (no requirement claimed)
// ======================================================================

TEST_F(DataRecorderTest, AllFourteenChannelsAreWrittenOnEveryTickIncludingWhileScanning) {
    for (U32 seq = 1; seq <= 70; seq++) {
        this->seedSegment(RecorderStream::TLM, seq, NOW0 - 1000 + seq, 30);
    }
    this->boot();
    this->tick(3);  // the TLM scan needs at least three ticks here
    for (const char* ch : CHANNELS) {
        EXPECT_EQ(this->rec->tlm[ch].size(), 3u) << ch;
    }
    EXPECT_EQ(this->rec->tlm.size(), 14u);
}

TEST_F(DataRecorderTest, QuietBootCreatesNoSegmentFile) {
    this->tick(100);
    EXPECT_TRUE(this->segmentSeqs(RecorderStream::TLM).empty());
    EXPECT_TRUE(this->segmentSeqs(RecorderStream::EVT).empty());
    EXPECT_EQ(fs().directories.count("/rec"), 1u);
    EXPECT_EQ(fs().directories.count("/rec/tlm"), 1u);
    EXPECT_EQ(fs().directories.count("/rec/evt"), 1u);
    EXPECT_EQ(this->op("write"), 0u);
}

TEST_F(DataRecorderTest, ListSegmentsIsBusyWhileScanningThenListsAscendingFromSeq) {
    this->seedSegment(RecorderStream::TLM, 3, NOW0 - 300, 100);
    this->seedSegment(RecorderStream::TLM, 7, NOW0 - 200, 200);
    this->seedSegment(RecorderStream::TLM, 12, NOW0 - 100, 300);
    this->boot();
    EXPECT_EQ(this->listSegments(RecorderStream::TLM, 0), Rsp::BUSY);
    this->tickUntilScanned();
    this->rec->clearHistory();
    EXPECT_EQ(this->listSegments(RecorderStream::TLM, 0), Rsp::OK);
    ASSERT_EQ(this->rec->eventsSegmentInfo.size(), 3u);
    const U32 seqs[3] = {3, 7, 12};
    const U32 bytes[3] = {100, 200, 300};
    const U32 opens[3] = {NOW0 - 300, NOW0 - 200, NOW0 - 100};
    for (int i = 0; i < 3; i++) {
        EXPECT_EQ(this->rec->eventsSegmentInfo[i].stream, RecorderStream::TLM);
        EXPECT_EQ(this->rec->eventsSegmentInfo[i].seq, seqs[i]);
        EXPECT_EQ(this->rec->eventsSegmentInfo[i].bytes, bytes[i]);
        EXPECT_EQ(this->rec->eventsSegmentInfo[i].openTimeS, opens[i]);
    }
    this->rec->clearHistory();
    EXPECT_EQ(this->listSegments(RecorderStream::TLM, 7), Rsp::OK);
    ASSERT_EQ(this->rec->eventsSegmentInfo.size(), 2u);
    EXPECT_EQ(this->rec->eventsSegmentInfo[0].seq, 7u);
    EXPECT_EQ(this->rec->eventsSegmentInfo[1].seq, 12u);
}

TEST_F(DataRecorderTest, ListSegmentsStopsAfterSixteenEventsOrSixtyFourProbes) {
    for (U32 seq = 1; seq <= 20; seq++) {
        this->seedSegment(RecorderStream::TLM, seq, NOW0 - 100 + seq, 40);
    }
    this->seedSegment(RecorderStream::EVT, 1, NOW0 - 50, 40);
    this->seedSegment(RecorderStream::EVT, 100, NOW0 - 10, 40);
    this->boot();
    this->tickUntilScanned();
    this->rec->clearHistory();
    EXPECT_EQ(this->listSegments(RecorderStream::TLM, 0), Rsp::OK);
    ASSERT_EQ(this->rec->eventsSegmentInfo.size(), 16u);
    EXPECT_EQ(this->rec->eventsSegmentInfo.front().seq, 1u);
    EXPECT_EQ(this->rec->eventsSegmentInfo.back().seq, 16u);
    this->rec->clearHistory();
    EXPECT_EQ(this->listSegments(RecorderStream::EVT, 0), Rsp::OK);
    ASSERT_EQ(this->rec->eventsSegmentInfo.size(), 1u);
    EXPECT_EQ(this->rec->eventsSegmentInfo[0].seq, 1u);
}

TEST_F(DataRecorderTest, DeleteSegmentRules) {
    this->seedSegment(RecorderStream::TLM, 2, NOW0 - 100, 100);
    this->seedSegment(RecorderStream::TLM, 4, NOW0 - 50, 100);
    this->boot();
    EXPECT_EQ(this->deleteSegment(RecorderStream::TLM, 2), Rsp::BUSY);
    this->tickUntilScanned();
    this->pushMany(RecorderStream::TLM, 1, TLM_FLUSH_RECORDS);
    this->tick();  // segment 5 is open now
    ASSERT_EQ(this->segmentSeqs(RecorderStream::TLM), (std::vector<U32>{2, 4, 5}));
    this->rec->clearHistory();
    EXPECT_EQ(this->deleteSegment(RecorderStream::TLM, 3), Rsp::VALIDATION_ERROR) << "absent segment";
    EXPECT_EQ(this->deleteSegment(RecorderStream::TLM, 5), Rsp::VALIDATION_ERROR) << "open segment";
    fs().failRemove = true;
    EXPECT_EQ(this->deleteSegment(RecorderStream::TLM, 2), Rsp::EXECUTION_ERROR);
    fs().failRemove = false;
    EXPECT_EQ(this->segmentSeqs(RecorderStream::TLM), (std::vector<U32>{2, 4, 5}));
    EXPECT_EQ(this->deleteSegment(RecorderStream::TLM, 2), Rsp::OK);
    ASSERT_EQ(this->rec->eventsSegmentDeleted.size(), 1u);
    EXPECT_EQ(this->rec->eventsSegmentDeleted[0].seq, 2u);
    EXPECT_EQ(this->rec->eventsSegmentDeleted[0].reason, DeleteReason::COMMAND);
    EXPECT_EQ(this->segmentSeqs(RecorderStream::TLM), (std::vector<U32>{4, 5}));
    this->tick();
    EXPECT_EQ(this->lastTlm("TlmSegmentsOnDisk"), 2u);
}

TEST_F(DataRecorderTest, CloseSegmentWritesTheRingAndTheNextRecordOpensANewSegment) {
    EXPECT_EQ(this->closeSegment(RecorderStream::TLM), Rsp::BUSY);
    this->tickUntilScanned();
    EXPECT_EQ(this->closeSegment(RecorderStream::TLM), Rsp::OK) << "nothing to do is OK";
    const std::vector<Bytes> three = this->pushMany(RecorderStream::TLM, 1, 3);
    EXPECT_EQ(this->closeSegment(RecorderStream::TLM), Rsp::OK);
    ASSERT_EQ(this->segmentSeqs(RecorderStream::TLM), std::vector<U32>{1});
    EXPECT_EQ(this->decodeSegment(RecorderStream::TLM, 1).records, three);
    this->pushMany(RecorderStream::TLM, 10, TLM_FLUSH_RECORDS);
    this->tick();
    EXPECT_EQ(this->segmentSeqs(RecorderStream::TLM), (std::vector<U32>{1, 2}));
    EXPECT_EQ(this->decodeSegment(RecorderStream::TLM, 1).records, three);
    this->pushMany(RecorderStream::TLM, 50, 2);
    fs().failWrite = true;
    EXPECT_EQ(this->closeSegment(RecorderStream::TLM), Rsp::EXECUTION_ERROR);
    fs().failWrite = false;
}

TEST_F(DataRecorderTest, DownlinkNewestClosesAndSendsTheHighestSegment) {
    EXPECT_EQ(this->downlinkNewest(RecorderStream::EVT), Rsp::BUSY);
    this->tickUntilScanned();
    this->rec->clearHistory();
    EXPECT_EQ(this->downlinkNewest(RecorderStream::TLM), Rsp::EXECUTION_ERROR) << "no segment on disk";
    EXPECT_TRUE(this->rec->sendFileOutCalls.empty());

    const std::vector<Bytes> three = this->pushMany(RecorderStream::EVT, 1, 3);
    EXPECT_EQ(this->downlinkNewest(RecorderStream::EVT), Rsp::OK);
    const std::string path = segmentPath(RecorderStream::EVT, 1);
    ASSERT_EQ(this->rec->sendFileOutCalls.size(), 1u);
    EXPECT_EQ(this->rec->sendFileOutCalls[0].source, path);
    EXPECT_EQ(this->rec->sendFileOutCalls[0].dest, path);
    EXPECT_EQ(this->rec->sendFileOutCalls[0].offset, 0u);
    EXPECT_EQ(this->rec->sendFileOutCalls[0].length, 0u);
    EXPECT_EQ(this->decodeSegment(RecorderStream::EVT, 1).records, three);
    ASSERT_EQ(this->rec->eventsSegmentInfo.size(), 1u);
    EXPECT_EQ(this->rec->eventsSegmentInfo[0].stream, RecorderStream::EVT);
    EXPECT_EQ(this->rec->eventsSegmentInfo[0].seq, 1u);
    EXPECT_EQ(this->rec->eventsSegmentInfo[0].bytes, static_cast<U32>(fs().files[path].size()));
    EXPECT_EQ(this->rec->eventsSegmentInfo[0].openTimeS, this->nowS());

    this->rec->sendFileResponse = Svc::SendFileResponse(Svc::SendFileStatus::STATUS_BUSY, 0);
    EXPECT_EQ(this->downlinkNewest(RecorderStream::EVT), Rsp::EXECUTION_ERROR);
}

TEST_F(DataRecorderTest, EachFlushIsOneBoundedWriteAndSegmentsStayUnderTheirMaximum) {
    this->tickUntilScanned();
    ASSERT_EQ(this->setFlush(RecorderStream::TLM, 1, 3600), Rsp::OK);  // flush whenever non-empty
    std::vector<Bytes> pushed;
    for (U32 round = 0; round < 12; round++) {
        const std::vector<Bytes> batch = this->pushMany(RecorderStream::TLM, round * 100, RING_SLOTS_MAX, 227);
        pushed.insert(pushed.end(), batch.begin(), batch.end());
        for (U32 t = 0; t < 3; t++) {
            const U32 writes = this->op("write");
            const U32 flushes = this->op("flush");
            const U32 removes = this->op("removeFile");
            size_t bytesBefore = 0;
            for (const auto& kv : fs().files) {
                bytesBefore += kv.second.size();
            }
            this->tick();
            size_t bytesAfter = 0;
            for (const auto& kv : fs().files) {
                bytesAfter += kv.second.size();
            }
            EXPECT_LE(this->op("write") - writes, 2u);  // at most one per stream
            EXPECT_LE(this->op("flush") - flushes, 2u);
            EXPECT_LE(this->op("removeFile") - removes, 2u);
            EXPECT_LE(bytesAfter - bytesBefore, STAGE_BYTES) << "one TLM write larger than STAGE_BYTES";
        }
    }
    this->tick(20);
    for (U32 seq : this->segmentSeqs(RecorderStream::TLM)) {
        EXPECT_LE(fs().files[segmentPath(RecorderStream::TLM, seq)].size(), TLM_SEGMENT_MAX_BYTES) << "seq " << seq;
    }
    EXPECT_EQ(this->recordsOnDisk(RecorderStream::TLM), pushed);
}

}  // namespace
