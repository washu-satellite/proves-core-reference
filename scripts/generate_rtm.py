#!/usr/bin/env python3
"""Generate the requirements traceability matrix (RTM).

Joins three sources into docs-site/requirements-matrix.md:

1. Requirements: the "## Requirements" table in every component
   PROVESFlightControllerReference/Components/**/docs/sdd.md.
2. Test links: requirement IDs declared in tests —
   - gtest:  RecordProperty("verifies", "Comp-1,Comp-2") inside TEST/TEST_F
   - pytest: @pytest.mark.verifies("Comp-1", "Comp-2") on integration tests
3. Results: an optional ctest JUnit XML (ctest --output-junit). gtest results
   are reported per test binary (one ctest case per binary), so a unit test's
   status is the status of the binary that contains it. Integration tests
   require flight hardware and are never run in CI; they are listed as
   hardware-verified evidence without a CI status.

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
UNIT_TEST_DIR = REPO_ROOT / "PROVESFlightControllerReference" / "test" / "unit-tests"
INT_TEST_DIR = REPO_ROOT / "PROVESFlightControllerReference" / "test" / "int"
OUTPUT = REPO_ROOT / "docs-site" / "requirements-matrix.md"

TEST_MACRO_RE = re.compile(r"^TEST(?:_F)?\(\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*\)", re.M)
RECORD_PROP_RE = re.compile(r'RecordProperty\(\s*"verifies"\s*,\s*"([^"]+)"')
PYTEST_MARK_RE = re.compile(r"@pytest\.mark\.verifies\(([^)]*)\)")
PY_DEF_RE = re.compile(r"^def\s+(test_[A-Za-z0-9_]+)", re.M)
TABLE_SEP_RE = re.compile(r"^:?-+:?$")


def parse_requirements():
    """Return {component: [(req_id, description, validation)]}, insertion-ordered."""
    requirements = {}
    for sdd in sorted(COMPONENTS_DIR.rglob("docs/sdd.md")):
        component = sdd.parent.parent.name
        text = sdd.read_text(encoding="utf-8")
        match = re.search(r"^##\s+Requirements\s*$(.*?)(?=^##\s|\Z)", text, re.M | re.S)
        if not match:
            continue
        rows = []
        for line in match.group(1).splitlines():
            line = line.strip()
            if not line.startswith("|"):
                continue
            cells = [c.strip() for c in line.strip("|").split("|")]
            if len(cells) < 2:
                continue
            req_id = cells[0]
            # Skip header, separator, and placeholder rows.
            if req_id.lower() == "name" or not req_id:
                continue
            if all(TABLE_SEP_RE.match(c) or not c for c in cells):
                continue
            description = cells[1] if len(cells) > 1 else ""
            validation = cells[2] if len(cells) > 2 else ""
            rows.append((req_id, description, validation))
        if rows:
            requirements[component] = rows
    return requirements


def split_ids(raw):
    return [r for r in re.split(r"[,\s]+", raw.strip().strip('"').strip("'")) if r]


def parse_unit_test_links():
    """Return {req_id: [(binary, case_ref)]} from RecordProperty tags."""
    links = {}
    for src in sorted(UNIT_TEST_DIR.glob("test_*.cpp")):
        binary = src.stem
        matches = list(TEST_MACRO_RE.finditer(src.read_text(encoding="utf-8")))
        text = src.read_text(encoding="utf-8")
        for i, m in enumerate(matches):
            body_end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
            body = text[m.start():body_end]
            for prop in RECORD_PROP_RE.finditer(body):
                for req_id in split_ids(prop.group(1)):
                    links.setdefault(req_id, []).append((binary, f"{m.group(1)}.{m.group(2)}"))
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


def status_cell(unit_refs, int_refs, junit):
    parts = []
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
        parts.append("🛰️ Integration (hardware)")
    if not parts:
        parts.append("⬜ No automated test")
    return "<br>".join(parts)


def verification_cell(unit_refs, int_refs, validation):
    lines = [f"`{binary}` :: {case}" for binary, case in unit_refs]
    lines += [f"`{fname}` :: {test}" for fname, test in int_refs]
    if not lines:
        return f"*{validation}*" if validation else "*none*"
    return "<br>".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--junit", type=Path, default=None, help="ctest --output-junit XML with unit-test results")
    parser.add_argument("--sha", default=None, help="commit sha to stamp into the page")
    args = parser.parse_args()

    requirements = parse_requirements()
    unit_links = parse_unit_test_links()
    int_links = parse_int_test_links()
    junit = parse_junit(args.junit) if args.junit and args.junit.exists() else None
    if args.junit and junit is None:
        print(f"warning: junit file {args.junit} not found; statuses will show 'not run'", file=sys.stderr)

    all_ids = {req_id for rows in requirements.values() for req_id, _, _ in rows}
    for req_id in sorted(set(unit_links) | set(int_links)):
        if req_id not in all_ids:
            print(f"warning: test verifies unknown requirement '{req_id}'", file=sys.stderr)

    total = automated = passing = 0
    sections = []
    for component, rows in requirements.items():
        lines = [f"## {component}", "", "| Requirement | Description | Verified by | Status |", "|---|---|---|---|"]
        for req_id, description, validation in rows:
            unit_refs = unit_links.get(req_id, [])
            int_refs = int_links.get(req_id, [])
            total += 1
            if unit_refs or int_refs:
                automated += 1
            if unit_refs and junit and all(junit.get(b) is True for b in {b for b, _ in unit_refs}):
                passing += 1
            lines.append(
                f"| {req_id} | {description} | "
                f"{verification_cell(unit_refs, int_refs, validation)} | "
                f"{status_cell(unit_refs, int_refs, junit)} |"
            )
        sections.append("\n".join(lines))

    stamp = f" at commit `{args.sha[:9]}`" if args.sha else ""
    header = "\n".join([
        "# Requirements Traceability Matrix",
        "",
        "<!-- GENERATED FILE — do not edit by hand. Regenerate with: make rtm -->",
        "",
        f"Generated by `scripts/generate_rtm.py`{stamp} from the `## Requirements` tables in each",
        "component's `docs/sdd.md`, the `verifies` tags in `test/unit-tests/` (gtest",
        '`RecordProperty("verifies", ...)`) and `test/int/` (`@pytest.mark.verifies(...)`),',
        "and the unit-test results from CI. Integration tests require flight hardware and are",
        "listed as evidence without a CI status.",
        "",
        f"**{total}** requirements &middot; **{automated}** linked to automated tests &middot; "
        f"**{passing}** verified by passing unit tests in this build",
        "",
    ])
    OUTPUT.write_text(header + "\n" + "\n\n".join(sections) + "\n", encoding="utf-8")
    print(f"wrote {OUTPUT.relative_to(REPO_ROOT)}: {total} requirements, "
          f"{automated} automated, {passing} passing")


if __name__ == "__main__":
    main()
