#!/usr/bin/env python3
"""Check the telemetry packet set against the TlmPacketizer configuration.

``Svc::TlmPacketizer::setPacketList`` takes one hash bucket per distinct channel
named in ``ReferenceDeploymentPackets.fppi``: every channel inside a ``packet``
block *and* every channel inside the ``omit { ... }`` block (``findBucket`` is
called for both lists and asserts when no bucket is free). It also asserts when
the packet count exceeds ``MAX_PACKETIZER_PACKETS``. Both asserts fire at boot,
after the target build has already succeeded, so this script enforces the two
limits on the host:

* packets declared  <= ``MAX_PACKETIZER_PACKETS``
* distinct channels <= ``TLMPACKETIZER_HASH_BUCKETS``

Exit status 1 with a message when either limit is exceeded, else 0. A warning
(no failure) is printed when the channel count is above 90 % of the buckets.

Usage: ``fprime-venv/bin/python3 scripts/check_packet_set.py`` from the repo root.
"""

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

# The packet set is included by ``topology.fpp`` inside this topology's scope, so a
# two-component name (``fsSpace.FreeSpace``) resolves to this module's instance.
ENCLOSING_MODULE = "ReferenceDeployment"

PACKET_RE = re.compile(r"^\s*packet\s+(\w+)\s+id\s+(\d+)\s+group\s+(\d+)")
# A channel reference: two or more dotted identifiers. FPP escapes an identifier
# that collides with a keyword with a leading ``$`` (``CdhCore.$health``).
CHANNEL_RE = re.compile(r"^\s*(\$?[A-Za-z_]\w*(?:\.\$?[A-Za-z_]\w*)+)\s*$")


def strip_comment(line: str) -> str:
    """Return ``line`` without a trailing ``#`` comment."""
    return line.split("#", 1)[0]


def qualify(name: str) -> str:
    """Return the fully-qualified form of a channel reference.

    Drops FPP's ``$`` keyword escape and prefixes ``instance.channel`` with the
    enclosing module, so the same channel named both ways counts once.
    """
    name = name.replace("$", "")
    if name.count(".") == 1:
        return f"{ENCLOSING_MODULE}.{name}"
    return name


def parse_packet_set(text: str) -> tuple[list[tuple[str, int, int]], set[str]]:
    """Return (packet declarations, distinct channel names) from a packet-set file.

    Channels are collected from the whole file, so both packet members and the
    ``omit`` block are counted, matching what the packetizer allocates buckets for.
    """
    packets: list[tuple[str, int, int]] = []
    channels: set[str] = set()
    for raw in text.splitlines():
        line = strip_comment(raw)
        m = PACKET_RE.match(line)
        if m:
            packets.append((m.group(1), int(m.group(2)), int(m.group(3))))
            continue
        m = CHANNEL_RE.match(line)
        if m:
            channels.add(qualify(m.group(1)))
    return packets, channels


def parse_limit(text: str, name: str) -> int:
    """Return the integer assigned to ``name`` in a C++ config header."""
    m = re.search(rf"\b{re.escape(name)}\s*=\s*(\d+)\s*;", text)
    if not m:
        raise ValueError(f"{name} not found in {PACKETIZER_CFG}")
    return int(m.group(1))


def main() -> int:
    packets, channels = parse_packet_set(PACKETS_FPPI.read_text())
    cfg = PACKETIZER_CFG.read_text()
    max_packets = parse_limit(cfg, "MAX_PACKETIZER_PACKETS")
    buckets = parse_limit(cfg, "TLMPACKETIZER_HASH_BUCKETS")

    print(f"packet set:  {PACKETS_FPPI.relative_to(REPO)}")
    print(f"config:      {PACKETIZER_CFG.relative_to(REPO)}")
    print(
        f"packets:     {len(packets)} declared, MAX_PACKETIZER_PACKETS = {max_packets}"
    )
    print(
        f"channels:    {len(channels)} distinct (packets + omit), TLMPACKETIZER_HASH_BUCKETS = {buckets}"
    )

    status = 0
    if len(packets) > max_packets:
        print(
            f"FAIL: {len(packets)} packets > MAX_PACKETIZER_PACKETS ({max_packets}); "
            "TlmPacketizer::setPacketList asserts at boot"
        )
        status = 1
    if len(channels) > buckets:
        print(
            f"FAIL: {len(channels)} distinct channels > TLMPACKETIZER_HASH_BUCKETS ({buckets}); "
            "TlmPacketizer::findBucket asserts at boot"
        )
        status = 1
    elif len(channels) > WARN_FRACTION * buckets:
        print(
            f"WARN: {len(channels)} distinct channels is above {WARN_FRACTION:.0%} of "
            f"TLMPACKETIZER_HASH_BUCKETS ({buckets}); raise the limit before adding more"
        )
    if status == 0:
        print("OK: packet set fits the packetizer configuration")
    return status


if __name__ == "__main__":
    sys.exit(main())
