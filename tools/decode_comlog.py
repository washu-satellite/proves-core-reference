#!/usr/bin/env python3
"""
decode_comlog.py - Decode Svc::ComLogger (.com) files using an F Prime JSON dictionary.

ComLogger file layout (with storeBufferLength=true, the default):

    +----------------+---------------------------+--------------------------+
    | U16 length (BE)| FwPacketDescriptorType    | packet body ...          |
    +----------------+---------------------------+--------------------------+
    |<-- 2 bytes --->|<----------------- `length` bytes ------------------->|

The descriptor says what the packet is (event, channelized telemetry,
packetized telemetry, ...). The body is decoded with the same decoders the
F Prime GDS uses, so values match what you would see live in the GDS.

Usage examples:
    python3 decode_comlog.py -d MyDeploymentTopologyDictionary.json logs/tlm_*.com
    python3 decode_comlog.py -d dict.json logs/*.com --sort --format csv -o all.csv
    python3 decode_comlog.py -d dict.json logs/evr_*.com --match "*Error*" "*Warn*"
    python3 decode_comlog.py -d dict.json logs/tlm_*.com --only telemetry --match "eps.*"

Requires: pip install fprime-gds  (use the version matching your F Prime release)
"""

import argparse
import csv
import fnmatch
import json
import sys
from glob import glob
from pathlib import Path

from fprime_gds.common.decoders.ch_decoder import ChDecoder
from fprime_gds.common.decoders.decoder import DecodingException
from fprime_gds.common.decoders.event_decoder import EventDecoder
from fprime_gds.common.decoders.pkt_decoder import PktDecoder
from fprime_gds.common.models.dictionaries import Dictionaries
from fprime_gds.common.utils.config_manager import ConfigManager

# ComLogger always writes the record length as a big-endian U16
LENGTH_FIELD_SIZE = 2


# ----------------------------------------------------------------------------
# Dictionary loading
# ----------------------------------------------------------------------------
def load_dictionary(dict_path, packet_set_name):
    """Load the JSON dictionary and push its type sizes into the GDS ConfigManager.

    The type sizes (FwChanIdType, FwEventIdType, FwPacketDescriptorType,
    FwTlmPacketizeIdType, ...) come from the dictionary, so the decoder follows
    whatever your project configured instead of hard-coding them.
    """
    if hasattr(Dictionaries, "load_dictionaries_into_config"):
        return Dictionaries.load_dictionaries_into_config(
            dict_path, None, packet_set_name
        )

    # Older fprime-gds releases: load and copy the type definitions by hand
    dictionaries = Dictionaries()
    dictionaries.load_dictionaries(dict_path, None, packet_set_name)
    config = ConfigManager.get_instance()
    for type_name, type_obj in getattr(dictionaries, "typedefs_name", {}).items():
        config.set_type(type_name, type_obj)
    return dictionaries


def packet_type_names():
    """Map descriptor values to names (FW_PACKET_LOG, FW_PACKET_TELEM, ...)."""
    apid_type = ConfigManager().get_type("ComCfg.Apid")
    return {value: name for name, value in apid_type.ENUM_DICT.items()}


# ----------------------------------------------------------------------------
# File parsing
# ----------------------------------------------------------------------------
def iter_records(data, filename):
    """Yield (offset, record_bytes) for each length-prefixed record in a file."""
    offset = 0
    while offset < len(data):
        if offset + LENGTH_FIELD_SIZE > len(data):
            warn(
                f"{filename}: {len(data) - offset} trailing byte(s) at offset {offset} "
                "(truncated length field, likely power loss before close)"
            )
            return
        length = int.from_bytes(data[offset : offset + LENGTH_FIELD_SIZE], "big")
        start = offset + LENGTH_FIELD_SIZE
        end = start + length
        if end > len(data):
            warn(
                f"{filename}: record at offset {offset} claims {length} bytes but only "
                f"{len(data) - start} remain (truncated record, skipped)"
            )
            return
        yield offset, data[start:end]
        offset = end


class ComLogDecoder:
    """Decodes individual ComLogger records into flat row dicts."""

    def __init__(self, dictionaries):
        self.dicts = dictionaries
        self.desc_type = ConfigManager().get_type("FwPacketDescriptorType")
        self.pkt_id_type = ConfigManager().get_type("FwTlmPacketizeIdType")
        self.type_names = packet_type_names()

        self.event_decoder = EventDecoder(dictionaries.event_id)
        self.channel_decoder = ChDecoder(dictionaries.channel_id)
        self.packet_decoder = None
        if dictionaries.packet is not None:
            self.packet_decoder = PktDecoder(
                dictionaries.packet, dictionaries.channel_id
            )

        self.stats = {}

    def _count(self, key):
        self.stats[key] = self.stats.get(key, 0) + 1

    def decode(self, record, filename, offset, time_zone):
        """Return a list of row dicts for one record (may be empty)."""
        desc_obj = self.desc_type()
        if len(record) < desc_obj.getSize():
            warn(f"{filename}@{offset}: record too short for a packet descriptor")
            self._count("errors")
            return []
        desc_obj.deserialize(record, 0)
        body = bytearray(record[desc_obj.getSize() :])
        kind = self.type_names.get(desc_obj.val, f"UNKNOWN({desc_obj.val})")

        try:
            if kind == "FW_PACKET_LOG":
                rows = [
                    self._event_row(e, time_zone)
                    for e in self.event_decoder.decode_api(body)
                ]
            elif kind == "FW_PACKET_TELEM":
                rows = [
                    self._chan_row(c, None, time_zone)
                    for c in self.channel_decoder.decode_api(body)
                ]
            elif kind == "FW_PACKET_PACKETIZED_TLM":
                rows = self._decode_packet(body, time_zone)
            else:
                self._count(f"skipped {kind}")
                return []
        except (DecodingException, Exception) as exc:  # keep going on a bad record
            warn(f"{filename}@{offset}: failed to decode {kind}: {exc}")
            self._count("errors")
            return []

        self._count(kind)
        for row in rows:
            row["file"] = filename
        return rows

    def _decode_packet(self, body, time_zone):
        if self.packet_decoder is None:
            raise DecodingException(
                "packetized telemetry found but the dictionary has no telemetry packet set"
            )
        # Check the packet ID ourselves: PktDecoder's unknown-ID path is not robust
        id_obj = self.pkt_id_type()
        id_obj.deserialize(body, 0)
        pkt_template = self.dicts.packet.get(id_obj.val)
        if pkt_template is None:
            raise DecodingException(
                f"packet id {id_obj.val} not in dictionary packet set"
            )
        chans = self.packet_decoder.decode_api(body) or []
        return [self._chan_row(c, pkt_template.get_name(), time_zone) for c in chans]

    @staticmethod
    def _sort_key(time_obj):
        return (time_obj.seconds, time_obj.useconds)

    def _chan_row(self, ch, packet_name, time_zone):
        return {
            "_sort": self._sort_key(ch.time),
            "time": ch.time.to_readable(time_zone),
            "raw_time": str(ch.time),
            "type": "TLM",
            "name": ch.template.get_full_name(),
            "id": ch.template.get_id(),
            "severity": "",
            "packet": packet_name or "",
            "value": ch.get_display_text(),
        }

    def _event_row(self, ev, time_zone):
        return {
            "_sort": self._sort_key(ev.time),
            "time": ev.time.to_readable(time_zone),
            "raw_time": str(ev.time),
            "type": "EVR",
            "name": ev.template.get_full_name(),
            "id": ev.id,
            "severity": str(ev.template.get_severity()).replace("EventSeverity.", ""),
            "packet": "",
            "value": ev.get_display_text(),
        }


# ----------------------------------------------------------------------------
# Output
# ----------------------------------------------------------------------------
FIELDS = [
    "time",
    "raw_time",
    "type",
    "name",
    "id",
    "severity",
    "packet",
    "value",
    "file",
]


def write_rows(rows, fmt, out):
    if fmt == "csv":
        writer = csv.DictWriter(out, fieldnames=FIELDS, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)
    elif fmt == "jsonl":
        for row in rows:
            out.write(json.dumps({k: row[k] for k in FIELDS}) + "\n")
    else:  # text
        for row in rows:
            if row["type"] == "EVR":
                out.write(
                    f"{row['time']}  EVR  {row['severity']:<11} {row['name']}: {row['value']}\n"
                )
            else:
                out.write(f"{row['time']}  TLM  {row['name']} = {row['value']}\n")


def warn(msg):
    print(f"[WARN] {msg}", file=sys.stderr)


def expand_inputs(patterns):
    files = []
    for pattern in patterns:
        matches = sorted(glob(pattern))
        files.extend(matches if matches else [pattern])
    # Skip ComLogger's hash/validation side files if a glob picked them up
    return [f for f in files if not f.endswith(".hash")]


def main():
    parser = argparse.ArgumentParser(
        description="Decode Svc::ComLogger .com files with an F Prime JSON dictionary."
    )
    parser.add_argument("files", nargs="+", help=".com files or glob patterns")
    parser.add_argument(
        "-d",
        "--dictionary",
        required=True,
        help="Deployment JSON dictionary (e.g. build-artifacts/.../*TopologyDictionary.json)",
    )
    parser.add_argument(
        "--packet-set-name",
        default=None,
        help="Telemetry packet set to use if the dictionary defines more than one",
    )
    parser.add_argument(
        "-f", "--format", choices=["text", "csv", "jsonl"], default="text"
    )
    parser.add_argument("-o", "--output", help="Output file (default: stdout)")
    parser.add_argument(
        "--only",
        choices=["events", "telemetry"],
        help="Keep only events or only telemetry",
    )
    parser.add_argument(
        "--match",
        nargs="+",
        metavar="PATTERN",
        help="Keep only names matching any of these glob patterns, e.g. 'eps.*' '*Error*'",
    )
    parser.add_argument(
        "--sort",
        action="store_true",
        help="Sort all output by on-board time (default: file order)",
    )
    parser.add_argument(
        "--utc", action="store_true", help="Print times in UTC instead of local time"
    )
    args = parser.parse_args()

    time_zone = None
    if args.utc:
        from datetime import timezone

        time_zone = timezone.utc

    decoder = ComLogDecoder(load_dictionary(args.dictionary, args.packet_set_name))

    rows = []
    for filename in expand_inputs(args.files):
        path = Path(filename)
        if not path.is_file():
            warn(f"{filename}: not found, skipping")
            continue
        data = path.read_bytes()
        for offset, record in iter_records(data, path.name):
            rows.extend(decoder.decode(record, path.name, offset, time_zone))

    if args.only == "events":
        rows = [r for r in rows if r["type"] == "EVR"]
    elif args.only == "telemetry":
        rows = [r for r in rows if r["type"] == "TLM"]
    if args.match:
        rows = [
            r for r in rows if any(fnmatch.fnmatch(r["name"], p) for p in args.match)
        ]
    if args.sort:
        rows.sort(key=lambda r: r["_sort"])

    if args.output:
        newline = "" if args.format == "csv" else None
        with open(args.output, "w", newline=newline) as out:
            write_rows(rows, args.format, out)
    else:
        write_rows(rows, args.format, sys.stdout)

    summary = (
        ", ".join(f"{k}: {v}" for k, v in sorted(decoder.stats.items())) or "no records"
    )
    print(f"[INFO] {len(rows)} rows written ({summary})", file=sys.stderr)


if __name__ == "__main__":
    main()
