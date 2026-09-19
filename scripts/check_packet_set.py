#!/usr/bin/env python3
"""Check the telemetry packet set against the TlmPacketizer configuration.

``Svc::TlmPacketizer::setPacketList`` bounds two things that only assert at boot,
after the target build has already succeeded:

* the number of ``packet`` blocks in ``ReferenceDeploymentPackets.fppi`` must be
  <= ``MAX_PACKETIZER_PACKETS``;
* the number of *distinct* channels named in the file, counting every channel
  inside a ``packet`` block **and** every channel inside the ``omit { ... }``
  block, must be <= the packetizer's channel limit. Before F' 4.2.2 that limit is
  ``TLMPACKETIZER_HASH_BUCKETS`` (``findBucket`` takes one bucket per channel in
  either list); from 4.2.2 it is ``MAX_PACKETIZER_CHANNELS`` (packet and omit
  channels are inserted into one ``RedBlackTreeMap`` of that capacity,
  ``TlmPacketizer.cpp:86-87,148-149`` at 4.3.0).

This script enforces both limits on the host. It reads whichever channel-limit
constant the config header defines (``MAX_PACKETIZER_CHANNELS`` is preferred when
both are present) and reports which one it used.

Parsing follows the packet-set syntax on both sides of the sync: ``packet NAME id
N group G { ... }`` blocks and one ``omit { ... }`` block. A channel line is a
dotted name (``instance.channel`` or fully qualified); a name with one dot is
qualified with the enclosing module so the same channel named both ways counts
once. Only lines inside a packet or omit block are counted.

Exit status 1 with a message when either limit is exceeded, else 0. A warning
(no failure) is printed when the channel count is above 90 % of the limit.

Usage: ``fprime-venv/bin/python3 scripts/check_packet_set.py`` from the repo root.
``--packets`` and ``--config`` point it at other copies of the two files.
"""

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PACKETS_FPPI = (
    REPO
    / "PROVESFlightControllerReference"
    / "ReferenceDeployment"
    / "Top"
    / "ReferenceDeploymentPackets.fppi"
)
PACKETIZER_CFG = (
    REPO
    / "PROVESFlightControllerReference"
    / "project"
    / "config"
    / "TlmPacketizerCfg.hpp"
)

WARN_FRACTION = 0.9

# Channel-limit constants, in order of preference when both are defined.
CHANNEL_LIMIT_NAMES = ("MAX_PACKETIZER_CHANNELS", "TLMPACKETIZER_HASH_BUCKETS")

# The packet set is included by ``topology.fpp`` inside this topology's scope, so a
# two-component name (``fsSpace.FreeSpace``) resolves to this module's instance.
ENCLOSING_MODULE = "ReferenceDeployment"

PACKET_RE = re.compile(r"^\s*packet\s+(\w+)\s+id\s+(\d+)\s+group\s+(\d+)")
OMIT_RE = re.compile(r"^\s*(?:\}\s*)?omit\s*\{")
CLOSE_RE = re.compile(r"^\s*\}\s*$")
# A channel reference: one or more dotted identifiers. FPP escapes an identifier
# that collides with a keyword with a leading ``$`` (``CdhCore.$health``).
CHANNEL_RE = re.compile(r"^\s*(\$?[A-Za-z_]\w*(?:\.\$?[A-Za-z_]\w*)*)\s*$")


def strip_comment(line: str) -> str:
    """Return ``line`` without a trailing ``#`` comment."""
    return line.split("#", 1)[0]


def qualify(name: str) -> str:
    """Return the fully-qualified form of a channel reference.

    Drops FPP's ``$`` keyword escape and prefixes ``instance.channel`` with the
    enclosing module, so the same channel named both ways counts once. A name
    with no dot or with two or more dots is returned as written.
    """
    name = name.replace("$", "")
    if name.count(".") == 1:
        return f"{ENCLOSING_MODULE}.{name}"
    return name


def parse_packet_set(
    text: str,
) -> tuple[list[tuple[str, int, int]], set[str], set[str]]:
    """Return (packet declarations, packet channels, omit channels).

    Channels are collected only inside ``packet { ... }`` and ``omit { ... }``
    blocks; the two sets are the distinct names in each. Their union is what the
    packetizer allocates for.
    """
    packets: list[tuple[str, int, int]] = []
    packet_channels: set[str] = set()
    omit_channels: set[str] = set()
    block: str | None = None  # "packet", "omit" or None
    for raw in text.splitlines():
        line = strip_comment(raw)
        m = PACKET_RE.match(line)
        if m:
            packets.append((m.group(1), int(m.group(2)), int(m.group(3))))
            block = "packet"
            continue
        if OMIT_RE.match(line):
            block = "omit"
            continue
        if CLOSE_RE.match(line):
            block = None
            continue
        m = CHANNEL_RE.match(line)
        if m and block == "packet":
            packet_channels.add(qualify(m.group(1)))
        elif m and block == "omit":
            omit_channels.add(qualify(m.group(1)))
    return packets, packet_channels, omit_channels


def parse_limit(text: str, name: str, source: Path) -> int:
    """Return the integer assigned to ``name`` in a C++ config header."""
    m = re.search(rf"\b{re.escape(name)}\s*=\s*(\d+)\s*;", text)
    if not m:
        raise ValueError(f"{name} not found in {source}")
    return int(m.group(1))


def find_channel_limit(text: str, source: Path) -> tuple[str, int]:
    """Return (constant name, value) for the channel limit the header defines."""
    for name in CHANNEL_LIMIT_NAMES:
        try:
            return name, parse_limit(text, name, source)
        except ValueError:
            continue
    raise ValueError(f"none of {', '.join(CHANNEL_LIMIT_NAMES)} found in {source}")


def display(path: Path) -> str:
    """Return ``path`` relative to the repo when it is inside it, else as given."""
    try:
        return str(path.resolve().relative_to(REPO))
    except ValueError:
        return str(path)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--packets", type=Path, default=PACKETS_FPPI, help="packet-set .fppi file"
    )
    parser.add_argument(
        "--config", type=Path, default=PACKETIZER_CFG, help="TlmPacketizerCfg.hpp"
    )
    args = parser.parse_args(argv)

    packets, packet_channels, omit_channels = parse_packet_set(args.packets.read_text())
    channels = packet_channels | omit_channels
    cfg = args.config.read_text()
    max_packets = parse_limit(cfg, "MAX_PACKETIZER_PACKETS", args.config)
    limit_name, limit = find_channel_limit(cfg, args.config)

    print(f"packet set:  {display(args.packets)}")
    print(f"config:      {display(args.config)}")
    print(
        f"packets:     {len(packets)} declared, MAX_PACKETIZER_PACKETS = {max_packets}"
    )
    print(
        f"channels:    {len(channels)} distinct "
        f"({len(packet_channels)} in packets + {len(omit_channels)} omitted), "
        f"{limit_name} = {limit}"
    )

    status = 0
    if len(packets) > max_packets:
        print(
            f"FAIL: {len(packets)} packets > MAX_PACKETIZER_PACKETS ({max_packets}); "
            "TlmPacketizer::setPacketList asserts at boot"
        )
        status = 1
    if len(channels) > limit:
        print(
            f"FAIL: {len(channels)} distinct channels > {limit_name} ({limit}); "
            "TlmPacketizer::setPacketList asserts at boot"
        )
        status = 1
    elif len(channels) > WARN_FRACTION * limit:
        print(
            f"WARN: {len(channels)} distinct channels is above {WARN_FRACTION:.0%} of "
            f"{limit_name} ({limit}); raise the limit before adding more"
        )
    if status == 0:
        print("OK: packet set fits the packetizer configuration")
    return status


if __name__ == "__main__":
    sys.exit(main())
