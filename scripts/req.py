#!/usr/bin/env python3
"""Terminal editor for requirement tables — no hand-editing markdown.

Requirements live in markdown tables (component docs/sdd.md files and the
system-level docs under docs-site/requirements/). This tool finds and edits
them in place, normalizing tables to the extended format:

    | Name | Description | Method | Level | Pass Criteria | Status | Reason |

Method: Unit Test / Integration Test / Analysis / Inspection / Demonstration.
Level (where the requirement is proven): Unit / Board / Subsystem / Flatsat /
Environmental. Pass Criteria is the measurable pass/fail condition — it MUST
be defined before a requirement is tested, and this tool refuses to record a
Pass/Fail status for a requirement whose criteria are still TBD.

Examples:
    scripts/req.py list
    scripts/req.py list --group TelemetryGate
    scripts/req.py show CDH-3
    scripts/req.py set TM-L2-02 --criteria "Interval commandable 1-60 s, takes effect within 2 cycles" --level Flatsat
    scripts/req.py set TM-L2-02 --status "Fail" --reason "Sampling loops fixed-frequency; not runtime configurable"
    scripts/req.py add --group "CDH L1 Requirements" --id CDH-32 --description "..." --method "Test" --level Subsystem
    scripts/req.py rtm    # regenerate the traceability matrix after edits

After editing, regenerate the matrix with `make rtm` (or `scripts/req.py rtm`).
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
COMPONENTS_DIR = REPO_ROOT / "PROVESFlightControllerReference" / "Components"
SYSTEM_REQ_DIR = REPO_ROOT / "docs-site" / "requirements"

FIELDS = ["id", "description", "method", "level", "criteria", "status", "reason"]
HEADER = "| Name | Description | Method | Level | Pass Criteria | Status | Reason |"
SEPARATOR = "|---|---|---|---|---|---|---|"

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

LEVELS = {"unit", "board", "subsystem", "flatsat", "environmental"}
CRITERIA_PLACEHOLDERS = {"", "tbd", "todo", "-", "n/a"}
TABLE_SEP_RE = re.compile(r"^:?-+:?$")


class Table:
    """One requirements table: its file, section, line span, and rows."""

    def __init__(self, path, group, lines, start, end, rows):
        self.path = path
        self.group = group
        self.lines = lines  # full file, as a list of lines
        self.start = start  # index of first table line
        self.end = end  # index one past the last table line
        self.rows = rows  # list of {field: value}

    def render(self):
        """Return the table re-rendered in the extended column format."""
        out = [HEADER, SEPARATOR]
        for row in self.rows:
            out.append("|" + "|".join(row[f] for f in FIELDS) + "|")
        return out

    def save(self):
        """Write the file back with this table's lines replaced."""
        new_lines = self.lines[: self.start] + self.render() + self.lines[self.end :]
        self.path.write_text("\n".join(new_lines) + "\n", encoding="utf-8")


def split_cells(line):
    """Split a markdown table line into stripped cell strings."""
    return [c.strip() for c in line.strip().strip("|").split("|")]


def parse_section_table(path, group, lines, lo, hi):
    """Parse the first markdown table between line indices lo..hi."""
    columns = None
    rows = []
    start = end = None
    for i in range(lo, hi):
        line = lines[i].strip()
        if not line.startswith("|"):
            if start is not None:
                break
            continue
        if start is None:
            start = i
        end = i + 1
        cells = split_cells(line)
        if all(TABLE_SEP_RE.match(c) or not c for c in cells):
            continue
        lowered = [c.lower() for c in cells]
        if columns is None and lowered[0] in ("name", "requirement", "id"):
            columns = [HEADER_ALIASES.get(c) for c in lowered]
            continue
        if columns is None:
            columns = ["id", "description", "method"]
        row = {f: "" for f in FIELDS}
        for field, value in zip(columns, cells):
            if field:
                row[field] = value
        if row["id"]:
            rows.append(row)
    if start is None or not rows:
        return None
    return Table(path, group, lines, start, end, rows)


def load_tables():
    """Parse every requirements table in the repo into Table objects."""
    tables = []
    if SYSTEM_REQ_DIR.is_dir():
        for path in sorted(SYSTEM_REQ_DIR.glob("*.md")):
            lines = path.read_text(encoding="utf-8").splitlines()
            headings = [
                (i, line[3:].strip())
                for i, line in enumerate(lines)
                if line.startswith("## ")
            ]
            for n, (i, title) in enumerate(headings):
                hi = headings[n + 1][0] if n + 1 < len(headings) else len(lines)
                table = parse_section_table(path, title, lines, i + 1, hi)
                if table:
                    tables.append(table)
    for path in sorted(COMPONENTS_DIR.rglob("docs/sdd.md")):
        component = path.parent.parent.name
        lines = path.read_text(encoding="utf-8").splitlines()
        for i, line in enumerate(lines):
            if re.match(r"^##\s+Requirements\s*$", line):
                hi = len(lines)
                for j in range(i + 1, len(lines)):
                    if lines[j].startswith("## "):
                        hi = j
                        break
                table = parse_section_table(path, component, lines, i + 1, hi)
                if table:
                    tables.append(table)
                break
    return tables


def find_requirement(tables, req_id):
    """Return (table, row) holding req_id, or (None, None)."""
    for table in tables:
        for row in table.rows:
            if row["id"] == req_id:
                return table, row
    return None, None


def has_criteria(row):
    """Return True if the row has real pass criteria (not TBD/blank)."""
    return row["criteria"].lower() not in CRITERIA_PLACEHOLDERS


def check_level(value):
    """Exit with an error if value is not a recognized proof level."""
    if value and value.lower() not in LEVELS:
        sys.exit(
            f"error: level '{value}' not recognized; use one of: "
            + ", ".join(sorted(LEVELS))
        )


def apply_fields(row, args, table):
    """Validate and write the requested field updates, then save the file."""
    updates = {
        "description": args.description,
        "method": args.method,
        "level": args.level,
        "criteria": args.criteria,
        "status": args.status,
        "reason": args.reason,
    }
    if all(v is None for v in updates.values()):
        sys.exit(
            "error: nothing to change (pass --description/--method/--level/"
            "--criteria/--status/--reason)"
        )
    check_level(args.level)
    new_status = updates["status"] if updates["status"] is not None else row["status"]
    new_criteria = (
        updates["criteria"] if updates["criteria"] is not None else row["criteria"]
    )
    if new_status.lower().startswith(("pass", "fail")) and (
        new_criteria.lower() in CRITERIA_PLACEHOLDERS
    ):
        sys.exit(
            f"error: refusing to set status '{new_status}' on {row['id']}: "
            "pass criteria must be decided before a requirement is tested. "
            'Set them in the same call: --criteria "<measurable condition>"'
        )
    if new_status.lower().startswith("fail") and not (
        updates["reason"] or row["reason"]
    ):
        sys.exit(
            f"error: a Fail status on {row['id']} requires --reason (why it failed)"
        )
    for field, value in updates.items():
        if value is not None:
            row[field] = value
    table.save()
    rel = table.path.relative_to(REPO_ROOT)
    print(f"updated {row['id']} in {rel} (group: {table.group})")
    print("regenerate the matrix with: make rtm")


def cmd_list(tables, args):
    """Print a one-line summary of every requirement."""
    fmt = "{:<14} {:<34} {:<18} {:<13} {:<9} {}"
    print(fmt.format("ID", "Group", "Method", "Level", "Criteria", "Status"))
    for table in tables:
        if args.group and args.group.lower() not in table.group.lower():
            continue
        for row in table.rows:
            print(
                fmt.format(
                    row["id"],
                    table.group[:34],
                    row["method"][:18],
                    row["level"][:13],
                    "yes" if has_criteria(row) else "TBD",
                    row["status"],
                )
            )


def cmd_show(tables, args):
    """Print one requirement's fields and source file."""
    table, row = find_requirement(tables, args.id)
    if not row:
        sys.exit(f"error: requirement '{args.id}' not found")
    print(f"file:  {table.path.relative_to(REPO_ROOT)}")
    print(f"group: {table.group}")
    for field in FIELDS:
        print(f"{field:<12} {row[field]}")
    if not has_criteria(row):
        print("\nnote: pass criteria are TBD — decide them before testing:")
        print(f'  scripts/req.py set {args.id} --criteria "<measurable condition>"')


def cmd_set(tables, args):
    """Update fields on an existing requirement."""
    table, row = find_requirement(tables, args.id)
    if not row:
        sys.exit(f"error: requirement '{args.id}' not found (see: req.py list)")
    apply_fields(row, args, table)


def cmd_add(tables, args):
    """Append a new requirement to a group's table."""
    matches = [t for t in tables if t.group.lower() == args.group.lower()]
    if not matches:
        groups = "\n  ".join(t.group for t in tables)
        sys.exit(
            f"error: group '{args.group}' not found. Available groups:\n  {groups}"
        )
    table = matches[0]
    _, existing = find_requirement(tables, args.id)
    if existing:
        sys.exit(f"error: requirement '{args.id}' already exists (use: req.py set)")
    check_level(args.level)
    row = {f: "" for f in FIELDS}
    row["id"] = args.id
    row["description"] = args.description
    for field in ("method", "level", "criteria", "status", "reason"):
        value = getattr(args, field)
        if value is not None:
            row[field] = value
    table.rows.append(row)
    table.save()
    print(
        f"added {args.id} to {table.path.relative_to(REPO_ROOT)} (group: {table.group})"
    )
    print("regenerate the matrix with: make rtm")


def cmd_rtm(_tables, _args):
    """Regenerate the traceability matrix."""
    subprocess.run(
        [sys.executable, str(REPO_ROOT / "scripts" / "generate_rtm.py")],
        check=True,
    )


def add_field_args(sub, require_desc=False):
    """Attach the shared field options to a subcommand parser."""
    sub.add_argument("--description", required=require_desc)
    sub.add_argument(
        "--method",
        help="Unit Test / Integration Test / Analysis / Inspection / Demonstration",
    )
    sub.add_argument(
        "--level", help="Unit / Board / Subsystem / Flatsat / Environmental"
    )
    sub.add_argument("--criteria", help="measurable pass/fail condition")
    sub.add_argument(
        "--status", help="Pass / Fail / or an assessment like 'CDR: Partial'"
    )
    sub.add_argument("--reason", help="why the requirement failed (required for Fail)")


def main():
    """Parse the command line and dispatch to a subcommand."""
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    commands = parser.add_subparsers(dest="command", required=True)

    sub = commands.add_parser("list", help="list all requirements")
    sub.add_argument("--group", help="filter by group/component name substring")
    sub.set_defaults(func=cmd_list)

    sub = commands.add_parser("show", help="show one requirement in full")
    sub.add_argument("id")
    sub.set_defaults(func=cmd_show)

    sub = commands.add_parser("set", help="update fields of a requirement")
    sub.add_argument("id")
    add_field_args(sub)
    sub.set_defaults(func=cmd_set)

    sub = commands.add_parser("add", help="add a new requirement to a group")
    sub.add_argument("--group", required=True, help="exact group name (see: list)")
    sub.add_argument("--id", required=True)
    add_field_args(sub, require_desc=True)
    sub.set_defaults(func=cmd_add)

    sub = commands.add_parser("rtm", help="regenerate the traceability matrix")
    sub.set_defaults(func=cmd_rtm)

    args = parser.parse_args()
    args.func(load_tables(), args)


if __name__ == "__main__":
    main()
