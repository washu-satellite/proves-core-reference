#!/usr/bin/env python3
"""Decode DataRecorder segment files into CSV using the deployment dictionary.

Usage::

    recorder_reader.py --dictionary DICT.json [--output OUT.csv] SEGMENT [SEGMENT ...]

A segment (``/rec/<tlm|evt>/NNNNNNNN.bin`` on the flight SD card, fetched with
``dataRecorder.DOWNLINK_NEWEST`` or ``fileDownlink.SendFile``) is a 20-byte
header followed by records; formats in
``docs-site/dev-loop/cycles/cycle-m-plan/01-normative.md`` section 5, command
line and output in section 10.

* Header (little-endian): magic ``SEG1``, version 1, stream u8, boot count u16,
  open seconds u32, open microseconds u32, CRC-32 over bytes 0..15.
* Record: ``len`` u16 LE (1..FW_COM_BUFFER_MAX_SIZE), ``len`` bytes of the
  ``Fw::ComBuffer`` exactly as received (F' big-endian serialization), CRC-32
  LE over the ``len`` field and the bytes. Reading stops at the first record
  that fails.

CSV columns: ``segment,record,stream,kind,id,name,time_base,time_context,
seconds,useconds,value``. A telemetry packet (descriptor 4) gives one ``tlm``
row per packet member, each member occupying its maximum serialized size; an
event (descriptor 2) gives one ``evt`` row whose value is ``name=value`` pairs
joined by ``;``; anything else is one ``unknown`` row with the packet in hex.

Stderr: one ``<path>: <n> records, stop=<END|TRUNCATED|BAD_LENGTH|BAD_CRC>``
line per segment. Exit 0 when every segment ends cleanly, 1 when any stopped
early (rows before the stop are still written), 2 for an unreadable file or
dictionary or an invalid header (one ``error: ...`` line).

Standard library only.
"""

from __future__ import annotations

import argparse
import csv
import json
import struct
import sys
import zlib
from pathlib import Path
from typing import Any, TextIO

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

SEGMENT_MAGIC = b"SEG1"
SEGMENT_VERSION = 1
HEADER_SIZE = 20
RECORD_OVERHEAD = 6
#: FW_COM_BUFFER_MAX_SIZE at this tree (FpConstants.fpp:22); a dictionary
#: constant of that name overrides it.
DEFAULT_MAX_PAYLOAD = 227

DESCRIPTOR_EVENT = 2
DESCRIPTOR_TLM_PACKET = 4

#: Fallback stream names when the dictionary has no Components.RecorderStream.
DEFAULT_STREAMS = {0: "TLM", 1: "EVT"}

EXIT_CLEAN = 0
EXIT_STOPPED = 1
EXIT_ERROR = 2


class ReaderError(Exception):
    """A condition that ends the run with exit 2 and one ``error:`` line."""


class DecodeError(Exception):
    """A packet whose bytes do not match the dictionary (reported as ``unknown``)."""


# ---------------------------------------------------------------------------
# Dictionary: types and widths
# ---------------------------------------------------------------------------

INT_FORMATS = {
    (8, False): "B",
    (16, False): "H",
    (32, False): "I",
    (64, False): "Q",
    (8, True): "b",
    (16, True): "h",
    (32, True): "i",
    (64, True): "q",
}
PRIMITIVES = {
    "U8": {"kind": "integer", "size": 8, "signed": False},
    "U16": {"kind": "integer", "size": 16, "signed": False},
    "U32": {"kind": "integer", "size": 32, "signed": False},
    "U64": {"kind": "integer", "size": 64, "signed": False},
    "I8": {"kind": "integer", "size": 8, "signed": True},
    "I16": {"kind": "integer", "size": 16, "signed": True},
    "I32": {"kind": "integer", "size": 32, "signed": True},
    "I64": {"kind": "integer", "size": 64, "signed": True},
    "F32": {"kind": "float", "size": 32},
    "F64": {"kind": "float", "size": 64},
    "bool": {"kind": "bool"},
}


class Dictionary:
    """The parts of an F' JSON dictionary the reader needs."""

    def __init__(self, data: dict[str, Any]) -> None:
        self.types: dict[str, dict[str, Any]] = {
            t["qualifiedName"]: t for t in data.get("typeDefinitions", [])
        }
        self.channels = {c["name"]: c for c in data.get("telemetryChannels", [])}
        self.events = {int(e["id"]): e for e in data.get("events", [])}
        packet_sets = data.get("telemetryPacketSets") or [{}]
        self.packets = {int(p["id"]): p for p in packet_sets[0].get("members", [])}
        self.max_payload = DEFAULT_MAX_PAYLOAD
        for constant in data.get("constants", []):
            if constant.get("qualifiedName") == "FW_COM_BUFFER_MAX_SIZE":
                self.max_payload = int(constant["value"])
        self.streams = dict(DEFAULT_STREAMS)
        stream_enum = self.types.get("Components.RecorderStream")
        if stream_enum is not None:
            self.streams = {
                int(c["value"]): c["name"] for c in stream_enum["enumeratedConstants"]
            }

    def alias(self, name: str, default: str) -> dict[str, Any]:
        """The underlying type of an F' alias such as FwEventIdType."""
        definition = self.types.get(name)
        if definition is None:
            return {"name": default, **PRIMITIVES[default]}
        return definition["underlyingType"]

    def resolve(self, type_ref: dict[str, Any]) -> dict[str, Any]:
        """Follow qualified identifiers to a concrete type description."""
        kind = type_ref.get("kind")
        if kind == "qualifiedIdentifier":
            name = type_ref["name"]
            if name in PRIMITIVES:
                return {"name": name, **PRIMITIVES[name]}
            definition = self.types.get(name)
            if definition is None:
                raise DecodeError(f"unknown type {name}")
            if definition["kind"] == "alias":
                return self.resolve(definition["underlyingType"])
            return definition
        return type_ref

    def max_size(self, type_ref: dict[str, Any]) -> int:
        """Maximum serialized size in bytes."""
        t = self.resolve(type_ref)
        kind = t["kind"]
        if kind in ("integer", "float"):
            return t["size"] // 8
        if kind == "bool":
            return 1
        if kind == "string":
            return self.size_store_bytes() + int(t["size"])
        if kind == "enum":
            return t["representationType"]["size"] // 8
        if kind == "array":
            return int(t["size"]) * self.max_size(t["elementType"])
        if kind == "struct":
            return sum(
                int(m.get("size", 1)) * self.max_size(m["type"])
                for m in t["members"].values()
            )
        raise DecodeError(f"unsupported type kind {kind}")

    def size_store_bytes(self) -> int:
        return self.alias("FwSizeStoreType", "U16")["size"] // 8


# ---------------------------------------------------------------------------
# Big-endian value decoding (F' serialization)
# ---------------------------------------------------------------------------


class Cursor:
    """Reads big-endian fields from a packet."""

    def __init__(self, data: bytes) -> None:
        self.data = data
        self.offset = 0

    def take(self, count: int) -> bytes:
        if self.offset + count > len(self.data):
            raise DecodeError("packet shorter than its dictionary layout")
        chunk = self.data[self.offset : self.offset + count]
        self.offset += count
        return chunk

    def integer(self, size_bits: int, signed: bool) -> int:
        fmt = ">" + INT_FORMATS[(size_bits, signed)]
        return struct.unpack(fmt, self.take(size_bits // 8))[0]

    def primitive(self, type_ref: dict[str, Any]) -> int:
        return self.integer(type_ref["size"], bool(type_ref.get("signed", False)))


def decode_value(cursor: Cursor, dictionary: Dictionary, type_ref: dict[str, Any]):
    """Decode one value as a Python object (enums as names)."""
    t = dictionary.resolve(type_ref)
    kind = t["kind"]
    if kind == "integer":
        return cursor.primitive(t)
    if kind == "float":
        fmt = ">f" if t["size"] == 32 else ">d"
        return struct.unpack(fmt, cursor.take(t["size"] // 8))[0]
    if kind == "bool":
        return cursor.take(1)[0] != 0
    if kind == "string":
        length = cursor.integer(dictionary.size_store_bytes() * 8, False)
        return cursor.take(length).decode("utf-8", errors="replace")
    if kind == "enum":
        rep = t["representationType"]
        raw = cursor.primitive(rep)
        for constant in t["enumeratedConstants"]:
            if int(constant["value"]) == raw:
                return constant["name"]
        return raw
    if kind == "array":
        return [
            decode_value(cursor, dictionary, t["elementType"])
            for _ in range(int(t["size"]))
        ]
    if kind == "struct":
        out = {}
        members = sorted(t["members"].items(), key=lambda kv: kv[1].get("index", 0))
        for name, member in members:
            count = int(member.get("size", 0))
            if count:
                out[name] = [
                    decode_value(cursor, dictionary, member["type"])
                    for _ in range(count)
                ]
            else:
                out[name] = decode_value(cursor, dictionary, member["type"])
        return out
    raise DecodeError(f"unsupported type kind {kind}")


def format_value(value) -> str:
    """Integers decimal, floats repr, bools true/false, containers compact JSON."""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, float):
        return repr(value)
    if isinstance(value, (list, dict)):
        return json.dumps(value, separators=(",", ":"))
    return str(value)


# ---------------------------------------------------------------------------
# Packets
# ---------------------------------------------------------------------------


def read_time(cursor: Cursor, dictionary: Dictionary) -> tuple[int, int, int, int]:
    base = cursor.primitive(dictionary.alias("FwTimeBaseStoreType", "U16"))
    context = cursor.primitive(dictionary.alias("FwTimeContextStoreType", "U8"))
    seconds = cursor.integer(32, False)
    useconds = cursor.integer(32, False)
    return base, context, seconds, useconds


def packet_rows(packet: bytes, dictionary: Dictionary) -> list[dict[str, str]]:
    """CSV fields (without segment/record/stream) for one recorded packet."""
    try:
        cursor = Cursor(packet)
        descriptor = cursor.primitive(dictionary.alias("FwPacketDescriptorType", "U16"))
        if descriptor == DESCRIPTOR_TLM_PACKET:
            packet_id = cursor.primitive(
                dictionary.alias("FwTlmPacketizeIdType", "U16")
            )
            definition = dictionary.packets.get(packet_id)
            if definition is None:
                raise DecodeError(f"unknown packet id {packet_id}")
            time = read_time(cursor, dictionary)
            rows = []
            for member in definition["members"]:
                channel = dictionary.channels.get(member)
                if channel is None:
                    raise DecodeError(f"unknown channel {member}")
                width = dictionary.max_size(channel["type"])
                start = cursor.offset
                value = decode_value(cursor, dictionary, channel["type"])
                cursor.offset = start
                cursor.take(width)
                rows.append(row_fields("tlm", packet_id, member, time, value))
            return rows
        if descriptor == DESCRIPTOR_EVENT:
            event_id = cursor.primitive(dictionary.alias("FwEventIdType", "U32"))
            definition = dictionary.events.get(event_id)
            if definition is None:
                raise DecodeError(f"unknown event id {event_id}")
            time = read_time(cursor, dictionary)
            pairs = [
                f"{param['name']}="
                + format_value(decode_value(cursor, dictionary, param["type"]))
                for param in definition.get("formalParams", [])
            ]
            return [
                row_fields(
                    "evt", event_id, definition["name"], time, None, ";".join(pairs)
                )
            ]
        raise DecodeError(f"descriptor {descriptor}")
    except DecodeError:
        return [
            {
                "kind": "unknown",
                "id": "",
                "name": "",
                "time_base": "",
                "time_context": "",
                "seconds": "",
                "useconds": "",
                "value": packet.hex(),
            }
        ]


def row_fields(kind, ident, name, time, value, text=None) -> dict[str, str]:
    base, context, seconds, useconds = time
    return {
        "kind": kind,
        "id": str(ident),
        "name": name,
        "time_base": str(base),
        "time_context": str(context),
        "seconds": str(seconds),
        "useconds": str(useconds),
        "value": text if text is not None else format_value(value),
    }


# ---------------------------------------------------------------------------
# Segments
# ---------------------------------------------------------------------------


def decode_header(data: bytes) -> int:
    """Validate the segment header; return the stream byte."""
    if len(data) < HEADER_SIZE:
        raise ReaderError("header truncated")
    if data[0:4] != SEGMENT_MAGIC:
        raise ReaderError("bad segment magic")
    (crc,) = struct.unpack("<I", data[16:20])
    if zlib.crc32(data[0:16]) != crc:
        raise ReaderError("bad header CRC")
    if data[4] != SEGMENT_VERSION:
        raise ReaderError(f"unsupported segment version {data[4]}")
    return data[5]


def decode_records(data: bytes, max_payload: int) -> tuple[list[bytes], str]:
    """Records after the header, and the stop reason."""
    records = []
    offset = HEADER_SIZE
    while True:
        remaining = len(data) - offset
        if remaining == 0:
            return records, "END"
        if remaining < 2:
            return records, "TRUNCATED"
        (declared,) = struct.unpack("<H", data[offset : offset + 2])
        if declared == 0 or declared > max_payload:
            return records, "BAD_LENGTH"
        if remaining < declared + RECORD_OVERHEAD:
            return records, "TRUNCATED"
        framed = data[offset : offset + 2 + declared]
        (crc,) = struct.unpack(
            "<I", data[offset + 2 + declared : offset + RECORD_OVERHEAD + declared]
        )
        if zlib.crc32(framed) != crc:
            return records, "BAD_CRC"
        records.append(framed[2:])
        offset += declared + RECORD_OVERHEAD


def load_dictionary(path: str) -> Dictionary:
    try:
        with open(path, encoding="utf-8") as handle:
            return Dictionary(json.load(handle))
    except (OSError, ValueError, KeyError, TypeError) as exc:
        raise ReaderError(f"cannot read dictionary {path}: {exc}") from exc


def run(dictionary_path: str, segments: list[str], out: TextIO) -> int:
    dictionary = load_dictionary(dictionary_path)
    writer = csv.writer(out, lineterminator="\n")
    writer.writerow(CSV_HEADER)
    result = EXIT_CLEAN
    for segment in segments:
        try:
            data = Path(segment).read_bytes()
        except OSError as exc:
            raise ReaderError(f"cannot read segment {segment}: {exc}") from exc
        try:
            stream_byte = decode_header(data)
        except ReaderError as exc:
            raise ReaderError(f"{segment}: {exc}") from exc
        stream = dictionary.streams.get(stream_byte, str(stream_byte))
        records, stop = decode_records(data, dictionary.max_payload)
        for index, packet in enumerate(records):
            for fields in packet_rows(packet, dictionary):
                writer.writerow(
                    [segment, str(index), stream]
                    + [fields[column] for column in CSV_HEADER[3:]]
                )
        print(f"{segment}: {len(records)} records, stop={stop}", file=sys.stderr)
        if stop != "END":
            result = EXIT_STOPPED
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Decode DataRecorder segment files into CSV."
    )
    parser.add_argument(
        "--dictionary", required=True, help="deployment dictionary JSON"
    )
    parser.add_argument("--output", help="CSV file to write (default: stdout)")
    parser.add_argument("segments", nargs="+", metavar="SEGMENT")
    args = parser.parse_args(argv)
    try:
        if args.output:
            with open(args.output, "w", encoding="utf-8", newline="") as out:
                return run(args.dictionary, args.segments, out)
        return run(args.dictionary, args.segments, sys.stdout)
    except ReaderError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return EXIT_ERROR


if __name__ == "__main__":
    sys.exit(main())
