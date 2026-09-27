# 03 — Advisory: design and method (the coder may deviate if every result in 01/02 holds)

## 1. One aggregator that imports the packet module — why not one bigger `check_packet_set.py`

Jesse's rule: reuse with modification is fine while the thing stays modular and does one job. `check_packet_set.py`
today is one parser (the `.fppi` packet set) plus one config read plus a report. The audit needs five more readers
(a C++ `enum`/`static const` header, an fpp `constant`, an fpp `enum`, topology connection indices, a dictionary
JSON) and a uniform report/exit contract across ten lines. Folding all of that into the packet script would make a
file whose name says one thing and whose body does six, and would force every doc that cites its lines to change.

So: **keep `scripts/check_packet_set.py` as a module with a CLI** (one small refactor: lift the body of `main()` into
`check(packets_path, config_path) -> PacketSetResult` carrying `packets`, `packet_channels`, `omit_channels`,
`max_packets`, `limit_name`, `limit`; `main()` prints today's six lines from it, unchanged), and **add
`scripts/check_capacity.py`** that `sys.path`-imports it (`# noqa: E402` for ruff's import-sort hook) and turns the
result into the first two audit lines. The packetizer parsing lives in exactly one place; the audit is a list of
small check functions with one shared line formatter. This is composition, not a parallel implementation.

`verify.sh` calls only the audit. The docs that cite `check_packet_set.py` (`CLAUDE.md:61`, `TlmPacketizerCfg.hpp:26`
comment, `FaultManager/docs/sdd.md:110`, ROADMAP rule 5) stay true because the script still exists and still prints
its lines; update `CLAUDE.md:61` and ROADMAP rule 5 to name the audit as the gate's caller (docs-only), leave the
header comment alone (no `project/config` diff).

## 2. Check functions (each returns one `Line(name, used, limit, detail, status, reason)`)

Shared helpers (all regex, no fpp tooling):
- `cxx_int(text, NAME)` — `NAME\s*=\s*(\d+)` inside `enum {}` or `static const ... NAME = N;` (reuse
  `check_packet_set.parse_limit`, which already handles the `= N;` form; extend for the enum `= N,` form).
- `fpp_const(text, NAME)` — `^\s*constant NAME = (\d+)`; a non-literal RHS → exit 2 (`AcConstants.fpp:25` is such a
  case for a constant this row does not read).
- `fpp_enum(text, ENUM)` — `enum ENUM(: T)? { NAME = n ... }` → `{NAME: n}`; enumerators without `= n` take the
  previous + 1 (fpp default), first is 0. `FaultType` and `SchedTask` both use explicit values.
- `connections(text, r"(\w+)\.RateGroupMemberOut\[([^\]]+)\]")` etc. — every `X.port[idx]` occurrence on a
  `->` line after stripping `#` comments; `idx` numeric or symbolic. Symbolic: split on `.`, take the last two
  parts as `Enum.NAME`, look up in the enums collected from `--task-gate` and `--fault-types`; miss → exit 2.
- `mask_bits(text)` — `param AUTHORITY_MASK: (U8|U16|U32|U64)` → 8/16/32/64.
- `dictionary(path_or_mode)` — `auto` search as 01 §1.1; parse; keep only the three keys; `projectVersion`
  hash = the group after `-g` in `git describe` form; tree HEAD via `git -C $R rev-parse HEAD`.

Line formatter: one function producing exactly the grammar in 01 §1.3, and one `RESULT:` summariser. WARN threshold
applies only to the boot-assert class; keep `WARN_FRACTION` in one place (`--warn-fraction`).

Instances of rate groups are discovered from the topology, not from `instances.fpp`, so a fourth rate group appears
as a fourth line without a script change.

## 3. `verify.sh` steps
1. `BUILD="${VERIFY_BUILD_DIR:-build-gtest}"`; replace every literal `build-gtest` with `"$BUILD"`.
2. `STAGES="${VERIFY_STAGES:-host-tests,int-collect,audit,script-tests,pre-commit,rtm}"`; `stage() { case
   ",$STAGES," in *",$1,"*) return 0;; *) return 1;; esac; }`; wrap each `say` block in `if stage <name>; then ... fi`.
   `int-collect` is today's "integration tests: collect + lint" block; the board blocks stay keyed on `VERIFY_ENV`.
3. host-tests: `rm -rf "$BUILD"` first (measured cost on this Mac: 29 s clean vs 2.8 s no-op, 30 binaries; googletest
   is in-tree via `add_subdirectory`, `test/unit-tests/CMakeLists.txt:11`, so nothing is re-downloaded).
4. audit block per 01 §V3; parse SKIP lines with `grep -E '^\s*\S+: \?/'`.
5. script-tests block per 01 §V4 with `VERIFY_NESTED=1` exported; guard at the top of the script: `[ -n
   "${VERIFY_NESTED:-}" ] && [ -z "${VERIFY_STAGES:-}" ] && { echo "nested verify.sh without VERIFY_STAGES"; exit 2; }`.
6. rtm block: add `--script-junit "$BUILD/scripts-junit.xml"`.
7. summary: print `stages: ...` line only when `VERIFY_STAGES` was set.

## 4. `generate_rtm.py` extension (~25 lines)
`SCRIPT_TEST_DIR = REPO_ROOT / "scripts" / "tests"`; `parse_script_test_links()` = `parse_int_test_links()` over
`test_*.py` there (factor the loop body into a helper taking the directory and glob). Links join the pytest junit by
**test function name** (`testcase@name`), not by file, because `parse_junit` keys on `name`; treat them as unit refs
in `status_cell` so a green run shows `✅ Unit (passing)` and a missing junit shows `⚠️ Unit (not run)`. New arg
`--script-junit PATH`. Nothing else in the matrix changes; the header counts move as 01 §V5 says.

## 5. Tests (written by the test-author from 01/02 before any code)
- `scripts/tests/conftest.py`: registers the `verifies` marker (`config.addinivalue_line`) because
  `PROVESFlightControllerReference/pytest.ini` is not an ancestor of `scripts/tests`; fixtures: `repo` (Path),
  `run_audit(args) -> (rc, stdout, stderr)` via `subprocess.run([sys.executable, "scripts/check_capacity.py", ...])`,
  `doctored(path, edit_fn) -> tmp path`, `synthetic_dictionary(n_opcodes, n_params, project_version)` writing the
  three-key JSON (never depends on `~/scalar-build` existing; the one "build copy" assertion in AUDIT-3/4 is
  `pytest.skip`ped when `auto` finds no dictionary).
- `scripts/tests/test_check_capacity.py`: one test per criteria clause; parse lines with
  `re.compile(r"^(\S+): (\d+|\?)/(\d+) used(?:, (\d+) free)?(?: \((.*?)\))? — (OK|WARN|INFO|FAIL|SKIP)(?:: (.*))?$")`.
- `scripts/tests/test_verify_sh.py`: AUDIT-9 per its criteria; uses `tmp_path` as `VERIFY_BUILD_DIR`; skips itself
  when `VERIFY_NESTED` is set **and** `VERIFY_STAGES` is `host-tests` (i.e. it is the nested run).
- Docstrings on every test module and function: the `interrogate` hook fails under 40 % coverage
  (`.pre-commit-config.yaml:42-49`).

## 6. File-by-file order (one commit, gate before it)
1. `docs-site/requirements/tooling.md` (seed) + `mkdocs.yml` nav + eight `req.py add` — test-author.
2. `scripts/tests/*` — test-author; hashes pinned in the review.
3. `scripts/check_packet_set.py` refactor (`check()`), `scripts/check_capacity.py`, `scripts/generate_rtm.py`,
   `scripts/verify.sh` — coder.
4. `CLAUDE.md`: Traps line 61 (name the audit), Commands block (add the audit line); `docs-site/dev-loop-findings.md`:
   append `04-findings.md`; ROADMAP rule 5 wording and row 2 DONE — reviewer.
Commit: `build(tooling): capacity audit gate (check_capacity.py), clean host build in verify.sh`.

## 7. Open risks
- A dictionary from the build copy can be stale relative to the tree; the WARN line is advisory. The honest fix is
  a target build in CI (issue #3/#5), out of scope.
- The regexes assume one connection per line and `->` syntax as in today's topology; fpp also allows
  `match`/pattern connections, which the script does not expand (command/telemetry patterns are not among the
  arrays checked).
- `git rev-parse` inside the script needs a git checkout; in a tarball it degrades to the WARN line.
