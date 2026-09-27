"""Tests for ``scripts/check_hardware_consistency.py``, claiming HWC-1..2.

Each test asserts one clause of its row's pass criteria
(``docs-site/requirements/hardware-consistency.md``) from the command line,
parsed with the output grammar of
``docs-site/dev-loop/cycles/cycle-j-plan/01-normative.md`` section 1.3 and the
exit codes of section 1.4. Every number is proven on a doctored copy of a
default input in ``tmp_path`` (section 1.5); the tree is never edited. On the
untouched tree a test asserts only that the rule line is well-formed, its status
is OK and the exit code is 0 (cycle-j-review amendment 1).

Doctoring sets a known state rather than assuming the tree's current one, so the
same tests hold before and after Stage 4 changes the drv2605 configure lines and
the ``&flash0`` size: ``set_channel`` rewrites one manager's
``state.muxChannel<a>Device`` and ``set_flash_size`` rewrites the ``&flash0``
size cell.
"""

import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
SCRIPT = Path("scripts") / "check_hardware_consistency.py"
P = REPO / "PROVESFlightControllerReference"

#: Default input per ``--<flag>`` (01-normative.md section 1.1).
DEFAULT_INPUTS = {
    "topology-cpp": P
    / "ReferenceDeployment"
    / "Top"
    / "ReferenceDeploymentTopology.cpp",
    "main-cpp": P / "ReferenceDeployment" / "Main.cpp",
    "dts": REPO
    / "boards"
    / "bronco_space"
    / "proves_flight_control_board_v5"
    / "proves_flight_control_board_v5.dtsi",
}

# The em dash of the output grammar: U+2014 with one space on each side.
DASH = " — "

# ---------------------------------------------------------------------------
# Output grammar (01-normative.md section 1.3)
# ---------------------------------------------------------------------------

HWC1_RE = re.compile(
    rf"^HWC-1 mux-channels: (?P<n>\d+) managers checked, (?P<m>\d+) mismatched"
    rf"{DASH}(?P<status>OK|FAIL)$"
)
OFFENDER_RE = re.compile(
    r"^    (?P<inst>\w+): configure passes mux_channel_(?P<a>\d+), "
    r"devicetree places (?P<label>\w+) on mux_channel_(?P<b>\d+)$"
)
HWC2_RE = re.compile(
    rf"^HWC-2 flash-size: reg (?P<r>\d+) bytes, partitions end (?P<e>\d+) bytes"
    rf"{DASH}(?P<status>OK|FAIL)$"
)
RESULT_RE = re.compile(
    rf"^RESULT: (?P<status>OK|FAIL){DASH}(?P<ok>\d+) ok, (?P<warn>\d+) warn, "
    r"(?P<skip>\d+) skip, (?P<fail>\d+) fail$"
)

#: The five drv2605 face managers and the channel section 1.5 ("HWC-1 OK")
#: gives each: face N sits on mux_channel_N.
DRV2605_FACES = (0, 1, 2, 3, 5)


@dataclass(frozen=True)
class Offender:
    """One four-space-indented offender line under the HWC-1 line."""

    inst: str
    a: int
    label: str
    b: int


@dataclass(frozen=True)
class Report:
    """A complete, well-formed stdout of the script (section 1.3)."""

    n: int
    m: int
    hwc1_status: str
    offenders: list[Offender]
    r: int
    e: int
    hwc2_status: str
    result_status: str
    tally: tuple[int, int, int, int]
    lines: list[str]


@dataclass(frozen=True)
class Run:
    """One invocation of the script."""

    returncode: int
    stdout: str
    stderr: str

    def report(self) -> Report:
        """Parse stdout strictly against section 1.3; fail the test if it deviates."""
        return parse_report(self.stdout)


def parse_report(stdout: str) -> Report:
    """Return the parsed stdout; assert every line is in the order section 1.3 fixes."""
    lines = stdout.splitlines()
    assert len(lines) >= 3, f"too few lines:\n{stdout}"
    m1 = HWC1_RE.match(lines[0])
    assert m1, f"first line is not a well-formed HWC-1 line: {lines[0]!r}"
    offenders = []
    i = 1
    while i < len(lines) and lines[i].startswith("    "):
        mo = OFFENDER_RE.match(lines[i])
        assert mo, f"malformed offender line: {lines[i]!r}"
        offenders.append(
            Offender(
                mo.group("inst"),
                int(mo.group("a")),
                mo.group("label"),
                int(mo.group("b")),
            )
        )
        i += 1
    assert i == len(lines) - 2, (
        f"expected exactly HWC-2 and RESULT after offenders:\n{stdout}"
    )
    m2 = HWC2_RE.match(lines[i])
    assert m2, f"not a well-formed HWC-2 line: {lines[i]!r}"
    mr = RESULT_RE.match(lines[-1])
    assert mr, f"last line is not a well-formed RESULT line: {lines[-1]!r}"
    return Report(
        n=int(m1.group("n")),
        m=int(m1.group("m")),
        hwc1_status=m1.group("status"),
        offenders=offenders,
        r=int(m2.group("r")),
        e=int(m2.group("e")),
        hwc2_status=m2.group("status"),
        result_status=mr.group("status"),
        tally=(
            int(mr.group("ok")),
            int(mr.group("warn")),
            int(mr.group("skip")),
            int(mr.group("fail")),
        ),
        lines=lines,
    )


# ---------------------------------------------------------------------------
# Running and doctoring (01-normative.md section 1.5)
# ---------------------------------------------------------------------------


def run(
    topology_cpp: Path | None = None,
    main_cpp: Path | None = None,
    dts: Path | None = None,
) -> Run:
    """Run the script from the repo root; a flag is passed only when its path is given."""
    cmd = [sys.executable, str(SCRIPT)]
    for flag, path in (
        ("--topology-cpp", topology_cpp),
        ("--main-cpp", main_cpp),
        ("--dts", dts),
    ):
        if path is not None:
            cmd += [flag, str(path)]
    proc = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True, timeout=60)
    return Run(proc.returncode, proc.stdout, proc.stderr)


def copy(tmp_path: Path, flag: str, name: str | None = None) -> Path:
    """Copy the default input of ``--<flag>`` into ``tmp_path`` and return the copy."""
    src = DEFAULT_INPUTS[flag]
    dst = tmp_path / (name or src.name)
    shutil.copyfile(src, dst)
    return dst


def replace_once(path: Path, old: str, new: str) -> None:
    """Replace the single occurrence of ``old`` in ``path`` with ``new``."""
    text = path.read_text()
    assert text.count(old) == 1, f"fixture expects exactly one {old!r} in {path.name}"
    path.write_text(text.replace(old, new))


def set_channel(path: Path, inst: str, channel: int) -> None:
    """Make ``inst``'s configure call pass ``state.muxChannel<channel>Device``."""
    text = path.read_text()
    prefix = f"{inst}.configure(state.tca9548aDevice, state.muxChannel"
    assert text.count(prefix) == 1, f"fixture expects one configure call for {inst}"
    start = text.index(prefix) + len(prefix)
    end = text.index("Device", start)
    path.write_text(text[:start] + str(channel) + text[end:])


def set_flash_size(path: Path, size_cell: str) -> None:
    """Make the ``&flash0`` node read ``reg = <0x10000000 <size_cell>>;``."""
    text = path.read_text()
    node = text.index("&flash0 {")
    prefix = "reg = <0x10000000 "
    start = text.index(prefix, node) + len(prefix)
    end = text.index(">;", start)
    path.write_text(text[:start] + size_cell + text[end:])


def remove_flash_partitions(path: Path) -> None:
    """Delete the whole brace-balanced ``partitions { ... };`` block of ``&flash0``."""
    text = path.read_text()
    node = text.index("&flash0 {")
    key = text.index("partitions {", node)
    line_start = text.rindex("\n", 0, key) + 1
    depth = 0
    i = text.index("{", key)
    while True:
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                break
        i += 1
    end = text.index(";", i) + 1
    if text[end : end + 1] == "\n":
        end += 1
    doctored = text[:line_start] + text[end:]
    assert "partitions" not in doctored[node:], (
        "fixture failed to remove the partitions block"
    )
    assert "reg = <0x10000000 " in doctored[node:], "fixture must keep the &flash0 reg"
    path.write_text(doctored)


def label_of(main_cpp: Path, field: str) -> str:
    """The label ``--main-cpp`` assigns to ``inputs.<field>`` (section 1.2)."""
    m = re.search(rf"^\s*inputs\.{field} = (\w+);", main_cpp.read_text(), re.M)
    assert m, f"fixture expects inputs.{field} in {main_cpp.name}"
    return m.group(1)


def drv2605_topology(tmp_path: Path, channels: dict[int, int], name: str) -> Path:
    """A ``--topology-cpp`` copy whose drv2605 face N manager passes ``channels[N]``."""
    topo = copy(tmp_path, "topology-cpp", name)
    for face, channel in channels.items():
        set_channel(topo, f"drv2605Face{face}Manager", channel)
    return topo


def good_dts(tmp_path: Path) -> Path:
    """A ``--dts`` copy with ``DT_SIZE_M(16)``, which section 1.5 fixes as HWC-2 OK."""
    dts = copy(tmp_path, "dts")
    set_flash_size(dts, "DT_SIZE_M(16)")
    return dts


#: The e1eced14 drv2605 state (section 1.2): all five pass muxChannel0Device.
DEFECT_CHANNELS = dict.fromkeys(DRV2605_FACES, 0)
#: The corrected state (section 1.5 "HWC-1 OK"): face N passes muxChannelN.
FIXED_CHANNELS = {face: face for face in DRV2605_FACES}


# ---------------------------------------------------------------------------
# HWC-1  mux-channels
# ---------------------------------------------------------------------------


@pytest.mark.verifies("HWC-1")
def test_hwc1_drv2605_fix_lowers_mismatch_count(tmp_path):
    """A copy that fixes one drv2605 manager lowers M by one; FAIL while M > 0."""
    main = copy(tmp_path, "main-cpp")
    dts = good_dts(tmp_path)
    before = drv2605_topology(tmp_path, DEFECT_CHANNELS, "before.cpp")
    after = drv2605_topology(tmp_path, DEFECT_CHANNELS, "after.cpp")
    set_channel(after, "drv2605Face1Manager", 1)

    base = run(before, main, dts)
    base_report = base.report()
    assert (base_report.n, base_report.m) == (18, 4)
    assert [o.inst for o in base_report.offenders] == [
        "drv2605Face1Manager",
        "drv2605Face2Manager",
        "drv2605Face3Manager",
        "drv2605Face5Manager",
    ]

    fixed = run(after, main, dts)
    rep = fixed.report()
    assert rep.n == base_report.n
    assert rep.m == base_report.m - 1
    assert rep.hwc1_status == "FAIL"
    assert len(rep.offenders) == rep.m
    assert rep.lines[1 : 1 + rep.m] == [
        f"    drv2605Face{face}Manager: configure passes mux_channel_0, devicetree places "
        f"{label_of(main, f'face{face}drv2605Device')} on mux_channel_{face}"
        for face in (2, 3, 5)
    ]
    assert fixed.returncode == 1
    assert rep.result_status == "FAIL"
    assert rep.tally == (1, 0, 0, 1)


@pytest.mark.parametrize(
    ("inst", "tree_channel", "new_channel", "field"),
    [
        ("tmp112Face2Manager", 2, 0, "face2TempDevice"),
        ("veml6031Face7Manager", 7, 4, "face7LightDevice"),
    ],
)
@pytest.mark.verifies("HWC-1")
def test_hwc1_moving_tmp112_or_veml6031_raises_mismatch_count(
    tmp_path, inst, tree_channel, new_channel, field
):
    """Moving one tmp112 or veml6031 manager to another channel raises M by one, FAIL, exit 1.

    The offender line names the instance, the channel passed and the channel in the devicetree.
    """
    main = copy(tmp_path, "main-cpp")
    dts = copy(tmp_path, "dts")
    before = copy(tmp_path, "topology-cpp", "before.cpp")
    after = copy(tmp_path, "topology-cpp", "after.cpp")
    replace_once(
        after,
        f"{inst}.configure(state.tca9548aDevice, state.muxChannel{tree_channel}Device",
        f"{inst}.configure(state.tca9548aDevice, state.muxChannel{new_channel}Device",
    )

    base_report = run(before, main, dts).report()
    moved = run(after, main, dts)
    rep = moved.report()
    assert rep.n == base_report.n
    assert rep.m == base_report.m + 1
    assert rep.hwc1_status == "FAIL"
    assert len(rep.offenders) == rep.m
    # The tmp112 and veml6031 configure calls precede the drv2605 ones in
    # --topology-cpp, so in file order the new offender comes first.
    assert rep.offenders[1:] == base_report.offenders
    assert rep.lines[1] == (
        f"    {inst}: configure passes mux_channel_{new_channel}, devicetree places "
        f"{label_of(main, field)} on mux_channel_{tree_channel}"
    )
    assert moved.returncode == 1
    assert rep.result_status == "FAIL"


@pytest.mark.verifies("HWC-1")
def test_hwc1_all_channels_matching_is_ok(tmp_path):
    """All four drv2605 lines set to channels 1, 2, 3, 5: M = 0, OK, exit 0 (exit 1 only from HWC-2)."""
    main = copy(tmp_path, "main-cpp")
    topo = drv2605_topology(tmp_path, FIXED_CHANNELS, "fixed.cpp")

    ok = run(topo, main, good_dts(tmp_path))
    rep = ok.report()
    assert (
        rep.lines[0] == f"HWC-1 mux-channels: 18 managers checked, 0 mismatched{DASH}OK"
    )
    assert rep.offenders == []
    assert ok.returncode == 0
    assert rep.lines[-1] == f"RESULT: OK{DASH}2 ok, 0 warn, 0 skip, 0 fail"

    small = copy(tmp_path, "dts", "small.dtsi")
    set_flash_size(small, "DT_SIZE_M(4)")
    only_hwc2 = run(topo, main, small)
    rep2 = only_hwc2.report()
    assert rep2.hwc1_status == "OK"
    assert rep2.m == 0
    assert rep2.hwc2_status == "FAIL"
    assert only_hwc2.returncode == 1
    assert rep2.lines[-1] == f"RESULT: FAIL{DASH}1 ok, 0 warn, 0 skip, 1 fail"


@pytest.mark.verifies("HWC-1")
def test_hwc1_missing_inputs_assignment_exits_2(tmp_path):
    """A --main-cpp copy missing the inputs.<field> assignment for a checked manager exits 2."""
    topo = copy(tmp_path, "topology-cpp")
    dts = good_dts(tmp_path)
    intact = copy(tmp_path, "main-cpp", "intact.cpp")
    main = copy(tmp_path, "main-cpp")
    replace_once(main, "    inputs.face3drv2605Device = face3_drv2605;\n", "")

    # Control: the same inputs with the assignment present give a report, so
    # the exit 2 below comes from the deleted line and not from the run itself.
    control = run(topo, intact, dts)
    assert control.returncode in (0, 1)
    control.report()

    r = run(topo, main, dts)
    assert r.returncode == 2
    assert r.stdout == ""
    assert len(r.stderr.strip().splitlines()) == 1
    assert "face3drv2605Device" in r.stderr


@pytest.mark.verifies("HWC-1")
def test_hwc1_current_tree_ok():
    """On the current tree the HWC-1 line is well-formed with status OK and exit 0."""
    r = run()
    rep = r.report()
    assert rep.hwc1_status == "OK"
    assert r.returncode == 0


# ---------------------------------------------------------------------------
# HWC-2  flash-size
# ---------------------------------------------------------------------------


@pytest.mark.verifies("HWC-2")
def test_hwc2_size_16m_is_ok(tmp_path):
    """A --dts copy with DT_SIZE_M(16) gives OK; the RESULT line is last."""
    main = copy(tmp_path, "main-cpp")
    topo = drv2605_topology(tmp_path, FIXED_CHANNELS, "fixed.cpp")

    r = run(topo, main, good_dts(tmp_path))
    rep = r.report()
    assert (
        rep.lines[-2]
        == f"HWC-2 flash-size: reg 16777216 bytes, partitions end 16777216 bytes{DASH}OK"
    )
    assert rep.lines[-1] == f"RESULT: OK{DASH}2 ok, 0 warn, 0 skip, 0 fail"
    assert r.returncode == 0


@pytest.mark.verifies("HWC-2")
def test_hwc2_partition_one_byte_past_reg_fails(tmp_path):
    """A copy where a partition ends one byte past R gives FAIL and exit 1."""
    dts = good_dts(tmp_path)
    replace_once(dts, "reg = <0x400000 0xC00000>;", "reg = <0x400000 0xC00001>;")

    r = run(copy(tmp_path, "topology-cpp"), copy(tmp_path, "main-cpp"), dts)
    rep = r.report()
    assert (rep.r, rep.e) == (16777216, 16777217)
    assert (
        rep.lines[-2]
        == f"HWC-2 flash-size: reg 16777216 bytes, partitions end 16777217 bytes{DASH}FAIL"
    )
    assert r.returncode == 1
    assert rep.result_status == "FAIL"


@pytest.mark.parametrize("size_cell", ["0x1000000", "16777216", "DT_SIZE_K(16384)"])
@pytest.mark.verifies("HWC-2")
def test_hwc2_reg_literal_forms(tmp_path, size_cell):
    """R is read from the reg size cell written as DT_SIZE_K, hex or decimal: each gives R = 16777216."""
    dts = copy(tmp_path, "dts")
    set_flash_size(dts, size_cell)

    r = run(copy(tmp_path, "topology-cpp"), copy(tmp_path, "main-cpp"), dts)
    rep = r.report()
    assert rep.r == 16777216
    assert rep.e == 16777216
    assert rep.hwc2_status == "OK"


@pytest.mark.verifies("HWC-2")
def test_hwc2_no_partitions_block_exits_2(tmp_path):
    """A copy with no partitions block exits 2."""
    topo = copy(tmp_path, "topology-cpp")
    main = copy(tmp_path, "main-cpp")
    intact = copy(tmp_path, "dts", "intact.dtsi")
    dts = copy(tmp_path, "dts")
    remove_flash_partitions(dts)

    # Control: the same inputs with the partitions block present give a report,
    # so the exit 2 below comes from the removal and not from the run itself.
    control = run(topo, main, intact)
    assert control.returncode in (0, 1)
    control.report()

    r = run(topo, main, dts)
    assert r.returncode == 2
    assert r.stdout == ""
    assert len(r.stderr.strip().splitlines()) == 1


@pytest.mark.verifies("HWC-2")
def test_hwc2_current_tree_ok():
    """On the current tree the HWC-2 line is well-formed with status OK and exit 0; RESULT is last."""
    r = run()
    rep = r.report()
    assert rep.hwc2_status == "OK"
    assert RESULT_RE.match(rep.lines[-1])
    assert r.returncode == 0
