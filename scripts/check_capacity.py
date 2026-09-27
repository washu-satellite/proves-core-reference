#!/usr/bin/env python3
"""Audit every fixed table size the flight software cannot outgrow silently.

Each line compares what the tree (and, when one exists, the built dictionary)
actually uses against a constant that bounds a table, and says how much headroom
is left:

* **boot assert** — ``MAX_PACKETIZER_PACKETS``, ``MAX_PACKETIZER_CHANNELS``,
  ``CMD_DISPATCHER_DISPATCH_TABLE_SIZE``: the overflow is an ``FW_ASSERT`` during
  topology setup, long after the target build succeeded. FAIL over the limit,
  WARN from ``--warn-fraction`` of it.
* **bench constraint** — ``PRMDB_NUM_DB_ENTRIES``: the 26th distinct saved
  parameter is dropped with ``PrmDbFull`` (``Svc/PrmDb/PrmDbImpl.cpp:96-113``),
  not asserted, so this line is INFO and never FAIL.
* **fpp-loud array** — ``ActiveRateGroupOutputPorts``, ``FaultInPorts``,
  ``NUM_TASKS``: ``fpp-check`` rejects an out-of-range or duplicated output index,
  but this fork's CI never runs the target build, so the range and duplicate
  checks are re-implemented here.
* **silent mask** — ``MAX_FAULT_TYPE``: ``FaultTable::bit()`` returns an empty
  mask for a type above it (``FaultManager/FaultTable.cpp:67-70``), so an
  overflow is silent at runtime; FAIL over the limit or over the
  ``AUTHORITY_MASK`` bit width, WARN while no bit is free.

Every input has a ``--<name> PATH`` override so a check can be exercised against
a doctored copy without touching the tree. Parsing is regex only (no fpp
tooling) and the standard library only, so this runs in the host gate.

Assumption: every instance connected as ``X.RateGroupMemberOut[i]`` in
``--topology`` is a ``Svc.ActiveRateGroup``, so ``ActiveRateGroupOutputPorts``
is its array bound. All three rate groups are Active today (``instances.fpp``
31-41); a ``Svc.PassiveRateGroup`` instance would be bounded by
``PassiveRateGroupOutputPorts`` and would be reported against the wrong limit.

Exit 0 when no line FAILs, 1 when one does, 2 when an input cannot be used.

Usage: ``fprime-venv/bin/python3 scripts/check_capacity.py`` from the repo root;
``scripts/verify.sh`` runs it as the capacity-audit stage.
"""

import argparse
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SCRIPTS_DIR = Path(__file__).resolve().parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

import check_packet_set  # noqa: E402

P = REPO / "PROVESFlightControllerReference"
PROJECT_CONFIG = P / "project" / "config"
TOP = P / "ReferenceDeployment" / "Top"

#: ``--prmdb-config`` default: the project override when it exists, else the lib one.
PRMDB_OVERRIDE = PROJECT_CONFIG / "PrmDbImplCfg.hpp"
PRMDB_LIB_DEFAULT = REPO / "lib" / "fprime" / "default" / "config" / "PrmDbImplCfg.hpp"

DEFAULT_BUILD_COPY = Path(
    os.environ.get("SCALAR_BUILD_COPY")
    or Path.home() / "scalar-build/proves-core-reference"
)
#: Where a target build leaves its dictionary, relative to a search root.
DICTIONARY_GLOBS = (
    "build-artifacts/**/*TopologyDictionary.json",
    "build-fprime-automatic-zephyr*/**/*TopologyDictionary.json",
)
DICTIONARY_KEYS = ("metadata", "commands", "parameters")

WARN_FRACTION = 0.9
DASH = " — "

RATE_GROUP_RE = re.compile(r"(\w+)\.RateGroupMemberOut\[([^\]]+)\]")
FAULT_IN_RE = re.compile(r"faultManager\.faultIn\[([^\]]+)\]")
SCHED_RE = re.compile(r"taskGate\.(?:schedIn|schedOut)\[([^\]]+)\]")
MASK_RE = re.compile(r"param\s+AUTHORITY_MASK\s*:\s*U(8|16|32|64)\b")
DESCRIBE_HASH_RE = re.compile(r"-g([0-9a-fA-F]+)$")


class InputError(Exception):
    """An input file could not be read, parsed, or resolved (exit code 2)."""

    def __init__(self, what: str, path: Path | str):
        """Remember what went wrong and which file it was."""
        super().__init__(what)
        self.what = what
        self.path = path

    def message(self) -> str:
        """The single ``error: <what> (<file>)`` line printed on stderr."""
        return f"error: {self.what} ({display(self.path)})"


def display(path: Path | str) -> str:
    """Return ``path`` relative to the repo when it is inside it, else as given."""
    return check_packet_set.display(Path(path))


@dataclass(frozen=True)
class Line:
    """One constant's result line, in the output grammar of the plan."""

    name: str
    used: int | None
    limit: int
    detail: str | None
    status: str
    reason: str | None = None

    def render(self) -> str:
        """Format the line exactly as the output grammar fixes it."""
        if self.status == "SKIP":
            return f"{self.name}: ?/{self.limit} used{DASH}SKIP: {self.reason}"
        used = self.used or 0
        free = max(0, self.limit - used)
        text = f"{self.name}: {used}/{self.limit} used, {free} free"
        if self.detail:
            text += f" ({self.detail})"
        text += f"{DASH}{self.status}"
        if self.reason:
            text += f": {self.reason}"
        return text


# ---------------------------------------------------------------------------
# Readers
# ---------------------------------------------------------------------------


def read_text(path: Path, what: str) -> str:
    """Read a whole input file, or raise ``InputError``."""
    try:
        return path.read_text(encoding="utf-8")
    except OSError:
        raise InputError(f"cannot read the {what}", path) from None


def cxx_int(text: str, name: str, path: Path) -> int:
    """Return the integer ``name`` is assigned in a C++ header (enum or const)."""
    match = re.search(rf"\b{re.escape(name)}\s*=\s*(\d+)\b", text)
    if not match:
        raise InputError(f"{name} is not defined as an integer literal", path)
    return int(match.group(1))


def fpp_const(text: str, name: str, path: Path) -> int:
    """Return the integer an fpp ``constant NAME = N`` declares."""
    match = re.search(rf"^\s*constant\s+{re.escape(name)}\s*=\s*(\d+)\b", text, re.M)
    if not match:
        raise InputError(f"constant {name} is not defined as an integer literal", path)
    return int(match.group(1))


def strip_comment(line: str) -> str:
    """Return ``line`` without a trailing ``#`` comment or ``@`` annotation."""
    return re.split(r"#|@", line, maxsplit=1)[0]


def fpp_enum(text: str, name: str, path: Path) -> dict[str, int]:
    """Return ``{enumerator: value}`` for an fpp ``enum NAME [: T] { ... }``.

    An enumerator without ``= n`` takes the previous value plus one, and the
    first takes 0, which is fpp's own rule.
    """
    lines = text.splitlines()
    start = None
    for i, line in enumerate(lines):
        if re.match(rf"^\s*enum\s+{re.escape(name)}\s*(?::\s*\w+\s*)?\{{", line):
            start = i
            break
    if start is None:
        raise InputError(f"enum {name} not found", path)
    values: dict[str, int] = {}
    previous = -1
    for line in lines[start + 1 :]:
        if re.match(r"^\s*\}", line):
            return values
        body = strip_comment(line).strip().rstrip(",").strip()
        if not body:
            continue
        match = re.fullmatch(r"(\$?[A-Za-z_]\w*)(?:\s*=\s*(-?\d+))?", body)
        if not match:
            continue
        previous = int(match.group(2)) if match.group(2) is not None else previous + 1
        values[match.group(1).lstrip("$")] = previous
    raise InputError(f"enum {name} is never closed", path)


def mask_bits(text: str, path: Path) -> tuple[str, int]:
    """Return the type and bit width of ``param AUTHORITY_MASK``."""
    match = MASK_RE.search(text)
    if not match:
        raise InputError("param AUTHORITY_MASK is not declared as U8/U16/U32/U64", path)
    return f"U{match.group(1)}", int(match.group(1))


def connection_lines(text: str) -> list[str]:
    """Return every connection line of an fpp file, comments stripped."""
    lines = []
    for raw in text.splitlines():
        line = raw.split("#", 1)[0]
        if "->" in line:
            lines.append(line)
    return lines


def resolve_index(raw: str, enums: dict[str, dict[str, int]], path: Path) -> int:
    """Return a port index written as a number or as ``[Module.]Enum.NAME``."""
    text = raw.strip()
    if re.fullmatch(r"\d+", text):
        return int(text)
    parts = text.split(".")
    if len(parts) >= 2:
        values = enums.get(parts[-2])
        if values is not None and parts[-1] in values:
            return values[parts[-1]]
    raise InputError(f"port index '{text}' cannot be resolved from the enums", path)


# ---------------------------------------------------------------------------
# Dictionary
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class Dictionary:
    """The three keys of a topology dictionary this audit reads, or why there is none."""

    path: Path | None = None
    version: str | None = None
    opcodes: frozenset[int] = frozenset()
    parameters: int = 0
    absent_reason: str | None = None

    @property
    def present(self) -> bool:
        """True when a dictionary was found and parsed."""
        return self.path is not None

    def source_line(self) -> str:
        """The ``dictionary:`` line printed before the constant lines."""
        if self.present:
            return f"dictionary: {display(self.path)} (projectVersion {self.version})"
        return f"dictionary: none ({self.absent_reason})"


def newest_dictionary(root: Path) -> Path | None:
    """Return the newest-by-mtime topology dictionary under ``root``, if any."""
    found = [
        p for pattern in DICTIONARY_GLOBS for p in root.glob(pattern) if p.is_file()
    ]
    return max(found, key=lambda p: p.stat().st_mtime) if found else None


def read_dictionary(path: Path) -> Dictionary:
    """Parse the three keys of a topology dictionary, or raise ``InputError``."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except OSError:
        raise InputError("dictionary cannot be read", path) from None
    except json.JSONDecodeError:
        raise InputError("dictionary is not valid JSON", path) from None
    if not isinstance(data, dict):
        raise InputError("dictionary is not a JSON object", path)
    for key in DICTIONARY_KEYS:
        if key not in data:
            raise InputError(f"dictionary has no '{key}'", path)
    try:
        version = str(data["metadata"].get("projectVersion", "unknown"))
        opcodes = frozenset(command["opcode"] for command in data["commands"])
        parameters = len(data["parameters"])
    except (AttributeError, KeyError, TypeError):
        raise InputError(
            "dictionary metadata/commands/parameters are not the expected shape", path
        ) from None
    return Dictionary(
        path=path, version=version, opcodes=opcodes, parameters=parameters
    )


def load_dictionary(mode: str, build_copy: Path) -> Dictionary:
    """Resolve ``--dictionary auto|none|PATH`` into a ``Dictionary``."""
    if mode == "none":
        return Dictionary(absent_reason="not requested")
    if mode == "auto":
        for root in (REPO, build_copy):
            found = newest_dictionary(root)
            if found is not None:
                return read_dictionary(found)
        return Dictionary(
            absent_reason="no *TopologyDictionary.json under the repo or the build copy;"
            " run a target build"
        )
    return read_dictionary(Path(mode))


def git_head() -> str | None:
    """Return the repo's HEAD commit, or None when git cannot answer."""
    try:
        done = subprocess.run(
            ["git", "-C", str(REPO), "rev-parse", "HEAD"],
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    return done.stdout.strip() if done.returncode == 0 else None


def head_warning(dictionary: Dictionary) -> str | None:
    """Return the staleness warning when the dictionary was not built from HEAD."""
    if not dictionary.present:
        return None
    head = git_head()
    match = DESCRIBE_HASH_RE.search(dictionary.version or "")
    short = match.group(1) if match else None
    if head and short and (head.startswith(short) or short.startswith(head)):
        return None
    return (
        f"WARN: dictionary projectVersion {dictionary.version} does not match tree HEAD"
        f" {head or 'unknown'}; dictionary-based lines may be stale"
    )


# ---------------------------------------------------------------------------
# Per-constant checks
# ---------------------------------------------------------------------------


def boot_assert(
    used: int, limit: int, warn_fraction: float, reason: str
) -> tuple[str, str | None]:
    """Status of a constant whose overflow asserts at boot."""
    if used > limit:
        return "FAIL", reason
    if used >= warn_fraction * limit:
        return "WARN", None
    return "OK", None


def packetizer_lines(packets: Path, config: Path, warn_fraction: float) -> list[Line]:
    """The ``MAX_PACKETIZER_PACKETS`` and channel-limit lines, from the packet script."""
    try:
        result = check_packet_set.check(packets, config)
    except OSError:
        raise InputError(
            "cannot read the packet set or the packetizer config", packets
        ) from None
    except ValueError as exc:
        raise InputError(str(exc).split(" in ")[0], config) from None
    assert_reason = "TlmPacketizer::setPacketList asserts at boot"
    packet_status, packet_reason = boot_assert(
        len(result.packets), result.max_packets, warn_fraction, assert_reason
    )
    channel_status, channel_reason = boot_assert(
        len(result.channels), result.limit, warn_fraction, assert_reason
    )
    return [
        Line(
            "MAX_PACKETIZER_PACKETS",
            len(result.packets),
            result.max_packets,
            None,
            packet_status,
            packet_reason,
        ),
        Line(
            result.limit_name,
            len(result.channels),
            result.limit,
            f"{len(result.packet_channels)} in packets"
            f" + {len(result.omit_channels)} omitted",
            channel_status,
            channel_reason,
        ),
    ]


def dispatch_line(dictionary: Dictionary, config: Path, warn_fraction: float) -> Line:
    """The ``CMD_DISPATCHER_DISPATCH_TABLE_SIZE`` line, from the dictionary's opcodes."""
    name = "CMD_DISPATCHER_DISPATCH_TABLE_SIZE"
    limit = cxx_int(read_text(config, "command dispatcher config"), name, config)
    if not dictionary.present:
        return Line(
            name, None, limit, None, "SKIP", "no dictionary (target build needed)"
        )
    status, reason = boot_assert(
        len(dictionary.opcodes),
        limit,
        warn_fraction,
        "CommandDispatcherImpl compCmdReg_handler asserts at boot",
    )
    return Line(name, len(dictionary.opcodes), limit, None, status, reason)


def prmdb_line(dictionary: Dictionary, config: Path) -> Line:
    """The ``PRMDB_NUM_DB_ENTRIES`` line: a bench constraint, INFO but never FAIL."""
    name = "PRMDB_NUM_DB_ENTRIES"
    limit = cxx_int(read_text(config, "PrmDb config"), name, config)
    if config == PRMDB_LIB_DEFAULT:
        detail = "lib default"
    elif config == PRMDB_OVERRIDE:
        detail = "project override"
    else:
        detail = "project override" if PRMDB_OVERRIDE.exists() else "lib default"
    if not dictionary.present:
        return Line(
            name, None, limit, None, "SKIP", "no dictionary (target build needed)"
        )
    used = dictionary.parameters
    if used > limit:
        return Line(
            name,
            used,
            limit,
            detail,
            "INFO",
            f"at most {limit} parameters can be saved; save only the parameters"
            " under test (HP-07)",
        )
    return Line(name, used, limit, detail, "OK")


def out_of_range_reason(indices: list[int], limit: int, constant: str) -> list[str]:
    """Reasons for every index that no longer fits the array."""
    over = sorted({index for index in indices if index >= limit})
    return [
        f"index {index} is outside 0..{limit - 1} of {constant}; fpp-check rejects it"
        " at the target build"
        for index in over
    ]


def rate_group_lines(topology: str, limit: int, enums, path: Path) -> list[Line]:
    """One ``ActiveRateGroupOutputPorts[<instance>]`` line per connected rate group."""
    order: list[str] = []
    uses: dict[str, list[int]] = {}
    for line in connection_lines(topology):
        for match in RATE_GROUP_RE.finditer(line):
            instance = match.group(1)
            if instance not in uses:
                uses[instance] = []
                order.append(instance)
            uses[instance].append(resolve_index(match.group(2), enums, path))
    lines = []
    for instance in order:
        indices = uses[instance]
        reasons = out_of_range_reason(indices, limit, "ActiveRateGroupOutputPorts")
        reasons += [
            f"duplicate output index {index}"
            for index in sorted({i for i in indices if indices.count(i) > 1})
        ]
        lines.append(
            Line(
                f"ActiveRateGroupOutputPorts[{instance}]",
                len(indices),
                limit,
                f"max index {max(indices)}" if indices else None,
                "FAIL" if reasons else "OK",
                "; ".join(reasons) or None,
            )
        )
    return lines


def fault_in_line(topology: str, limit: int, enums, path: Path) -> Line:
    """The ``FaultInPorts`` line: distinct ``faultManager.faultIn`` slots in use."""
    indices = sorted(
        {
            resolve_index(match.group(1), enums, path)
            for line in connection_lines(topology)
            for match in FAULT_IN_RE.finditer(line)
        }
    )
    reasons = out_of_range_reason(indices, limit, "FaultInPorts")
    return Line(
        "FaultInPorts",
        len(indices),
        limit,
        f"max index {max(indices)}" if indices else None,
        "FAIL" if reasons else "OK",
        "; ".join(reasons) or None,
    )


def num_tasks_line(
    topology: str, task_gate: str, enums, path: Path, gate_path: Path
) -> Line:
    """The ``NUM_TASKS`` line: TaskGate slots in use and ``SchedTask`` ordinals."""
    limit = fpp_const(task_gate, "NUM_TASKS", gate_path)
    indices = sorted(
        {
            resolve_index(match.group(1), enums, path)
            for line in connection_lines(topology)
            for match in SCHED_RE.finditer(line)
        }
    )
    enumerators = enums["SchedTask"]
    reasons = out_of_range_reason(indices, limit, "NUM_TASKS")
    reasons += [
        f"SchedTask.{member} = {value} is outside 0..{limit - 1} of NUM_TASKS;"
        " TaskGate asserts on that ordinal"
        for member, value in sorted(enumerators.items(), key=lambda item: item[1])
        if value >= limit
    ]
    return Line(
        "NUM_TASKS",
        len(indices),
        limit,
        f"{len(enumerators)} SchedTask enumerators, max index {max(indices)}"
        if indices
        else f"{len(enumerators)} SchedTask enumerators",
        "FAIL" if reasons else "OK",
        "; ".join(reasons) or None,
    )


def fault_type_line(enums, limit: int, mask: tuple[str, int]) -> Line:
    """The ``MAX_FAULT_TYPE`` line: the mask is silent when a type outgrows it."""
    used = max(enums["FaultType"].values())
    mask_type, bits = mask
    detail = f"AUTHORITY_MASK {mask_type} = {bits} bits"
    reasons = []
    if used > limit:
        reasons.append(
            f"silent: FaultTable::bit() returns 0 for a type above MAX_FAULT_TYPE"
            f" ({limit})"
        )
    if limit > bits:
        reasons.append(
            f"MAX_FAULT_TYPE ({limit}) needs more bits than AUTHORITY_MASK"
            f" {mask_type} has ({bits})"
        )
    if reasons:
        return Line("MAX_FAULT_TYPE", used, limit, detail, "FAIL", "; ".join(reasons))
    if limit - used <= 0:
        return Line(
            "MAX_FAULT_TYPE",
            used,
            limit,
            detail,
            "WARN",
            "the next FaultType needs a wider mask",
        )
    return Line("MAX_FAULT_TYPE", used, limit, detail, "OK")


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------


def check(args: argparse.Namespace) -> tuple[list[str], list[Line]]:
    """Run every check and return the source lines and the constant lines."""
    dictionary = load_dictionary(args.dictionary, args.build_copy)
    source_lines = [dictionary.source_line()]
    warning = head_warning(dictionary)
    if warning:
        source_lines.append(warning)

    topology = read_text(args.topology, "topology")
    task_gate_text = read_text(args.task_gate, "TaskGate model")
    fault_types_text = read_text(args.fault_types, "FaultTypes model")
    enums = {
        "SchedTask": fpp_enum(task_gate_text, "SchedTask", args.task_gate),
        "FaultType": fpp_enum(fault_types_text, "FaultType", args.fault_types),
    }

    lines = packetizer_lines(args.packets, args.packetizer_config, args.warn_fraction)
    lines.append(dispatch_line(dictionary, args.cmd_config, args.warn_fraction))
    lines.append(prmdb_line(dictionary, args.prmdb_config))
    lines += rate_group_lines(
        topology,
        fpp_const(
            read_text(args.ac_constants, "AcConstants model"),
            "ActiveRateGroupOutputPorts",
            args.ac_constants,
        ),
        enums,
        args.topology,
    )
    lines.append(
        fault_in_line(
            topology,
            fpp_const(fault_types_text, "FaultInPorts", args.fault_types),
            enums,
            args.topology,
        )
    )
    lines.append(
        num_tasks_line(topology, task_gate_text, enums, args.topology, args.task_gate)
    )
    lines.append(
        fault_type_line(
            enums,
            cxx_int(
                read_text(args.fault_table, "FaultTable header"),
                "MAX_FAULT_TYPE",
                args.fault_table,
            ),
            mask_bits(
                read_text(args.fault_manager, "FaultManager model"), args.fault_manager
            ),
        )
    )
    return source_lines, lines


def default_prmdb_config() -> Path:
    """The ``--prmdb-config`` default: the project override when it exists."""
    return PRMDB_OVERRIDE if PRMDB_OVERRIDE.exists() else PRMDB_LIB_DEFAULT


def parse_args(argv: list[str] | None) -> argparse.Namespace:
    """Build the command line of the audit; every input is overridable."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--packets", type=Path, default=TOP / "ReferenceDeploymentPackets.fppi"
    )
    parser.add_argument(
        "--packetizer-config",
        type=Path,
        default=PROJECT_CONFIG / "TlmPacketizerCfg.hpp",
    )
    parser.add_argument(
        "--cmd-config",
        type=Path,
        default=PROJECT_CONFIG / "CommandDispatcherImplCfg.hpp",
    )
    parser.add_argument("--prmdb-config", type=Path, default=default_prmdb_config())
    parser.add_argument(
        "--ac-constants", type=Path, default=PROJECT_CONFIG / "AcConstants.fpp"
    )
    parser.add_argument("--topology", type=Path, default=TOP / "topology.fpp")
    parser.add_argument(
        "--task-gate", type=Path, default=P / "Components" / "TaskGate" / "TaskGate.fpp"
    )
    parser.add_argument(
        "--fault-types",
        type=Path,
        default=P / "Components" / "FaultTypes" / "FaultTypes.fpp",
    )
    parser.add_argument(
        "--fault-table",
        type=Path,
        default=P / "Components" / "FaultManager" / "FaultTable.hpp",
    )
    parser.add_argument(
        "--fault-manager",
        type=Path,
        default=P / "Components" / "FaultManager" / "FaultManager.fpp",
    )
    parser.add_argument(
        "--dictionary",
        default="auto",
        help="auto (newest built dictionary), none (skip those lines), or a path",
    )
    parser.add_argument(
        "--build-copy",
        type=Path,
        default=DEFAULT_BUILD_COPY,
        help="second search root for --dictionary auto",
    )
    parser.add_argument("--warn-fraction", type=float, default=WARN_FRACTION)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    """Print the audit and return the process exit status (0, 1 or 2)."""
    args = parse_args(argv)
    try:
        source_lines, lines = check(args)
    except InputError as error:
        print(error.message(), file=sys.stderr)
        return 2
    for text in source_lines:
        print(text)
    for line in lines:
        print(line.render())
    tally = {
        status: sum(1 for line in lines if line.status == status)
        for status in ("OK", "WARN", "INFO", "SKIP", "FAIL")
    }
    overall = "FAIL" if tally["FAIL"] else "OK"
    print(
        f"RESULT: {overall}{DASH}{tally['OK']} ok, {tally['WARN']} warn,"
        f" {tally['INFO']} info, {tally['SKIP']} skip, {tally['FAIL']} fail"
    )
    return 1 if tally["FAIL"] else 0


if __name__ == "__main__":
    sys.exit(main())
