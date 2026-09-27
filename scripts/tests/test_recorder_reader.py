"""Tests for ``tools/recorder_reader.py``, claiming DataRecorder-12.

Written from ``docs-site/dev-loop/cycles/cycle-m-plan/01-normative.md`` section
5.2-5.4 (segment bytes and the 57-byte golden segment) and section 10 (the
reader's command line, CSV columns, stderr lines and exit codes). The reader is
run as a subprocess and only stdout/stderr/exit code and the ``--output`` file
are asserted.

The dictionary is the fixture ``fixtures/recorder_reader_dictionary.json``
(review amendment 5: never the build-copy dictionary). It has the real
dictionary's shape (``typeDefinitions`` aliases, ``telemetryChannels``,
``events`` with ``formalParams``, ``telemetryPacketSets[0].members``) with
just the FileSystem packet, the DataRecorder ``SegmentDeleted`` event and one
fixture event carrying an I16, an F32 and a bool.

Segments other than the golden one are built here byte by byte from sections
5.2/5.3: little-endian header and record framing, CRC-32 (IEEE, ``zlib.crc32``)
and F' big-endian packet bytes inside each record.
"""

import csv
import io
import struct
import subprocess
import sys
import zlib
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
READER = REPO / "tools" / "recorder_reader.py"
DICTIONARY = (
    Path(__file__).resolve().parent / "fixtures" / "recorder_reader_dictionary.json"
)

CSV_HEADER = [
    "segment",
    "record",
    "stream",
    "kind",
    "id",
    "name",
    "time_base",
    "time_context",
    "seconds",
    "useconds",
    "value",
]

#: Section 5.4: the 57-byte golden segment (stream TLM, 1000 s 5 us, one
#: FileSystem packet with FreeSpace 4000000000 and TotalSpace 4294967296).
GOLDEN_SEGMENT = bytes.fromhex(
    "53 45 47 31 01 00 FF FF E8 03 00 00 05 00 00 00 E5 D8 63 6F"
    "1F 00 00 04 00 05 00 02 00 00 00 03 E8 00 00 00 05 00 00 00 00 EE 6B 28"
    "00 00 00 00 01 00 00 00 00 37 92 21 A6"
)

STREAM_TLM = 0
STREAM_EVT = 1
DESCRIPTOR_EVENT = 2
DESCRIPTOR_TLM_PACKET = 4
TIME_BASE_WORKSTATION = 2

SEGMENT_DELETED_ID = 268959746
TEMPERATURE_ID = 268959747
DELETE_REASON_AGE = 1


def segment_header(stream: int, seconds: int, useconds: int = 0) -> bytes:
    """Section 5.2: magic, version 1, stream, boot count 0xFFFF, open time, CRC-32 over bytes 0..15."""
    head = b"SEG1" + struct.pack("<BBHII", 1, stream, 0xFFFF, seconds, useconds)
    return head + struct.pack("<I", zlib.crc32(head))


def record(packet: bytes) -> bytes:
    """Section 5.3: len u16 LE, the bytes, CRC-32 LE over len and bytes."""
    framed = struct.pack("<H", len(packet)) + packet
    return framed + struct.pack("<I", zlib.crc32(framed))


def fw_time(
    seconds: int, useconds: int, base: int = TIME_BASE_WORKSTATION, context: int = 0
) -> bytes:
    """F' Fw::Time serialization: base U16, context U8, seconds U32, useconds U32, big-endian."""
    return struct.pack(">HBII", base, context, seconds, useconds)


def segment_deleted_event(
    seconds: int, useconds: int, stream: int, seq: int, reason: int
) -> bytes:
    """An event packet for dataRecorder.SegmentDeleted(stream, seq, reason)."""
    return (
        struct.pack(">HI", DESCRIPTOR_EVENT, SEGMENT_DELETED_ID)
        + fw_time(seconds, useconds)
        + struct.pack(">BIB", stream, seq, reason)
    )


def temperature_event(
    seconds: int, useconds: int, sensor: int, degrees: float, latched: bool
) -> bytes:
    """An event packet for the fixture Temperature(sensor: I16, degrees: F32, latched: bool)."""
    return (
        struct.pack(">HI", DESCRIPTOR_EVENT, TEMPERATURE_ID)
        + fw_time(seconds, useconds)
        + struct.pack(">hfB", sensor, degrees, 1 if latched else 0)
    )


def run_reader(*args: str) -> subprocess.CompletedProcess:
    """Run the reader with the project interpreter from the repo root."""
    return subprocess.run(
        [sys.executable, str(READER), *args],
        cwd=REPO,
        capture_output=True,
        text=True,
        timeout=60,
    )


def csv_rows(text: str) -> list[list[str]]:
    """Parse CSV text into rows (header included)."""
    return list(csv.reader(io.StringIO(text)))


def error_lines(stderr: str) -> list[str]:
    """Stderr lines of the ``error: ...`` form."""
    return [line for line in stderr.splitlines() if line.startswith("error: ")]


@pytest.fixture
def golden(tmp_path: Path) -> Path:
    """The golden segment written to a file."""
    path = tmp_path / "00000001.bin"
    path.write_bytes(GOLDEN_SEGMENT)
    return path


@pytest.mark.verifies("DataRecorder-12")
def test_golden_segment_decodes_to_two_tlm_rows(golden: Path):
    """The golden segment gives the two FileSystem members, one row each, and exit 0."""
    proc = run_reader("--dictionary", str(DICTIONARY), str(golden))
    assert proc.returncode == 0, proc.stderr
    rows = csv_rows(proc.stdout)
    assert rows[0] == CSV_HEADER
    body = [dict(zip(CSV_HEADER, r)) for r in rows[1:]]
    assert len(body) == 2, proc.stdout
    expected = [
        ("ReferenceDeployment.fsSpace.FreeSpace", "4000000000"),
        ("ReferenceDeployment.fsSpace.TotalSpace", "4294967296"),
    ]
    for row, (name, value) in zip(body, expected):
        assert row["segment"] == str(golden)
        assert row["record"] == "0"
        assert row["stream"] == "TLM"
        assert row["kind"] == "tlm"
        assert row["id"] == "5"
        assert row["name"] == name
        assert row["time_base"] == "2"
        assert row["time_context"] == "0"
        assert row["seconds"] == "1000"
        assert row["useconds"] == "5"
        assert row["value"] == value


@pytest.mark.verifies("DataRecorder-12")
def test_golden_segment_stderr_summary_and_output_file(golden: Path, tmp_path: Path):
    """--output receives the same CSV; stderr carries one ``<path>: 1 records, stop=END`` line."""
    out = tmp_path / "out.csv"
    proc = run_reader(
        "--dictionary", str(DICTIONARY), "--output", str(out), str(golden)
    )
    assert proc.returncode == 0, proc.stderr
    assert proc.stdout == ""
    rows = csv_rows(out.read_text(encoding="utf-8"))
    assert rows[0] == CSV_HEADER
    assert len(rows) == 3
    assert proc.stderr.splitlines() == [f"{golden}: 1 records, stop=END"]


@pytest.mark.verifies("DataRecorder-12")
def test_multi_record_event_segment_decodes_each_argument_by_name(tmp_path: Path):
    """Two event records give two evt rows whose value names every argument."""
    path = tmp_path / "00000007.bin"
    path.write_bytes(
        segment_header(STREAM_EVT, 2000)
        + record(segment_deleted_event(2001, 17, STREAM_EVT, 7, DELETE_REASON_AGE))
        + record(temperature_event(2002, 0, -3, 21.5, True))
    )
    proc = run_reader("--dictionary", str(DICTIONARY), str(path))
    assert proc.returncode == 0, proc.stderr
    rows = csv_rows(proc.stdout)
    assert rows[0] == CSV_HEADER
    body = [dict(zip(CSV_HEADER, r)) for r in rows[1:]]
    assert len(body) == 2, proc.stdout

    first, second = body
    assert first["record"] == "0"
    assert first["stream"] == "EVT"
    assert first["kind"] == "evt"
    assert first["id"] == str(SEGMENT_DELETED_ID)
    assert first["name"] == "ReferenceDeployment.dataRecorder.SegmentDeleted"
    assert (first["time_base"], first["time_context"]) == ("2", "0")
    assert (first["seconds"], first["useconds"]) == ("2001", "17")
    assert first["value"] == "stream=EVT;seq=7;reason=AGE"

    assert second["record"] == "1"
    assert second["kind"] == "evt"
    assert second["id"] == str(TEMPERATURE_ID)
    assert second["name"] == "ReferenceDeployment.fixture.Temperature"
    assert (second["seconds"], second["useconds"]) == ("2002", "0")
    assert second["value"] == "sensor=-3;degrees=21.5;latched=true"
    assert proc.stderr.splitlines() == [f"{path}: 2 records, stop=END"]


@pytest.mark.verifies("DataRecorder-12")
def test_corrupted_record_keeps_rows_before_it_and_exits_1(tmp_path: Path):
    """A bad CRC in the second record: the first record's row is still written, exit 1."""
    good = record(segment_deleted_event(3001, 0, STREAM_EVT, 3, DELETE_REASON_AGE))
    damaged = bytearray(record(temperature_event(3002, 0, 4, 1.0, False)))
    damaged[5] ^= 0xFF  # inside the packet bytes
    path = tmp_path / "00000003.bin"
    path.write_bytes(segment_header(STREAM_EVT, 3000) + good + bytes(damaged))
    proc = run_reader("--dictionary", str(DICTIONARY), str(path))
    assert proc.returncode == 1, proc.stderr
    rows = csv_rows(proc.stdout)
    assert rows[0] == CSV_HEADER
    body = [dict(zip(CSV_HEADER, r)) for r in rows[1:]]
    assert len(body) == 1, proc.stdout
    assert body[0]["value"] == "stream=EVT;seq=3;reason=AGE"
    assert proc.stderr.splitlines() == [f"{path}: 1 records, stop=BAD_CRC"]


@pytest.mark.verifies("DataRecorder-12")
def test_truncated_golden_segment_keeps_nothing_and_exits_1(tmp_path: Path):
    """The golden segment cut inside its record stops TRUNCATED with no row, exit 1."""
    path = tmp_path / "00000001.bin"
    path.write_bytes(GOLDEN_SEGMENT[:-3])
    proc = run_reader("--dictionary", str(DICTIONARY), str(path))
    assert proc.returncode == 1, proc.stderr
    rows = csv_rows(proc.stdout)
    assert rows == [CSV_HEADER]
    assert proc.stderr.splitlines() == [f"{path}: 0 records, stop=TRUNCATED"]


@pytest.mark.verifies("DataRecorder-12")
def test_clean_and_damaged_segments_together_exit_1_with_all_good_rows(
    golden: Path, tmp_path: Path
):
    """Exit 1 when any segment stops early; rows from every segment are written."""
    damaged = tmp_path / "00000002.bin"
    damaged.write_bytes(
        segment_header(STREAM_EVT, 4000)
        + record(segment_deleted_event(4001, 0, STREAM_TLM, 9, 0))
        + b"\x05"  # one stray byte: a truncated length field
    )
    proc = run_reader("--dictionary", str(DICTIONARY), str(golden), str(damaged))
    assert proc.returncode == 1, proc.stderr
    body = [dict(zip(CSV_HEADER, r)) for r in csv_rows(proc.stdout)[1:]]
    assert [r["segment"] for r in body] == [str(golden), str(golden), str(damaged)]
    assert body[2]["value"] == "stream=TLM;seq=9;reason=CAPACITY"
    assert proc.stderr.splitlines() == [
        f"{golden}: 1 records, stop=END",
        f"{damaged}: 1 records, stop=TRUNCATED",
    ]


@pytest.mark.verifies("DataRecorder-12")
@pytest.mark.parametrize(
    "offset",
    [0, 4, 5, 8, 16],
    ids=["magic", "version", "stream", "open-seconds", "header-crc"],
)
def test_invalid_header_exits_2_with_one_error_line(tmp_path: Path, offset: int):
    """A corrupted header byte gives exit 2 and exactly one ``error: ...`` line."""
    bad = bytearray(GOLDEN_SEGMENT)
    bad[offset] ^= 0x01
    path = tmp_path / "00000001.bin"
    path.write_bytes(bytes(bad))
    proc = run_reader("--dictionary", str(DICTIONARY), str(path))
    assert proc.returncode == 2
    assert len(error_lines(proc.stderr)) == 1, proc.stderr


@pytest.mark.verifies("DataRecorder-12")
def test_short_header_exits_2_with_one_error_line(tmp_path: Path):
    """A file shorter than the 20-byte header is an invalid header: exit 2."""
    path = tmp_path / "00000001.bin"
    path.write_bytes(GOLDEN_SEGMENT[:19])
    proc = run_reader("--dictionary", str(DICTIONARY), str(path))
    assert proc.returncode == 2
    assert len(error_lines(proc.stderr)) == 1, proc.stderr


@pytest.mark.verifies("DataRecorder-12")
def test_missing_dictionary_exits_2_with_one_error_line(golden: Path, tmp_path: Path):
    """An absent dictionary file gives exit 2 and one error line."""
    proc = run_reader("--dictionary", str(tmp_path / "absent.json"), str(golden))
    assert proc.returncode == 2
    assert len(error_lines(proc.stderr)) == 1, proc.stderr


@pytest.mark.verifies("DataRecorder-12")
def test_unparseable_dictionary_exits_2_with_one_error_line(
    golden: Path, tmp_path: Path
):
    """A dictionary that is not JSON gives exit 2 and one error line."""
    broken = tmp_path / "broken.json"
    broken.write_text("{ not json", encoding="utf-8")
    proc = run_reader("--dictionary", str(broken), str(golden))
    assert proc.returncode == 2
    assert len(error_lines(proc.stderr)) == 1, proc.stderr


def test_unreadable_segment_exits_2_with_one_error_line(tmp_path: Path):
    """Section 10: an unreadable segment file is exit 2 with one error line."""
    proc = run_reader("--dictionary", str(DICTIONARY), str(tmp_path / "absent.bin"))
    assert proc.returncode == 2
    assert len(error_lines(proc.stderr)) == 1, proc.stderr
