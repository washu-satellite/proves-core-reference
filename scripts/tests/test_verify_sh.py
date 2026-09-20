"""Tests for ``scripts/verify.sh``, claiming AUDIT-9.

Each test runs the gate itself with the ``VERIFY_STAGES`` / ``VERIFY_BUILD_DIR``
knobs and a temporary build directory, so nothing under the tree is written.
``VERIFY_NESTED=1`` is set explicitly on every spawned run so the behaviour is
the same whether pytest was started by hand or by the gate's script-tests stage.

Re-entry guard: the gate exports ``VERIFY_NESTED=1`` when it runs pytest, and
the runs spawned here restrict the nested gate to one stage, so pytest is never
re-entered. The module skips itself only when ``VERIFY_NESTED`` is set *and*
``VERIFY_STAGES`` is exactly ``host-tests``; the gate only runs pytest when the
script-tests stage is selected, so no pytest process it starts ever carries
``VERIFY_STAGES=host-tests``. The skip is therefore unreachable from the gate and
no claiming test can show as skipped in the junit the matrix reads (amendment 4).
"""

import os
import re
import subprocess
import sys
from pathlib import Path

import pytest

if os.environ.get("VERIFY_NESTED") and os.environ.get("VERIFY_STAGES") == "host-tests":
    pytest.skip(
        "nested verify.sh restricted to host-tests must not re-enter the gate",
        allow_module_level=True,
    )

REPO = Path(__file__).resolve().parents[2]
VERIFY_SH = Path("scripts") / "verify.sh"
CAPACITY_SCRIPT = Path("scripts") / "check_capacity.py"

HOST_TEST_BINARIES = 30
PARTIAL_LABEL = "stages: host-tests (partial run — not a gate result)"
AUDIT_HEADER = "== capacity audit"
OLD_PACKET_HEADER = "== packet set vs TlmPacketizer config"
NESTED_REFUSAL = "nested verify.sh without VERIFY_STAGES"
PASSED_LINE_RE = re.compile(r"^\s+(test_\w+): .*\bPASSED\b")


def gate_env(build_dir: Path, stages: str | None) -> dict[str, str]:
    """Environment for one spawned gate: host env, explicit stages, temp build dir, nested."""
    env = dict(os.environ)
    env["VERIFY_ENV"] = "host"
    env["VERIFY_BUILD_DIR"] = str(build_dir)
    env["VERIFY_NESTED"] = "1"
    env.pop("VERIFY_STAGES", None)
    if stages is not None:
        env["VERIFY_STAGES"] = stages
    return env


def run_gate(env: dict[str, str], timeout: float) -> subprocess.CompletedProcess:
    """Run ``scripts/verify.sh`` from the repo root with ``env`` and capture its output."""
    return subprocess.run(
        [str(VERIFY_SH)],
        cwd=REPO,
        env=env,
        capture_output=True,
        text=True,
        timeout=timeout,
    )


@pytest.mark.verifies("AUDIT-9")
def test_audit9_host_tests_stage_starts_from_empty_build_dir(tmp_path):
    """A canary in VERIFY_BUILD_DIR is gone after the run, 30 test_* PASSED, exit 0, partial label."""
    build_dir = tmp_path / "bt"
    build_dir.mkdir()
    canary = build_dir / "canary"
    canary.write_text("planted before the run\n", encoding="utf-8")
    assert canary.exists()
    proc = run_gate(gate_env(build_dir, "host-tests"), timeout=900)
    out = proc.stdout
    assert not canary.exists(), "the build directory was not emptied before configure"
    assert build_dir.is_dir(), "the build directory was not recreated"
    assert (build_dir / "junit.xml").exists(), (
        "ctest junit was not written to VERIFY_BUILD_DIR"
    )
    passed = [m.group(1) for m in map(PASSED_LINE_RE.match, out.splitlines()) if m]
    assert len(passed) == HOST_TEST_BINARIES, f"{len(passed)} PASSED lines:\n{out}"
    assert len(set(passed)) == HOST_TEST_BINARIES, "a binary was listed twice"
    assert proc.returncode == 0, out + proc.stderr
    assert PARTIAL_LABEL in out, out


@pytest.mark.verifies("AUDIT-9")
def test_audit9_audit_stage_prints_every_capacity_line(tmp_path):
    """With VERIFY_STAGES=audit the output has the capacity-audit header and every script line."""
    build_dir = tmp_path / "bt"
    env = gate_env(build_dir, "audit")
    direct = subprocess.run(
        [sys.executable, str(CAPACITY_SCRIPT)],
        cwd=REPO,
        env=env,
        capture_output=True,
        text=True,
        timeout=60,
    )
    proc = run_gate(env, timeout=120)
    out_lines = proc.stdout.splitlines()
    assert any(line.startswith(AUDIT_HEADER) for line in out_lines), proc.stdout
    assert not any(line.startswith(OLD_PACKET_HEADER) for line in out_lines), (
        proc.stdout
    )
    missing = [
        line
        for line in direct.stdout.splitlines()
        if line and f"  {line}" not in out_lines
    ]
    assert not missing, (
        f"script lines absent from the gate output: {missing!r}\n{proc.stdout}"
    )
    assert direct.returncode == 0, direct.stdout + direct.stderr
    assert proc.returncode == 0, proc.stdout + proc.stderr


@pytest.mark.verifies("AUDIT-9")
def test_audit9_nested_run_without_stages_exits_2(tmp_path):
    """A nested run with VERIFY_NESTED=1 and no VERIFY_STAGES exits 2 and says why."""
    proc = run_gate(gate_env(tmp_path / "bt", None), timeout=60)
    assert proc.returncode == 2, proc.stdout + proc.stderr
    assert NESTED_REFUSAL in proc.stdout + proc.stderr
