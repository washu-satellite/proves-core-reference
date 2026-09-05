#!/usr/bin/env python3
"""Generate the requirements traceability matrix (RTM).

Joins these sources into docs-site/requirements-matrix.md:

1. Component requirements: the "## Requirements" table in every
   PROVESFlightControllerReference/Components/**/docs/sdd.md.
2. System requirements: every "## <group>" table in docs-site/requirements/*.md
   (e.g. the CDH L1/L2 requirements transcribed from the CDR).
3. Test links: requirement IDs declared in tests —
   - gtest:  RecordProperty("verifies", "Comp-1,Comp-2") inside TEST/TEST_F
   - pytest: @pytest.mark.verifies("Comp-1", "Comp-2") on integration tests
4. Results: an optional ctest JUnit XML (ctest --output-junit). gtest results
   are reported per test binary (one ctest case per binary), so a unit test's
   status is the status of the binary that contains it. Integration tests
   require flight hardware and are never run in CI; they are listed as
   hardware-verified evidence without a CI status.

Requirement tables support two formats:

  Legacy:   | Name | Description | Validation |
  Extended: | Name | Description | Method | Level | Pass Criteria | Status | Reason |

Method is the verification method (Unit Test, Integration Test, Analysis,
Inspection, Demonstration). Level is where the requirement is proven (Unit,
Board, Subsystem, Flatsat, Environmental). Pass Criteria is the measurable
pass/fail condition and MUST be defined before a linked test result is
accepted — a requirement with test links but no criteria is flagged in the
matrix and generates a warning. Status/Reason hold a manual assessment (e.g.
"CDR: Not met" and why) used only until automated evidence exists.

Edit requirements with scripts/req.py rather than hand-editing the tables.

Usage:
    python3 scripts/generate_rtm.py [--junit build-gtest/junit.xml] [--sha <sha>]
"""

import argparse
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
COMPONENTS_DIR = REPO_ROOT / "PROVESFlightControllerReference" / "Components"
SYSTEM_REQ_DIR = REPO_ROOT / "docs-site" / "requirements"
UNIT_TEST_DIR = REPO_ROOT / "PROVESFlightControllerReference" / "test" / "unit-tests"
INT_TEST_DIR = REPO_ROOT / "PROVESFlightControllerReference" / "test" / "int"
OUTPUT = REPO_ROOT / "docs-site" / "requirements-matrix.md"

TEST_MACRO_RE = re.compile(
    r"^TEST(?:_F)?\(\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*\)", re.M
)
RECORD_PROP_RE = re.compile(r'RecordProperty\(\s*"verifies"\s*,\s*"([^"]+)"')
PYTEST_MARK_RE = re.compile(r"@pytest\.mark\.verifies\(([^)]*)\)")
PY_DEF_RE = re.compile(r"^def\s+(test_[A-Za-z0-9_]+)", re.M)
TABLE_SEP_RE = re.compile(r"^:?-+:?$")
SECTION_RE = re.compile(r"^##\s+(.+?)\s*$(.*?)(?=^##\s|\Z)", re.M | re.S)

# Header-cell aliases -> canonical field name. Legacy "Validation" maps to
# the verification method so old three-column tables keep working.
HEADER_ALIASES = {
    "name": "id",
    "requirement": "id",
    "id": "id",
    "description": "description",
    "validation": "method",
    "method": "method",
    "verification method": "method",
    "level": "level",
    "proof level": "level",
    "criteria": "criteria",
    "pass criteria": "criteria",
    "pass/fail criteria": "criteria",
    "status": "status",
    "cdr status": "status",
    "reason": "reason",
    "failure reason": "reason",
    "notes": "reason",
}

CRITERIA_PLACEHOLDERS = {"", "tbd", "todo", "-", "n/a"}


def split_cells(line):
    """Split a markdown table line into stripped cell strings."""
    return [c.strip() for c in line.strip().strip("|").split("|")]


def parse_table(block):
    """Parse one markdown table into a list of requirement dicts."""
    rows = []
    columns = None
    for line in block.splitlines():
        line = line.strip()
        if not line.startswith("|"):
            continue
        cells = split_cells(line)
        if len(cells) < 2:
            continue
        if all(TABLE_SEP_RE.match(c) or not c for c in cells):
            continue
        lowered = [c.lower() for c in cells]
        if columns is None and lowered[0] in ("name", "requirement", "id"):
            columns = [HEADER_ALIASES.get(c) for c in lowered]
            continue
        if columns is None:
            # Table without a recognized header: assume legacy positional.
            columns = ["id", "description", "method"]
        req = {
            "id": "",
            "description": "",
            "method": "",
            "level": "",
            "criteria": "",
            "status": "",
            "reason": "",
        }
        for field, value in zip(columns, cells):
            if field:
                req[field] = value
        if req["id"]:
            rows.append(req)
    return rows


def has_criteria(req):
    """Return True if the requirement has real pass criteria (not TBD/blank)."""
    return req["criteria"].lower() not in CRITERIA_PLACEHOLDERS


def parse_requirements():
    """Return {group: [req dict]}, system-level groups first."""
    requirements = {}
    if SYSTEM_REQ_DIR.is_dir():
        for doc in sorted(SYSTEM_REQ_DIR.glob("*.md")):
            text = doc.read_text(encoding="utf-8")
            for match in SECTION_RE.finditer(text):
                rows = parse_table(match.group(2))
                if rows:
                    requirements[match.group(1)] = rows
    for sdd in sorted(COMPONENTS_DIR.rglob("docs/sdd.md")):
        component = sdd.parent.parent.name
        text = sdd.read_text(encoding="utf-8")
        match = re.search(r"^##\s+Requirements\s*$(.*?)(?=^##\s|\Z)", text, re.M | re.S)
        if not match:
            continue
        rows = parse_table(match.group(1))
        if rows:
            requirements[component] = rows
    return requirements


def split_ids(raw):
    """Split a comma/space separated requirement-ID string into a list."""
    return [r for r in re.split(r"[,\s]+", raw.strip().strip('"').strip("'")) if r]


def parse_unit_test_links():
    """Return {req_id: [(binary, case_ref)]} from RecordProperty tags."""
    links = {}
    for src in sorted(UNIT_TEST_DIR.glob("test_*.cpp")):
        binary = src.stem
        text = src.read_text(encoding="utf-8")
        matches = list(TEST_MACRO_RE.finditer(text))
        for i, m in enumerate(matches):
            body_end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
            body = text[m.start() : body_end]
            for prop in RECORD_PROP_RE.finditer(body):
                for req_id in split_ids(prop.group(1)):
                    links.setdefault(req_id, []).append(
                        (binary, f"{m.group(1)}.{m.group(2)}")
                    )
    return links


def parse_int_test_links():
    """Return {req_id: [(file, test_name)]} from pytest verifies markers."""
    links = {}
    for src in sorted(INT_TEST_DIR.glob("*_test.py")):
        text = src.read_text(encoding="utf-8")
        pending = []
        for line in text.splitlines():
            mark = PYTEST_MARK_RE.search(line)
            if mark:
                for arg in re.findall(r"""["']([^"']+)["']""", mark.group(1)):
                    pending.extend(split_ids(arg))
                continue
            definition = PY_DEF_RE.match(line)
            if definition:
                for req_id in pending:
                    links.setdefault(req_id, []).append((src.name, definition.group(1)))
                pending = []
            elif line.strip() and not line.strip().startswith("@"):
                pending = []
    return links


def parse_junit(junit_path):
    """Return {ctest_case_name: bool_passed} from a ctest JUnit XML."""
    statuses = {}
    root = ET.parse(junit_path).getroot()
    for case in root.iter("testcase"):
        name = case.get("name", "")
        failed = case.find("failure") is not None or case.find("error") is not None
        status = case.get("status", "")
        statuses[name] = (not failed) and status != "fail"
    return statuses


ENVIRONMENTS = {
    "host": "no board in this environment",
    "desk": "board on USB, no radio rig",
    "rig": "CI runner with probe and radio",
}


def int_status(env):
    """Status text for integration-test links under the declared environment.

    Board-level tests cannot execute without hardware. In a `host` environment
    they are DEFERRED — expected, not a defect, and not counted as unverified —
    so a laptop build does not read as a regression of the hardware evidence.
    """
    if env == "host":
        return "⏸ Integration (deferred: no board in host env)"
    return "🛰️ Integration (hardware)"


def status_cell(req, unit_refs, int_refs, junit, env="host"):
    """Build the Status cell: test results, criteria flag, or manual assessment."""
    parts = []
    if (unit_refs or int_refs) and not has_criteria(req):
        parts.append("🚫 Tested without pass criteria")
    if unit_refs:
        binaries = sorted({binary for binary, _ in unit_refs})
        if junit:
            results = [junit.get(b) for b in binaries]
            if all(r is True for r in results):
                parts.append("✅ Unit (passing)")
            elif any(r is False for r in results):
                parts.append("❌ Unit (FAILING)")
            else:
                parts.append("⚠️ Unit (no result)")
        else:
            parts.append("⚠️ Unit (not run)")
    if int_refs:
        parts.append(int_status(env))
    if not parts:
        if req["status"]:
            parts.append(f"📋 {req['status']}")
        else:
            parts.append("⬜ No automated test")
    return "<br>".join(parts)


def verification_cell(unit_refs, int_refs):
    """Build the Verified-by cell listing linked unit and integration tests."""
    lines = [f"`{binary}` :: {case}" for binary, case in unit_refs]
    lines += [f"`{fname}` :: {test}" for fname, test in int_refs]
    return "<br>".join(lines) if lines else "*none*"


def main():
    """Parse args, join requirements/tests/results, and write the matrix."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--junit",
        type=Path,
        default=None,
        help="ctest --output-junit XML with unit-test results",
    )
    parser.add_argument("--sha", default=None, help="commit sha to stamp into the page")
    parser.add_argument(
        "--env",
        choices=sorted(ENVIRONMENTS),
        default="host",
        help="verification environment this build ran in: host (no hardware; board tests"
        " are reported as deferred, not unverified), desk (board on USB), rig (CI hardware)",
    )
    args = parser.parse_args()

    requirements = parse_requirements()
    unit_links = parse_unit_test_links()
    int_links = parse_int_test_links()
    junit = parse_junit(args.junit) if args.junit and args.junit.exists() else None
    if args.junit and junit is None:
        print(
            f"warning: junit file {args.junit} not found; statuses will show 'not run'",
            file=sys.stderr,
        )

    all_ids = {req["id"] for rows in requirements.values() for req in rows}
    for req_id in sorted(set(unit_links) | set(int_links)):
        if req_id not in all_ids:
            print(
                f"warning: test verifies unknown requirement '{req_id}'",
                file=sys.stderr,
            )

    total = automated = passing = deferred = 0
    sections = []
    for group, rows in requirements.items():
        group_total = group_automated = group_passing = 0
        lines = [
            "| Requirement | Description | Method | Level | Pass Criteria "
            "| Verified by | Status | Reason |",
            "|---|---|---|---|---|---|---|---|",
        ]
        for req in rows:
            req_id = req["id"]
            unit_refs = unit_links.get(req_id, [])
            int_refs = int_links.get(req_id, [])
            group_total += 1
            if unit_refs or int_refs:
                group_automated += 1
                if not has_criteria(req):
                    print(
                        f"warning: '{req_id}' has linked tests but no pass criteria"
                        " — criteria must be decided before testing"
                        " (fix: scripts/req.py set"
                        f' {req_id} --criteria "...")',
                        file=sys.stderr,
                    )
            if (
                unit_refs
                and junit
                and has_criteria(req)
                and all(junit.get(b) is True for b in {b for b, _ in unit_refs})
            ):
                group_passing += 1
            if int_refs and not unit_refs and args.env == "host":
                deferred += 1
            lines.append(
                f"| {req_id} | {req['description']} | {req['method']} "
                f"| {req['level']} | {req['criteria']} "
                f"| {verification_cell(unit_refs, int_refs)} "
                f"| {status_cell(req, unit_refs, int_refs, junit, args.env)} "
                f"| {req['reason']} |"
            )
        total += group_total
        automated += group_automated
        passing += group_passing
        summary = (
            f"{group_total} requirements &middot; {group_automated} automated "
            f"&middot; {group_passing} passing"
        )
        sections.append(f"## {group}\n\n*{summary}*\n\n" + "\n".join(lines))

    stamp = f" at commit `{args.sha[:9]}`" if args.sha else ""
    header = "\n".join(
        [
            "# Requirements Traceability Matrix",
            "",
            "<!-- GENERATED FILE — do not edit by hand. Regenerate with: make rtm -->",
            "",
            f"Generated by `scripts/generate_rtm.py`{stamp} from the `## Requirements` tables in each",
            "component's `docs/sdd.md`, the system-level tables in `docs-site/requirements/`,",
            "the `verifies` tags in `test/unit-tests/` (gtest",
            '`RecordProperty("verifies", ...)`) and `test/int/` (`@pytest.mark.verifies(...)`),',
            "and the unit-test results from CI. Integration tests require flight hardware and are",
            "listed as evidence without a CI status. Edit requirements with `scripts/req.py`;",
            "a requirement's test results only count as *passing* once its pass criteria are",
            "defined (🚫 marks tests run against undefined criteria). 📋 marks a manual",
            "assessment (e.g. from CDR) with no automated evidence yet.",
            "",
            f"**{total}** requirements &middot; **{automated}** linked to automated tests &middot; "
            f"**{passing}** verified by passing unit tests in this build &middot; "
            f"**{deferred}** deferred to hardware (environment: {args.env}, "
            f"{ENVIRONMENTS[args.env]})",
            "",
        ]
    )
    OUTPUT.write_text(header + "\n" + "\n\n".join(sections) + "\n", encoding="utf-8")
    print(
        f"wrote {OUTPUT.relative_to(REPO_ROOT)}: {total} requirements, "
        f"{automated} automated, {passing} passing, {deferred} deferred (env={args.env})"
    )


if __name__ == "__main__":
    main()
