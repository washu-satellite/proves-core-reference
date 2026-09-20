"""Shared fixtures for the verification-tooling tests (the ``AUDIT-*`` rows).

Every test in this directory drives ``scripts/check_capacity.py`` or
``scripts/verify.sh`` from the command line and asserts only on stdout, stderr
and the exit code, parsed with the output grammar in
``docs-site/dev-loop/cycles/cycle-h-plan/01-normative.md`` section 1.3. The tree
is never edited: a fixture copies a default input file into ``tmp_path`` and
doctors the copy, and every dictionary is a synthetic three-key JSON file
(``metadata.projectVersion``, ``commands[*].opcode``, ``parameters[*].id``).
No test reads ``~/scalar-build``: ``--dictionary`` is always given explicitly and
``SCALAR_BUILD_COPY`` is removed from the environment.
"""

import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
P = REPO / "PROVESFlightControllerReference"
SCRIPT = Path("scripts") / "check_capacity.py"
PACKET_SCRIPT = Path("scripts") / "check_packet_set.py"
VERIFY_SH = Path("scripts") / "verify.sh"

# The em dash of the output grammar: U+2014 with one space on each side.
DASH = " — "


def prmdb_default() -> Path:
    """Return the ``--prmdb-config`` default: the project override if present, else lib."""
    override = P / "project" / "config" / "PrmDbImplCfg.hpp"
    if override.exists():
        return override
    return REPO / "lib" / "fprime" / "default" / "config" / "PrmDbImplCfg.hpp"


#: Default input per ``--<flag>`` (01-normative.md section 1.1).
DEFAULT_INPUTS = {
    "packets": P / "ReferenceDeployment" / "Top" / "ReferenceDeploymentPackets.fppi",
    "packetizer-config": P / "project" / "config" / "TlmPacketizerCfg.hpp",
    "cmd-config": P / "project" / "config" / "CommandDispatcherImplCfg.hpp",
    "prmdb-config": prmdb_default(),
    "ac-constants": P / "project" / "config" / "AcConstants.fpp",
    "topology": P / "ReferenceDeployment" / "Top" / "topology.fpp",
    "task-gate": P / "Components" / "TaskGate" / "TaskGate.fpp",
    "fault-types": P / "Components" / "FaultTypes" / "FaultTypes.fpp",
    "fault-table": P / "Components" / "FaultManager" / "FaultTable.hpp",
    "fault-manager": P / "Components" / "FaultManager" / "FaultManager.fpp",
}

# ---------------------------------------------------------------------------
# Output grammar (01-normative.md section 1.3)
# ---------------------------------------------------------------------------

_NAME = r"(?P<name>[A-Za-z_]\w*(?:\[[A-Za-z_]\w*\])?)"
CONSTANT_LINE_RE = re.compile(
    rf"^{_NAME}: (?:"
    r"(?P<used>\d+)/(?P<limit>\d+) used, (?P<free>\d+) free"
    r"(?: \((?P<detail>[^()]*)\))?"
    r" — (?P<status>OK|WARN|INFO|FAIL)(?:: (?P<reason>.+))?"
    r"|\?/(?P<skip_limit>\d+) used — SKIP: (?P<skip_reason>.+)"
    r")$"
)
RESULT_RE = re.compile(
    r"^RESULT: (?P<status>OK|FAIL) — (?P<ok>\d+) ok, (?P<warn>\d+) warn, "
    r"(?P<info>\d+) info, (?P<skip>\d+) skip, (?P<fail>\d+) fail$"
)
DICTIONARY_LINE_RE = re.compile(
    r"^dictionary: (?:(?P<path>.+) \(projectVersion (?P<version>[^()]+)\)"
    r"|none \((?P<reason>.+)\))$"
)
HEAD_WARN_PREFIX = "WARN: dictionary projectVersion"
ERROR_LINE_RE = re.compile(r"^error: .+ \(.+\)$")

#: Constant names in the order section 1.3 fixes (rate-group lines sit between
#: PRMDB_NUM_DB_ENTRIES and FaultInPorts, one per instance).
LINE_ORDER_BEFORE_RATE_GROUPS = (
    "MAX_PACKETIZER_PACKETS",
    "MAX_PACKETIZER_CHANNELS",
    "CMD_DISPATCHER_DISPATCH_TABLE_SIZE",
    "PRMDB_NUM_DB_ENTRIES",
)
LINE_ORDER_AFTER_RATE_GROUPS = ("FaultInPorts", "NUM_TASKS", "MAX_FAULT_TYPE")
RATE_GROUP_PREFIX = "ActiveRateGroupOutputPorts["


@dataclass(frozen=True)
class ConstantLine:
    """One parsed constant line of the audit output."""

    name: str
    status: str
    limit: int
    used: int | None
    free: int | None
    detail: str | None
    reason: str | None
    raw: str


def parse_constant_line(raw: str) -> ConstantLine | None:
    """Return the parsed form of ``raw`` if it is a well-formed constant line."""
    m = CONSTANT_LINE_RE.match(raw)
    if not m:
        return None
    if m.group("skip_limit") is not None:
        return ConstantLine(
            name=m.group("name"),
            status="SKIP",
            limit=int(m.group("skip_limit")),
            used=None,
            free=None,
            detail=None,
            reason=m.group("skip_reason"),
            raw=raw,
        )
    return ConstantLine(
        name=m.group("name"),
        status=m.group("status"),
        limit=int(m.group("limit")),
        used=int(m.group("used")),
        free=int(m.group("free")),
        detail=m.group("detail"),
        reason=m.group("reason"),
        raw=raw,
    )


@dataclass
class CapacityRun:
    """A finished ``check_capacity.py`` run and the grammar-level view of it."""

    proc: subprocess.CompletedProcess

    @property
    def returncode(self) -> int:
        """The script's exit code."""
        return self.proc.returncode

    @property
    def stdout_lines(self) -> list[str]:
        """Stdout split into lines, trailing newline dropped."""
        return self.proc.stdout.splitlines()

    @property
    def constant_lines(self) -> list[ConstantLine]:
        """Every well-formed constant line, in output order."""
        parsed = (parse_constant_line(line) for line in self.stdout_lines)
        return [c for c in parsed if c is not None]

    def line(self, name: str) -> ConstantLine:
        """Return the constant line ``name``, asserting it is present and well-formed."""
        raw = [line for line in self.stdout_lines if line.startswith(f"{name}: ")]
        assert raw, f"no line for {name} in stdout:\n{self.proc.stdout}"
        assert len(raw) == 1, f"{name} printed more than once:\n{self.proc.stdout}"
        parsed = parse_constant_line(raw[0])
        assert parsed is not None, f"line for {name} is not well-formed: {raw[0]!r}"
        return parsed

    def result(self) -> re.Match:
        """Return the parsed ``RESULT:`` line, asserting it is the last stdout line."""
        assert self.stdout_lines, f"empty stdout; stderr:\n{self.proc.stderr}"
        m = RESULT_RE.match(self.stdout_lines[-1])
        assert m, f"last line is not a RESULT line: {self.stdout_lines[-1]!r}"
        return m

    def dictionary_line(self) -> re.Match:
        """Return the parsed ``dictionary:`` source line, asserting exactly one exists."""
        found = [line for line in self.stdout_lines if line.startswith("dictionary: ")]
        assert len(found) == 1, f"expected one dictionary: line, got {found!r}"
        m = DICTIONARY_LINE_RE.match(found[0])
        assert m, f"dictionary line is not well-formed: {found[0]!r}"
        return m

    def head_warning_lines(self) -> list[str]:
        """Stdout lines starting with the projectVersion-mismatch warning prefix."""
        return [line for line in self.stdout_lines if line.startswith(HEAD_WARN_PREFIX)]

    def error_lines(self) -> list[str]:
        """Stderr lines of the ``error: <what> (<file>)`` form."""
        return [
            line for line in self.proc.stderr.splitlines() if ERROR_LINE_RE.match(line)
        ]


def scrubbed_env() -> dict[str, str]:
    """The current environment without the build-copy override (amendment 2)."""
    env = dict(os.environ)
    env.pop("SCALAR_BUILD_COPY", None)
    return env


def run_capacity(
    dictionary: str | os.PathLike = "none",
    build_copy: os.PathLike | None = None,
    warn_fraction: float | None = None,
    **inputs: os.PathLike,
) -> CapacityRun:
    """Run ``scripts/check_capacity.py`` from the repo root and capture everything.

    ``dictionary`` is always passed explicitly (``none``, ``auto`` or a path) so no
    run falls back to the ``~/scalar-build`` search. Keyword ``inputs`` map an input
    flag with underscores (``packetizer_config``) to a doctored copy.
    """
    args = [sys.executable, str(SCRIPT), "--dictionary", str(dictionary)]
    if build_copy is not None:
        args += ["--build-copy", str(build_copy)]
    if warn_fraction is not None:
        args += ["--warn-fraction", str(warn_fraction)]
    for flag, path in inputs.items():
        args += [f"--{flag.replace('_', '-')}", str(path)]
    proc = subprocess.run(
        args,
        cwd=REPO,
        env=scrubbed_env(),
        capture_output=True,
        text=True,
        timeout=60,
    )
    return CapacityRun(proc)


class Doctor:
    """Builds doctored copies of the default input files under ``tmp_path``."""

    def __init__(self, tmp_path: Path):
        """Remember where copies go."""
        self.tmp_path = tmp_path

    def copy(self, flag: str) -> Path:
        """Copy the default input for ``--<flag>`` into ``tmp_path`` and return it."""
        src = DEFAULT_INPUTS[flag]
        dst = self.tmp_path / flag / src.name
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, dst)
        return dst

    @staticmethod
    def read_int(path: os.PathLike, pattern: str) -> int:
        """Return the integer captured by group 1 of ``pattern`` in ``path``."""
        m = re.search(pattern, Path(path).read_text(encoding="utf-8"), re.M)
        assert m, f"{pattern!r} not found in {path}"
        return int(m.group(1))

    @staticmethod
    def _lines(path: Path) -> list[str]:
        """Read ``path`` as a list of lines."""
        return path.read_text(encoding="utf-8").splitlines()

    @staticmethod
    def _write(path: Path, lines: list[str]) -> None:
        """Write ``lines`` back to ``path`` with a trailing newline."""
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    @classmethod
    def insert_after_last(cls, path: Path, pattern: str, new_line: str) -> None:
        """Insert ``new_line`` after the last line matching ``pattern``."""
        lines = cls._lines(path)
        hits = [i for i, line in enumerate(lines) if re.search(pattern, line)]
        assert hits, f"{pattern!r} not found in {path}"
        lines.insert(hits[-1] + 1, new_line)
        cls._write(path, lines)

    @classmethod
    def insert_before_first(
        cls, path: Path, pattern: str, new_lines: list[str]
    ) -> None:
        """Insert ``new_lines`` before the first line matching ``pattern``."""
        lines = cls._lines(path)
        hits = [i for i, line in enumerate(lines) if re.search(pattern, line)]
        assert hits, f"{pattern!r} not found in {path}"
        lines[hits[0] : hits[0]] = new_lines
        cls._write(path, lines)

    @classmethod
    def insert_before_last(cls, path: Path, pattern: str, new_lines: list[str]) -> None:
        """Insert ``new_lines`` before the last line matching ``pattern``."""
        lines = cls._lines(path)
        hits = [i for i, line in enumerate(lines) if re.search(pattern, line)]
        assert hits, f"{pattern!r} not found in {path}"
        lines[hits[-1] : hits[-1]] = new_lines
        cls._write(path, lines)

    @classmethod
    def insert_in_block(cls, path: Path, block_start: str, new_line: str) -> None:
        """Insert ``new_line`` as the last line of the ``{ ... }`` block opened at ``block_start``."""
        lines = cls._lines(path)
        starts = [i for i, line in enumerate(lines) if re.search(block_start, line)]
        assert starts, f"{block_start!r} not found in {path}"
        closes = [
            i for i in range(starts[0] + 1, len(lines)) if re.match(r"^\s*\}", lines[i])
        ]
        assert closes, f"block at {block_start!r} never closes in {path}"
        lines.insert(closes[0], new_line)
        cls._write(path, lines)

    @staticmethod
    def replace(path: Path, pattern: str, repl: str) -> None:
        """Replace exactly one match of ``pattern`` in ``path`` with ``repl``."""
        text = path.read_text(encoding="utf-8")
        text, n = re.subn(pattern, repl, text, count=1, flags=re.M)
        assert n == 1, f"{pattern!r} not found in {path}"
        path.write_text(text, encoding="utf-8")

    def first_index(self, path: Path, pattern: str) -> int:
        """Return group 1 of the first line matching ``pattern`` as an int."""
        for line in self._lines(path):
            m = re.search(pattern, line)
            if m:
                return int(m.group(1))
        raise AssertionError(f"{pattern!r} not found in {path}")

    def dictionary(
        self,
        opcodes: list[int],
        param_ids: list[int],
        version: str = "v1.2.0-65-g0000000",
        name: str = "SyntheticTopologyDictionary.json",
        directory: Path | None = None,
    ) -> Path:
        """Write a synthetic three-key dictionary and return its path."""
        directory = directory or (self.tmp_path / "dictionary")
        directory.mkdir(parents=True, exist_ok=True)
        path = directory / name
        path.write_text(
            json.dumps(
                {
                    "metadata": {"projectVersion": version},
                    "commands": [
                        {"name": f"synth.CMD_{i}", "opcode": op}
                        for i, op in enumerate(opcodes)
                    ],
                    "parameters": [
                        {"name": f"synth.PRM_{i}", "id": pid}
                        for i, pid in enumerate(param_ids)
                    ],
                }
            ),
            encoding="utf-8",
        )
        return path


def pytest_configure(config: pytest.Config) -> None:
    """Register the ``verifies`` marker; the root pytest.ini does too, this is idempotent."""
    config.addinivalue_line(
        "markers",
        "verifies(*requirement_ids): links a test to the requirement IDs it verifies;"
        " consumed by scripts/generate_rtm.py",
    )


@pytest.fixture
def doctor(tmp_path: Path) -> Doctor:
    """A ``Doctor`` writing doctored copies under this test's ``tmp_path``."""
    return Doctor(tmp_path)


@pytest.fixture
def run():
    """The ``run_capacity`` helper, as a fixture so tests need no conftest import."""
    return run_capacity


@pytest.fixture(scope="module")
def tree_run() -> CapacityRun:
    """One run on the untouched tree with ``--dictionary none`` (amendment 2)."""
    return run_capacity()
