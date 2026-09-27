#!/usr/bin/env python3
"""Check that the flight software's static view of the hardware matches the devicetree.

Two rules, one line each:

* **HWC-1 mux-channels** — every ``(tmp112|veml6031|drv2605)Face<n>Manager``
  configure call in ``--topology-cpp`` passes ``state.muxChannel<a>Device``; its
  device field is resolved through ``--main-cpp`` (``inputs.<field> = <ident>;``
  and ``const struct device* <ident> = DEVICE_DT_GET(DT_NODELABEL(<label>));``)
  to a devicetree node label, and ``--dts`` places that label inside a
  ``mux_channel_<b>: i2c_mux@<b> { ... }`` block. FAIL when any ``a != b``; each
  mismatch gets one indented offender line.
* **HWC-2 flash-size** — the size cell of ``&flash0 { reg = <ADDR SIZE>; }``
  (``DT_SIZE_M(n)``, ``DT_SIZE_K(n)``, hex or decimal) must cover the largest
  ``offset + size`` of every ``reg`` inside its ``partitions { ... }`` block.

Every input has a ``--<name> PATH`` override so a rule can be exercised against a
doctored copy without touching the tree. Parsing is regex only (no fpp or
devicetree tooling) and the standard library only, so this runs in the host gate.

Exit 0 when no line FAILs, 1 when one does, 2 when an input cannot be used
(nothing on stdout, one line on stderr).

Usage: ``fprime-venv/bin/python3 scripts/check_hardware_consistency.py`` from the
repo root.
"""

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SCRIPTS_DIR = Path(__file__).resolve().parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from check_capacity import DASH, InputError, read_text  # noqa: E402

P = REPO / "PROVESFlightControllerReference"
TOP = P / "ReferenceDeployment" / "Top"
BOARD_DTSI = (
    REPO
    / "boards"
    / "bronco_space"
    / "proves_flight_control_board_v5"
    / "proves_flight_control_board_v5.dtsi"
)

#: Comments are blanked (newlines kept) before any parsing; strings are kept.
COMMENT_RE = re.compile(r'"(?:\\.|[^"\\\n])*"|//[^\n]*|/\*.*?\*/', re.S)

CONFIGURE_RE = re.compile(
    r"^\s*((?:tmp112|veml6031|drv2605)Face\d+Manager)\.configure\(\s*"
    r"state\.tca9548aDevice\s*,\s*state\.muxChannel(\d+)Device\s*,\s*"
    r"state\.(\w+)\s*(?:,[^)]*)?\)\s*;",
    re.M,
)
INPUT_RE = re.compile(r"^\s*inputs\.(\w+)\s*=\s*(\w+)\s*;", re.M)
DEVICE_RE = re.compile(
    r"^\s*(?:static\s+)?const\s+struct\s+device\s*\*\s*(\w+)\s*=\s*"
    r"DEVICE_DT_GET\(\s*DT_NODELABEL\(\s*(\w+)\s*\)\s*\)\s*;",
    re.M,
)
MUX_BLOCK_RE = re.compile(r"\bmux_channel_(\d+)\s*:\s*i2c_mux@\w+\s*\{")
NODE_LABEL_RE = re.compile(r"^\s*(\w+)\s*:\s*[\w,.+-]+@[0-9a-fA-F]+\s*\{", re.M)
FLASH0_RE = re.compile(r"&flash0\s*\{")
PARTITIONS_RE = re.compile(r"\bpartitions\s*\{")
REG_RE = re.compile(r"\breg\s*=\s*<\s*([^\s>]+)\s+([^>]+?)\s*>\s*;")


@dataclass(frozen=True)
class Mismatch:
    """One face manager configured with a mux channel its device is not on."""

    inst: str
    passed: int
    label: str
    placed: int

    def render(self) -> str:
        """The four-space-indented offender line under HWC-1."""
        return (
            f"    {self.inst}: configure passes mux_channel_{self.passed}, "
            f"devicetree places {self.label} on mux_channel_{self.placed}"
        )


@dataclass(frozen=True)
class MuxResult:
    """HWC-1: managers checked and the ones that disagree with the devicetree."""

    checked: int
    mismatches: list[Mismatch]

    @property
    def status(self) -> str:
        """FAIL iff any manager is mismatched."""
        return "FAIL" if self.mismatches else "OK"

    def render(self) -> list[str]:
        """The HWC-1 line followed by one offender line per mismatch."""
        head = (
            f"HWC-1 mux-channels: {self.checked} managers checked, "
            f"{len(self.mismatches)} mismatched{DASH}{self.status}"
        )
        return [head] + [m.render() for m in self.mismatches]


@dataclass(frozen=True)
class FlashResult:
    """HWC-2: declared flash size against the end of the partition table."""

    reg: int
    end: int

    @property
    def status(self) -> str:
        """FAIL iff a partition ends past the declared size."""
        return "FAIL" if self.end > self.reg else "OK"

    def render(self) -> list[str]:
        """The single HWC-2 line."""
        return [
            f"HWC-2 flash-size: reg {self.reg} bytes, partitions end {self.end} bytes"
            f"{DASH}{self.status}"
        ]


def strip_comments(text: str) -> str:
    """Blank ``//`` and ``/* */`` comments, keeping newlines and string literals."""

    def blank(match: re.Match) -> str:
        token = match.group(0)
        if token.startswith('"'):
            return token
        return re.sub(r"[^\n]", " ", token)

    return COMMENT_RE.sub(blank, text)


def block_body(text: str, open_brace: int, what: str, path: Path) -> str:
    """Return the text between the ``{`` at ``open_brace`` and its matching ``}``."""
    depth = 0
    for i in range(open_brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace + 1 : i]
    raise InputError(f"unbalanced braces in {what}", path)


def dt_int(cell: str, what: str, path: Path) -> int:
    """Parse a devicetree size or offset cell: DT_SIZE_M/K(n), hex or decimal."""
    cell = cell.strip()
    for macro, scale in (("DT_SIZE_M", 1024 * 1024), ("DT_SIZE_K", 1024)):
        match = re.fullmatch(rf"{macro}\(\s*(\d+)\s*\)", cell)
        if match:
            return int(match.group(1)) * scale
    if re.fullmatch(r"0[xX][0-9a-fA-F]+", cell):
        return int(cell, 16)
    if re.fullmatch(r"\d+", cell):
        return int(cell)
    raise InputError(f"cannot read {what} cell {cell!r}", path)


def mux_labels(dts: str, path: Path) -> dict[str, int]:
    """Map every node label declared inside a ``mux_channel_<b>`` block to ``b``."""
    labels: dict[str, int] = {}
    for match in MUX_BLOCK_RE.finditer(dts):
        channel = int(match.group(1))
        body = block_body(dts, match.end() - 1, f"mux_channel_{channel}", path)
        for label in NODE_LABEL_RE.findall(body):
            labels[label] = channel
    return labels


def device_labels(main: str) -> tuple[dict[str, str], dict[str, str]]:
    """Return ``inputs.<field>`` -> identifier and identifier -> node label maps."""
    fields = dict(INPUT_RE.findall(main))
    idents = dict(DEVICE_RE.findall(main))
    return fields, idents


def check_mux(args: argparse.Namespace, dts: str) -> MuxResult:
    """HWC-1: compare each face manager's mux argument with its device's channel."""
    topology = strip_comments(read_text(args.topology_cpp, "topology source"))
    main = strip_comments(read_text(args.main_cpp, "main source"))
    calls = CONFIGURE_RE.findall(topology)
    if not calls:
        raise InputError("no face-manager configure call found", args.topology_cpp)
    fields, idents = device_labels(main)
    placed = mux_labels(dts, args.dts)
    mismatches = []
    for inst, passed, field in calls:
        if field not in fields:
            raise InputError(f"no inputs.{field} assignment", args.main_cpp)
        ident = fields[field]
        if ident not in idents:
            raise InputError(
                f"inputs.{field} = {ident} has no DEVICE_DT_GET(DT_NODELABEL(...))",
                args.main_cpp,
            )
        label = idents[ident]
        if label not in placed:
            raise InputError(
                f"{label} (inputs.{field}) is declared in no mux_channel block",
                args.dts,
            )
        if int(passed) != placed[label]:
            mismatches.append(Mismatch(inst, int(passed), label, placed[label]))
    return MuxResult(len(calls), mismatches)


def check_flash(args: argparse.Namespace, dts: str) -> FlashResult:
    """HWC-2: compare the ``&flash0`` size cell with the partition table's end."""
    node = FLASH0_RE.search(dts)
    if not node:
        raise InputError("no &flash0 block", args.dts)
    body = block_body(dts, node.end() - 1, "&flash0", args.dts)
    parts = PARTITIONS_RE.search(body)
    if not parts:
        raise InputError("no partitions block in &flash0", args.dts)
    parts_body = block_body(body, parts.end() - 1, "partitions", args.dts)
    parts_close = parts.end() + len(parts_body) + 1
    own = body[: parts.start()] + body[parts_close:]
    reg = REG_RE.search(own)
    if not reg:
        raise InputError("no reg in &flash0", args.dts)
    size = dt_int(reg.group(2), "&flash0 reg size", args.dts)
    ends = [
        dt_int(offset, "partition offset", args.dts)
        + dt_int(length, "partition size", args.dts)
        for offset, length in REG_RE.findall(parts_body)
    ]
    if not ends:
        raise InputError("no reg in the &flash0 partitions block", args.dts)
    return FlashResult(size, max(ends))


def parse_args(argv: list[str] | None) -> argparse.Namespace:
    """Parse the command line; every input has a default in the tree."""
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument(
        "--topology-cpp", type=Path, default=TOP / "ReferenceDeploymentTopology.cpp"
    )
    parser.add_argument(
        "--main-cpp", type=Path, default=P / "ReferenceDeployment" / "Main.cpp"
    )
    parser.add_argument("--dts", type=Path, default=BOARD_DTSI)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    """Print both rule lines and ``RESULT:``; return the exit status (0, 1 or 2)."""
    args = parse_args(argv)
    try:
        dts = strip_comments(read_text(args.dts, "devicetree source"))
        results = [check_mux(args, dts), check_flash(args, dts)]
    except InputError as error:
        print(error.message(), file=sys.stderr)
        return 2
    for result in results:
        for text in result.render():
            print(text)
    fail = sum(1 for result in results if result.status == "FAIL")
    ok = len(results) - fail
    overall = "FAIL" if fail else "OK"
    print(f"RESULT: {overall}{DASH}{ok} ok, 0 warn, 0 skip, {fail} fail")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
