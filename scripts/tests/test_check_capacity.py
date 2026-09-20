"""Tests for ``scripts/check_capacity.py``, claiming AUDIT-1..8.

Each test asserts one clause of its row's pass criteria
(``docs-site/requirements/tooling.md``) from the command line: doctored copies
of the default inputs live in ``tmp_path``, dictionaries are synthetic, and on
the untouched tree a test asserts only that a line is present, well-formed, not
FAIL, with exit 0 (cycle-h-review amendment 1). ``--dictionary`` is always
explicit so nothing here depends on ``~/scalar-build`` (amendment 2).
"""

import json
import math
import os
import re
import subprocess
import sys
import time
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
PACKET_SCRIPT = Path("scripts") / "check_packet_set.py"

#: Harm-table pin: the standalone ``check_packet_set.py`` output at 6b22f72d,
#: recorded in ``cycle-h-plan/04-findings.md`` section 10.
PINNED_PACKET_SET_OUTPUT = (
    "packet set:  PROVESFlightControllerReference/ReferenceDeployment/Top/ReferenceDeploymentPackets.fppi\n"
    "config:      PROVESFlightControllerReference/project/config/TlmPacketizerCfg.hpp\n"
    "packets:     23 declared, MAX_PACKETIZER_PACKETS = 24\n"
    "channels:    244 distinct (174 in packets + 70 omitted), MAX_PACKETIZER_CHANNELS = 256\n"
    "WARN: 244 distinct channels is above 90% of MAX_PACKETIZER_CHANNELS (256); raise the limit before adding more\n"
    "OK: packet set fits the packetizer configuration\n"
)

PACKETS_LIMIT_RE = r"\bMAX_PACKETIZER_PACKETS\s*=\s*(\d+)"
CHANNELS_LIMIT_RE = r"\bMAX_PACKETIZER_CHANNELS\s*=\s*(\d+)"
DISPATCH_LIMIT_RE = r"\bCMD_DISPATCHER_DISPATCH_TABLE_SIZE\s*=\s*(\d+)"
PRMDB_LIMIT_RE = r"\bPRMDB_NUM_DB_ENTRIES\s*=\s*(\d+)"
RATE_GROUP_LIMIT_RE = r"^constant ActiveRateGroupOutputPorts\s*=\s*(\d+)"
FAULT_IN_LIMIT_RE = r"^\s*constant FaultInPorts\s*=\s*(\d+)"
NUM_TASKS_RE = r"^\s*constant NUM_TASKS\s*=\s*(\d+)"
MAX_FAULT_TYPE_RE = r"\bMAX_FAULT_TYPE\s*=\s*(\d+)"
AUTHORITY_MASK_RE = r"(param AUTHORITY_MASK:\s*)U(?:8|16|32)"

CHANNELS_DETAIL_RE = re.compile(r"^(\d+) in packets \+ (\d+) omitted$")
MAX_INDEX_DETAIL_RE = re.compile(r"^max index (\d+)$")
NUM_TASKS_DETAIL_RE = re.compile(r"^(\d+) SchedTask enumerators, max index (\d+)$")
MASK_DETAIL_RE = re.compile(r"^AUTHORITY_MASK U(8|16|32) = (8|16|32) bits$")
PRMDB_DETAIL_RE = re.compile(r"^(project override|lib default)$")


def synthetic_packet_blocks(count: int, first_id: int = 900) -> list[str]:
    """Packet blocks that reuse one existing channel, so only the packet count moves."""
    lines = []
    for i in range(count):
        lines += [
            f"  packet Synth{i} id {first_id + i} group 1 {{",
            "    startupManager.BootCount",
            "  }",
            "",
        ]
    return lines


# ---------------------------------------------------------------------------
# AUDIT-1  MAX_PACKETIZER_PACKETS
# ---------------------------------------------------------------------------


@pytest.mark.verifies("AUDIT-1")
def test_audit1_packets_over_limit_fails(run, doctor):
    """A --packets copy with L+1 packet blocks gives FAIL and exit 1."""
    limit = doctor.read_int(doctor.copy("packetizer-config"), PACKETS_LIMIT_RE)
    packets = doctor.copy("packets")
    declared, _ = packet_counts_of(packets)
    assert declared <= limit, "fixture assumes the tree is not already over the limit"
    doctor.insert_before_first(
        packets, r"^\}\s*omit\s*\{", synthetic_packet_blocks(limit + 1 - declared)
    )
    r = run(packets=packets)
    line = r.line("MAX_PACKETIZER_PACKETS")
    assert (line.used, line.limit, line.free) == (limit + 1, limit, 0)
    assert line.status == "FAIL"
    assert r.returncode == 1
    assert r.result().group("status") == "FAIL"


@pytest.mark.verifies("AUDIT-1")
def test_audit1_packets_at_limit_exits_zero(run, doctor):
    """L packet blocks give exit 0."""
    limit = doctor.read_int(doctor.copy("packetizer-config"), PACKETS_LIMIT_RE)
    packets = doctor.copy("packets")
    declared, _ = packet_counts_of(packets)
    assert declared <= limit, "fixture assumes the tree is not already over the limit"
    doctor.insert_before_first(
        packets, r"^\}\s*omit\s*\{", synthetic_packet_blocks(limit - declared)
    )
    r = run(packets=packets)
    line = r.line("MAX_PACKETIZER_PACKETS")
    assert (line.used, line.limit, line.free) == (limit, limit, 0)
    assert line.status != "FAIL"
    assert r.returncode == 0


@pytest.mark.verifies("AUDIT-1")
def test_audit1_current_tree_line(tree_run):
    """On the current tree the line is present, well-formed, not FAIL, exit 0."""
    line = tree_run.line("MAX_PACKETIZER_PACKETS")
    assert line.status != "FAIL"
    assert tree_run.returncode == 0


# ---------------------------------------------------------------------------
# AUDIT-2  MAX_PACKETIZER_CHANNELS
# ---------------------------------------------------------------------------


def packet_counts_of(packets: Path) -> tuple[int, int]:
    """(packet blocks, distinct channels) of ``packets`` per the pinned packet script."""
    proc = subprocess.run(
        [sys.executable, str(PACKET_SCRIPT), "--packets", str(packets)],
        cwd=REPO,
        capture_output=True,
        text=True,
        timeout=60,
    )
    declared = re.search(r"^packets:\s+(\d+) declared", proc.stdout, re.M)
    distinct = re.search(r"^channels:\s+(\d+) distinct", proc.stdout, re.M)
    assert declared and distinct, f"unexpected check_packet_set output:\n{proc.stdout}"
    return int(declared.group(1)), int(distinct.group(1))


def synthetic_omit_channels(count: int) -> list[str]:
    """Distinct never-seen channel names (one dot each, so they qualify uniquely)."""
    return [f"  synthChannel{i}.Value" for i in range(count)]


@pytest.mark.verifies("AUDIT-2")
def test_audit2_channels_over_limit_fails(run, doctor):
    """A --packets copy naming L+1 distinct channels gives FAIL and exit 1."""
    limit = doctor.read_int(doctor.copy("packetizer-config"), CHANNELS_LIMIT_RE)
    packets = doctor.copy("packets")
    _, distinct = packet_counts_of(packets)
    assert distinct <= limit, "fixture assumes the tree is not already over the limit"
    doctor.insert_before_last(
        packets, r"^\}\s*$", synthetic_omit_channels(limit + 1 - distinct)
    )
    r = run(packets=packets)
    line = r.line("MAX_PACKETIZER_CHANNELS")
    assert (line.used, line.limit, line.free) == (limit + 1, limit, 0)
    assert CHANNELS_DETAIL_RE.match(line.detail or ""), line.raw
    assert line.status == "FAIL"
    assert r.returncode == 1


@pytest.mark.verifies("AUDIT-2")
@pytest.mark.parametrize("side", ["warn", "ok"])
def test_audit2_warn_at_ninety_percent(run, doctor, side):
    """WARN at U >= 0.9 L: a config copy with L = U warns, one with 0.9 L > U is OK."""
    packets = doctor.copy("packets")
    _, used = packet_counts_of(packets)
    config = doctor.copy("packetizer-config")
    # floor(U/0.9) + 2 keeps 0.9 L at least 0.9 above U, clear of float rounding.
    limit = used if side == "warn" else math.floor(used / 0.9) + 2
    doctor.replace(config, CHANNELS_LIMIT_RE, f"MAX_PACKETIZER_CHANNELS = {limit}")
    r = run(packets=packets, packetizer_config=config)
    line = r.line("MAX_PACKETIZER_CHANNELS")
    assert (line.used, line.limit) == (used, limit)
    assert line.status == ("WARN" if side == "warn" else "OK")
    assert r.returncode == 0


@pytest.mark.verifies("AUDIT-2")
def test_audit2_current_tree_line(tree_run):
    """On the current tree the line is present, well-formed with its detail, not FAIL, exit 0."""
    line = tree_run.line("MAX_PACKETIZER_CHANNELS")
    assert CHANNELS_DETAIL_RE.match(line.detail or ""), line.raw
    assert line.status != "FAIL"
    assert tree_run.returncode == 0


@pytest.mark.verifies("AUDIT-2")
def test_audit2_check_packet_set_standalone_is_byte_identical():
    """scripts/check_packet_set.py run standalone still prints its six lines byte-for-byte, exit 0."""
    proc = subprocess.run(
        [sys.executable, str(PACKET_SCRIPT)],
        cwd=REPO,
        capture_output=True,
        timeout=60,
    )
    assert proc.returncode == 0
    assert proc.stdout == PINNED_PACKET_SET_OUTPUT.encode("utf-8")


# ---------------------------------------------------------------------------
# AUDIT-3  CMD_DISPATCHER_DISPATCH_TABLE_SIZE
# ---------------------------------------------------------------------------

DISPATCH_LIMIT = 6


@pytest.fixture
def cmd_config(doctor) -> Path:
    """A --cmd-config copy with CMD_DISPATCHER_DISPATCH_TABLE_SIZE = 6."""
    path = doctor.copy("cmd-config")
    doctor.replace(
        path,
        DISPATCH_LIMIT_RE,
        f"CMD_DISPATCHER_DISPATCH_TABLE_SIZE = {DISPATCH_LIMIT}",
    )
    return path


@pytest.mark.verifies("AUDIT-3")
def test_audit3_opcodes_over_limit_fails(run, doctor, cmd_config):
    """L+1 distinct opcodes (plus one duplicate, which must not count) give FAIL and exit 1."""
    opcodes = list(range(DISPATCH_LIMIT + 1)) + [0]
    dictionary = doctor.dictionary(opcodes, param_ids=[1])
    r = run(dictionary=dictionary, cmd_config=cmd_config)
    line = r.line("CMD_DISPATCHER_DISPATCH_TABLE_SIZE")
    assert (line.used, line.limit, line.free) == (DISPATCH_LIMIT + 1, DISPATCH_LIMIT, 0)
    assert line.status == "FAIL"
    assert r.returncode == 1


@pytest.mark.verifies("AUDIT-3")
def test_audit3_opcodes_at_limit_not_fail(run, doctor, cmd_config):
    """L opcodes give OK (read as: not FAIL and exit 0; section 1.4 makes U == L a WARN)."""
    dictionary = doctor.dictionary(list(range(DISPATCH_LIMIT)), param_ids=[1])
    r = run(dictionary=dictionary, cmd_config=cmd_config)
    line = r.line("CMD_DISPATCHER_DISPATCH_TABLE_SIZE")
    assert (line.used, line.limit, line.free) == (DISPATCH_LIMIT, DISPATCH_LIMIT, 0)
    assert line.status in ("OK", "WARN")
    assert r.returncode == 0


@pytest.mark.verifies("AUDIT-3")
def test_audit3_dictionary_none_skips(run, cmd_config):
    """--dictionary none gives ?/L used — SKIP, a dictionary: none line, and exit 0."""
    r = run(dictionary="none", cmd_config=cmd_config)
    line = r.line("CMD_DISPATCHER_DISPATCH_TABLE_SIZE")
    assert line.status == "SKIP"
    assert line.limit == DISPATCH_LIMIT
    assert line.raw.startswith(
        f"CMD_DISPATCHER_DISPATCH_TABLE_SIZE: ?/{DISPATCH_LIMIT} used"
    )
    assert r.dictionary_line().group("reason") is not None
    assert r.returncode == 0


def tree_has_no_dictionary() -> bool:
    """True when the repo tree itself holds no *TopologyDictionary.json (auto searches it first)."""
    patterns = (
        "build-artifacts/**/*TopologyDictionary.json",
        "build-fprime-automatic-zephyr*/**/*TopologyDictionary.json",
    )
    return not any(list(REPO.glob(p)) for p in patterns)


@pytest.mark.verifies("AUDIT-3")
def test_audit3_auto_uses_newest_dictionary_in_build_copy(run, doctor, cmd_config):
    """--dictionary auto --build-copy DIR reads the newest *TopologyDictionary.json under DIR/build-artifacts."""
    assert tree_has_no_dictionary(), (
        "the tree holds a dictionary; auto would read it first"
    )
    build_copy = doctor.tmp_path / "build-copy"
    old = doctor.dictionary(
        [1, 2, 3],
        param_ids=[1],
        version="v0.0.1-1-g0000000",
        name="OldTopologyDictionary.json",
        directory=build_copy / "build-artifacts" / "a",
    )
    new = doctor.dictionary(
        [1, 2, 3, 4],
        param_ids=[1],
        version="v0.0.2-2-g0000000",
        name="NewTopologyDictionary.json",
        directory=build_copy / "build-artifacts" / "b",
    )
    now = time.time()
    os.utime(old, (now - 600, now - 600))
    os.utime(new, (now, now))
    r = run(dictionary="auto", build_copy=build_copy, cmd_config=cmd_config)
    source = r.dictionary_line()
    assert source.group("path") and source.group("path").endswith(
        "NewTopologyDictionary.json"
    )
    assert source.group("version") == "v0.0.2-2-g0000000"
    line = r.line("CMD_DISPATCHER_DISPATCH_TABLE_SIZE")
    assert line.status != "SKIP"
    assert line.used == 4
    assert r.returncode == 0


@pytest.mark.verifies("AUDIT-3")
def test_audit3_auto_with_empty_build_copy_skips(run, doctor, cmd_config):
    """--dictionary auto with an empty --build-copy gives SKIP on both dictionary lines and exit 0."""
    assert tree_has_no_dictionary(), (
        "the tree holds a dictionary; auto would read it first"
    )
    build_copy = doctor.tmp_path / "empty-build-copy"
    build_copy.mkdir()
    r = run(dictionary="auto", build_copy=build_copy, cmd_config=cmd_config)
    assert r.dictionary_line().group("reason") is not None
    assert r.line("CMD_DISPATCHER_DISPATCH_TABLE_SIZE").status == "SKIP"
    assert r.line("PRMDB_NUM_DB_ENTRIES").status == "SKIP"
    assert r.returncode == 0


@pytest.mark.verifies("AUDIT-3")
@pytest.mark.parametrize("version", ["v1.2.0-65-g0000000", "v9.9.9"])
def test_audit3_head_mismatch_adds_warning_line(run, doctor, cmd_config, version):
    """A projectVersion whose -g<hash> is not HEAD (or has none) adds one WARN line; status unchanged."""
    dictionary = doctor.dictionary([1, 2], param_ids=[1], version=version)
    r = run(dictionary=dictionary, cmd_config=cmd_config)
    assert len(r.head_warning_lines()) == 1, r.proc.stdout
    line = r.line("CMD_DISPATCHER_DISPATCH_TABLE_SIZE")
    assert line.status == "OK"
    assert r.returncode == 0


def broken_dictionaries(doctor) -> dict[str, Path]:
    """Dictionaries that must make the script exit 2, keyed by what is wrong."""
    good = json.loads(doctor.dictionary([1], param_ids=[1]).read_text())
    cases = {"missing": doctor.tmp_path / "does-not-exist.json"}
    not_json = doctor.tmp_path / "not-json.json"
    not_json.write_text("{ this is not json", encoding="utf-8")
    cases["not-json"] = not_json
    for key in ("metadata", "commands", "parameters"):
        partial = {k: v for k, v in good.items() if k != key}
        path = doctor.tmp_path / f"lacks-{key}.json"
        path.write_text(json.dumps(partial), encoding="utf-8")
        cases[f"lacks-{key}"] = path
    return cases


@pytest.mark.verifies("AUDIT-3")
@pytest.mark.parametrize(
    "defect",
    ["missing", "not-json", "lacks-metadata", "lacks-commands", "lacks-parameters"],
)
def test_audit3_unusable_dictionary_exits_2(run, doctor, cmd_config, defect):
    """A --dictionary path that is missing, not JSON, or lacks a key exits 2 with one error: line."""
    r = run(dictionary=broken_dictionaries(doctor)[defect], cmd_config=cmd_config)
    assert r.returncode == 2
    assert len(r.error_lines()) == 1, r.proc.stderr


# ---------------------------------------------------------------------------
# AUDIT-4  PRMDB_NUM_DB_ENTRIES
# ---------------------------------------------------------------------------

PRMDB_LIMIT = 3


@pytest.fixture
def prmdb_config(doctor) -> Path:
    """A --prmdb-config copy with PRMDB_NUM_DB_ENTRIES = 3."""
    path = doctor.copy("prmdb-config")
    doctor.replace(path, PRMDB_LIMIT_RE, f"PRMDB_NUM_DB_ENTRIES = {PRMDB_LIMIT}")
    return path


@pytest.mark.verifies("AUDIT-4")
@pytest.mark.parametrize("entries", [PRMDB_LIMIT + 1, 4 * PRMDB_LIMIT])
def test_audit4_over_limit_is_info_never_fail(run, doctor, prmdb_config, entries):
    """U > L gives INFO and exit 0 (never FAIL)."""
    dictionary = doctor.dictionary([1], param_ids=list(range(entries)))
    r = run(dictionary=dictionary, prmdb_config=prmdb_config)
    line = r.line("PRMDB_NUM_DB_ENTRIES")
    assert (line.used, line.limit, line.free) == (entries, PRMDB_LIMIT, 0)
    assert PRMDB_DETAIL_RE.match(line.detail or ""), line.raw
    assert line.status == "INFO"
    assert r.returncode == 0
    assert r.result().group("status") == "OK"


@pytest.mark.verifies("AUDIT-4")
@pytest.mark.parametrize("entries", [PRMDB_LIMIT, PRMDB_LIMIT - 1])
def test_audit4_within_limit_is_ok(run, doctor, prmdb_config, entries):
    """U <= L gives OK (a bench constraint never warns, so U == L is OK too)."""
    dictionary = doctor.dictionary([1], param_ids=list(range(entries)))
    r = run(dictionary=dictionary, prmdb_config=prmdb_config)
    line = r.line("PRMDB_NUM_DB_ENTRIES")
    assert (line.used, line.limit, line.free) == (
        entries,
        PRMDB_LIMIT,
        PRMDB_LIMIT - entries,
    )
    assert line.status == "OK"
    assert r.returncode == 0


@pytest.mark.verifies("AUDIT-4")
def test_audit4_dictionary_none_skips(run, prmdb_config):
    """--dictionary none gives SKIP."""
    r = run(dictionary="none", prmdb_config=prmdb_config)
    line = r.line("PRMDB_NUM_DB_ENTRIES")
    assert line.status == "SKIP"
    assert line.raw.startswith(f"PRMDB_NUM_DB_ENTRIES: ?/{PRMDB_LIMIT} used")
    assert r.returncode == 0


@pytest.mark.verifies("AUDIT-4")
def test_audit4_default_config_is_project_override_else_lib_default(run, doctor):
    """--prmdb-config defaults to the project override when present, else the lib default."""
    override = (
        REPO
        / "PROVESFlightControllerReference"
        / "project"
        / "config"
        / "PrmDbImplCfg.hpp"
    )
    lib_default = REPO / "lib" / "fprime" / "default" / "config" / "PrmDbImplCfg.hpp"
    expected_file, expected_detail = (
        (override, "project override")
        if override.exists()
        else (lib_default, "lib default")
    )
    dictionary = doctor.dictionary([1], param_ids=[1])
    r = run(dictionary=dictionary)
    line = r.line("PRMDB_NUM_DB_ENTRIES")
    assert line.limit == doctor.read_int(expected_file, PRMDB_LIMIT_RE)
    assert line.detail == expected_detail


# ---------------------------------------------------------------------------
# AUDIT-5  ActiveRateGroupOutputPorts[<instance>]
# ---------------------------------------------------------------------------

RATE_GROUP_1HZ_LINE_RE = r"^\s*rateGroup1Hz\.RateGroupMemberOut\[(\d+)\]"


@pytest.mark.verifies("AUDIT-5")
def test_audit5_index_at_limit_fails(run, doctor):
    """A topology copy connecting rateGroup1Hz.RateGroupMemberOut[L] gives FAIL and exit 1."""
    limit = doctor.read_int(doctor.copy("ac-constants"), RATE_GROUP_LIMIT_RE)
    topology = doctor.copy("topology")
    doctor.insert_after_last(
        topology,
        RATE_GROUP_1HZ_LINE_RE,
        f"      rateGroup1Hz.RateGroupMemberOut[{limit}] -> synthMember.run",
    )
    r = run(topology=topology)
    line = r.line("ActiveRateGroupOutputPorts[rateGroup1Hz]")
    assert line.limit == limit
    assert line.status == "FAIL"
    assert r.returncode == 1


@pytest.mark.verifies("AUDIT-5")
def test_audit5_duplicate_output_index_fails(run, doctor):
    """A topology copy connecting the same output index twice gives FAIL."""
    topology = doctor.copy("topology")
    index = doctor.first_index(topology, RATE_GROUP_1HZ_LINE_RE)
    doctor.insert_after_last(
        topology,
        RATE_GROUP_1HZ_LINE_RE,
        f"      rateGroup1Hz.RateGroupMemberOut[{index}] -> synthMember.run",
    )
    r = run(topology=topology)
    line = r.line("ActiveRateGroupOutputPorts[rateGroup1Hz]")
    assert line.status == "FAIL"
    assert f"duplicate output index {index}" in (line.reason or ""), line.raw
    assert r.returncode == 1


@pytest.mark.verifies("AUDIT-5")
def test_audit5_current_tree_one_line_per_instance(tree_run, doctor):
    """On the current tree one well-formed, non-FAIL line per connected instance, in topology order; exit 0."""
    topology = doctor.copy("topology")
    instances: list[str] = []
    for text_line in topology.read_text(encoding="utf-8").splitlines():
        m = re.match(r"^\s*(\w+)\.RateGroupMemberOut\[", text_line)
        if m and m.group(1) not in instances:
            instances.append(m.group(1))
    assert instances, "the topology connects no rate group"
    printed = [
        c
        for c in tree_run.constant_lines
        if c.name.startswith("ActiveRateGroupOutputPorts[")
    ]
    assert [c.name for c in printed] == [
        f"ActiveRateGroupOutputPorts[{x}]" for x in instances
    ]
    for c in printed:
        assert MAX_INDEX_DETAIL_RE.match(c.detail or ""), c.raw
        assert c.status != "FAIL", c.raw
    assert tree_run.returncode == 0


# ---------------------------------------------------------------------------
# AUDIT-6  FaultInPorts
# ---------------------------------------------------------------------------

FAULT_IN_LINE_RE = r"^\s*\w+\.faultOut\s*->\s*faultManager\.faultIn\["


@pytest.mark.verifies("AUDIT-6")
def test_audit6_index_at_limit_fails(run, doctor):
    """A topology copy connecting faultIn[L] gives FAIL and exit 1."""
    limit = doctor.read_int(doctor.copy("fault-types"), FAULT_IN_LIMIT_RE)
    topology = doctor.copy("topology")
    doctor.insert_after_last(
        topology,
        FAULT_IN_LINE_RE,
        f"      synthProducer.faultOut -> faultManager.faultIn[{limit}]",
    )
    r = run(topology=topology)
    line = r.line("FaultInPorts")
    assert line.limit == limit
    assert line.status == "FAIL"
    assert r.returncode == 1


@pytest.mark.verifies("AUDIT-6")
def test_audit6_current_tree_line(tree_run):
    """On the current tree the line is present, well-formed with its detail, not FAIL, exit 0."""
    line = tree_run.line("FaultInPorts")
    assert MAX_INDEX_DETAIL_RE.match(line.detail or ""), line.raw
    assert line.status != "FAIL"
    assert tree_run.returncode == 0


# ---------------------------------------------------------------------------
# AUDIT-7  NUM_TASKS
# ---------------------------------------------------------------------------

SCHED_OUT_LINE_RE = r"^\s*taskGate\.schedOut\["
SCHED_TASK_ENUM_RE = r"^\s*enum SchedTask\s*\{"


@pytest.mark.verifies("AUDIT-7")
def test_audit7_numeric_index_at_limit_fails(run, doctor):
    """A topology copy connecting schedIn[L] gives FAIL and exit 1."""
    limit = doctor.read_int(doctor.copy("task-gate"), NUM_TASKS_RE)
    topology = doctor.copy("topology")
    doctor.insert_after_last(
        topology,
        SCHED_OUT_LINE_RE,
        f"      synthSource.run -> taskGate.schedIn[{limit}]",
    )
    r = run(topology=topology)
    line = r.line("NUM_TASKS")
    assert line.limit == limit
    assert line.status == "FAIL"
    assert r.returncode == 1


@pytest.mark.verifies("AUDIT-7")
def test_audit7_enumerator_at_limit_fails(run, doctor):
    """A --task-gate copy with one more enumerator = NUM_TASKS (a sixth = 5 today) gives FAIL."""
    task_gate = doctor.copy("task-gate")
    limit = doctor.read_int(task_gate, NUM_TASKS_RE)
    doctor.insert_in_block(
        task_gate, SCHED_TASK_ENUM_RE, f"        SYNTH_TASK = {limit}"
    )
    r = run(task_gate=task_gate)
    line = r.line("NUM_TASKS")
    assert line.limit == limit
    assert line.status == "FAIL"
    assert r.returncode == 1


@pytest.mark.verifies("AUDIT-7")
def test_audit7_unknown_enumerator_exits_2(run, doctor):
    """A topology copy using an unknown SchedTask enumerator exits 2 with one error: line."""
    topology = doctor.copy("topology")
    doctor.insert_after_last(
        topology,
        SCHED_OUT_LINE_RE,
        "      synthSource.run -> taskGate.schedIn[Components.SchedTask.NOPE]",
    )
    r = run(topology=topology)
    assert r.returncode == 2
    assert len(r.error_lines()) == 1, r.proc.stderr


@pytest.mark.verifies("AUDIT-7")
def test_audit7_symbolic_index_resolved_from_enum(run, doctor):
    """An index Components.SchedTask.NAME is resolved to its value in --task-gate."""
    task_gate = doctor.copy("task-gate")
    limit = doctor.read_int(task_gate, NUM_TASKS_RE)
    doctor.replace(
        task_gate,
        r"\bconstant NUM_TASKS\s*=\s*\d+",
        f"constant NUM_TASKS = {limit + 1}",
    )
    doctor.insert_in_block(
        task_gate, SCHED_TASK_ENUM_RE, f"        SYNTH_TASK = {limit}"
    )
    baseline = run(task_gate=task_gate).line("NUM_TASKS")
    topology = doctor.copy("topology")
    doctor.insert_after_last(
        topology,
        SCHED_OUT_LINE_RE,
        "      synthSource.run -> taskGate.schedIn[Components.SchedTask.SYNTH_TASK]",
    )
    r = run(task_gate=task_gate, topology=topology)
    line = r.line("NUM_TASKS")
    assert line.limit == limit + 1
    assert line.used == (baseline.used or 0) + 1
    detail = NUM_TASKS_DETAIL_RE.match(line.detail or "")
    assert detail, line.raw
    assert int(detail.group(2)) == limit
    assert line.status == "OK"
    assert r.returncode == 0


@pytest.mark.verifies("AUDIT-7")
def test_audit7_current_tree_line(tree_run):
    """On the current tree the line is present, well-formed with its detail, not FAIL, exit 0."""
    line = tree_run.line("NUM_TASKS")
    assert NUM_TASKS_DETAIL_RE.match(line.detail or ""), line.raw
    assert line.status != "FAIL"
    assert tree_run.returncode == 0


# ---------------------------------------------------------------------------
# AUDIT-8  MAX_FAULT_TYPE
# ---------------------------------------------------------------------------

FAULT_TYPE_ENUM_RE = r"^\s*enum FaultType\b.*\{"


@pytest.mark.verifies("AUDIT-8")
def test_audit8_enumerator_above_limit_fails(run, doctor):
    """A --fault-types copy adding an enumerator = L+1 gives FAIL and exit 1."""
    limit = doctor.read_int(doctor.copy("fault-table"), MAX_FAULT_TYPE_RE)
    fault_types = doctor.copy("fault-types")
    doctor.insert_in_block(
        fault_types,
        FAULT_TYPE_ENUM_RE,
        f"        SYNTH_FAULT = {limit + 1} @< synthetic",
    )
    r = run(fault_types=fault_types)
    line = r.line("MAX_FAULT_TYPE")
    assert (line.used, line.limit, line.free) == (limit + 1, limit, 0)
    assert line.status == "FAIL"
    assert r.returncode == 1


@pytest.mark.verifies("AUDIT-8")
def test_audit8_limit_wider_than_u8_mask_fails(run, doctor):
    """A --fault-table copy with MAX_FAULT_TYPE = 9 against a U8 mask gives FAIL."""
    fault_table = doctor.copy("fault-table")
    doctor.replace(fault_table, MAX_FAULT_TYPE_RE, "MAX_FAULT_TYPE = 9")
    fault_manager = doctor.copy("fault-manager")
    doctor.replace(fault_manager, AUTHORITY_MASK_RE, r"\1U8")
    r = run(fault_table=fault_table, fault_manager=fault_manager)
    line = r.line("MAX_FAULT_TYPE")
    assert line.limit == 9
    assert line.detail == "AUTHORITY_MASK U8 = 8 bits"
    assert line.status == "FAIL"
    assert r.returncode == 1


@pytest.mark.verifies("AUDIT-8")
def test_audit8_zero_free_warns(run, doctor):
    """F == 0 gives WARN (U == L with a mask wide enough, so nothing fails)."""
    fault_table = doctor.copy("fault-table")
    limit = doctor.read_int(fault_table, MAX_FAULT_TYPE_RE) + 1
    assert limit <= 16, "fixture widens the mask to U16"
    doctor.replace(fault_table, MAX_FAULT_TYPE_RE, f"MAX_FAULT_TYPE = {limit}")
    fault_types = doctor.copy("fault-types")
    doctor.insert_in_block(
        fault_types, FAULT_TYPE_ENUM_RE, f"        SYNTH_FAULT = {limit} @< synthetic"
    )
    fault_manager = doctor.copy("fault-manager")
    doctor.replace(fault_manager, AUTHORITY_MASK_RE, r"\1U16")
    r = run(
        fault_types=fault_types, fault_table=fault_table, fault_manager=fault_manager
    )
    line = r.line("MAX_FAULT_TYPE")
    assert (line.used, line.limit, line.free) == (limit, limit, 0)
    assert line.detail == "AUTHORITY_MASK U16 = 16 bits"
    assert line.status == "WARN"
    assert r.returncode == 0


@pytest.mark.verifies("AUDIT-8")
def test_audit8_current_tree_line(tree_run):
    """On the current tree the line is present, well-formed with its detail, not FAIL, exit 0."""
    line = tree_run.line("MAX_FAULT_TYPE")
    assert MASK_DETAIL_RE.match(line.detail or ""), line.raw
    assert line.status != "FAIL"
    assert tree_run.returncode == 0


# ---------------------------------------------------------------------------
# Output grammar as a whole (01-normative.md sections 1.2 and 1.3; no row claimed)
# ---------------------------------------------------------------------------


def test_line_order_result_line_and_exit_code_agree(tree_run):
    """Constant lines come in the fixed order, RESULT tallies them, exit is 1 iff a line FAILs."""
    names = [c.name for c in tree_run.constant_lines]
    rate_groups = [n for n in names if n.startswith("ActiveRateGroupOutputPorts[")]
    assert names == [
        "MAX_PACKETIZER_PACKETS",
        "MAX_PACKETIZER_CHANNELS",
        "CMD_DISPATCHER_DISPATCH_TABLE_SIZE",
        "PRMDB_NUM_DB_ENTRIES",
        *rate_groups,
        "FaultInPorts",
        "NUM_TASKS",
        "MAX_FAULT_TYPE",
    ]
    result = tree_run.result()
    tally = {
        s: sum(1 for c in tree_run.constant_lines if c.status == s)
        for s in ("OK", "WARN", "INFO", "SKIP", "FAIL")
    }
    assert (
        int(result.group("ok")),
        int(result.group("warn")),
        int(result.group("info")),
        int(result.group("skip")),
        int(result.group("fail")),
    ) == (tally["OK"], tally["WARN"], tally["INFO"], tally["SKIP"], tally["FAIL"])
    assert result.group("status") == ("FAIL" if tally["FAIL"] else "OK")
    assert tree_run.returncode == (1 if tally["FAIL"] else 0)
