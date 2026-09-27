# 01 — Normative: results the tests are written from

Everything here is observable from a command line, exit codes and file contents. `$PY`, `$R`, `DL` as in `README.md`.
"Synthetic tree" = a directory the test builds under `tmp_path` holding `docs-site/dev-loop/{README.md, ROADMAP.md,
decisions/README.md, cycles/README.md, hardware-procedures/README.md}` and `CLAUDE.md`, passed as `--root`; for rules 4–5 the
test runs `git init`, commits, and controls dates with `GIT_AUTHOR_DATE`/`GIT_COMMITTER_DATE`. The real tree is never edited.

## 1. `scripts/check_docs.py`

### 1.1 Command line and defaults
```
$PY scripts/check_docs.py [--root DIR] [--dev-loop DIR] [--claude-md PATH] [--roadmap PATH] [--cycles-index PATH]
    [--decisions DIR] [--git auto|none] [--head REV] [--prune NAME ...]
```
| Flag | Default | Used by |
|---|---|---|
| `--root` | `$R` (the script's `parents[1]`) | rule 1 walk; rule 2 consumer search (`docs-site/**/*.md`, `CLAUDE.md`) |
| `--dev-loop` | `<root>/docs-site/dev-loop` | rules 2, 3, 7 |
| `--claude-md` | `<root>/CLAUDE.md` | rules 2, 3 (absent file → not an error, just not scanned) |
| `--roadmap` | `<dev-loop>/ROADMAP.md` | rule 6 |
| `--cycles-index` | `<dev-loop>/cycles/README.md` | rules 4, 7 |
| `--decisions` | `<dev-loop>/decisions` | rules 2, 5 |
| `--git` | `auto` (= `<root>` when `<root>/.git` exists, else none) | rules 4, 5 |
| `--head` | `HEAD` | rule 4 ancestor target |
| `--prune` | `lib build* .git .venv fprime-venv node_modules` (+ any dir holding `pyvenv.cfg`) | rule 1 walk; repeatable, replaces the default list |

### 1.2 Exit codes
`0` no rule FAILs (OK/WARN/SKIP mix); `1` ≥ 1 FAIL; `2` an input cannot be used (`--root`/`--dev-loop`/`--decisions` not a
directory, `--roadmap`/`--cycles-index` missing, `--git` a path without `.git`, `--head` unresolvable). One `error: <what>
(<path>)` line on stderr; stdout may be partial. A missing `hardware-procedures/README.md` is not an error (index list shrinks).

### 1.3 Output grammar (stdout)
1. Seven rule lines, in rule order, each exactly `DOCS-<n> <slug>: <count> <unit> — <STATUS>[: <reason>]` with
   `STATUS ∈ {OK, WARN, FAIL, SKIP}`, em dash U+2014 spaced, and slugs `conflict-copies`, `decision-records`, `links`,
   `landed-hashes`, `append-only`, `typed-state`, `orphans`; `<count>` is the number *checked* (§2 says what), `<unit>` a noun.
2. After a FAIL/WARN line, one indented line per offender: `  - <path relative to root>[: <what>]` (sorted, deterministic).
3. Last line: `RESULT: OK — <a> ok, <w> warn, <s> skip, <f> fail` or `RESULT: FAIL — …` (FAIL iff `f > 0`).

## 2. Rules (each is one requirement row; "fixture" = what makes it FAIL on a synthetic tree)
- **R1 `conflict-copies`.** Walk `<root>` pruning `--prune` names at any depth. Offender: any file **or directory** whose
  basename matches `^.+ [2-9](\.[^.]*)?$` (iCloud's `<name> 2`, `<name> 2.<ext>`, `<name> 3`). count = offenders, unit
  `conflict copies`; FAIL if > 0, else `0 conflict copies — OK`. Fixtures: `<root>/a/README 2.md` FAIL; a directory
  `<root>/docs 2/` FAIL; `<root>/lib/x 2.h` and `<root>/build-gtest/y 2` OK (pruned). Today: 14 offenders (`04-findings.md` §1).
- **R2 `decision-records`.** Under `--decisions`, every `D-*.md` (a) matches `^D-(\d{3})-[a-z0-9]+(-[a-z0-9]+)*\.md$`, (b) has
  a unique ID, (c) is linked from `decisions/README.md` by `[D-nnn](<its filename>)`, (d) has ≥ 1 consumer: a `[D-nnn](…)`
  link whose target resolves to that file, in a `.md` under `<root>/docs-site/` outside `--decisions`, or in `--claude-md`.
  Also every `[D-nnn](target)` link in `<dev-loop>/**/*.md` and `--claude-md` must resolve to an existing file whose basename
  starts with `D-nnn-`. count = records, unit `decision records`. Fixtures: `D-6-x.md` (2 digits) FAIL; two files `D-001-*`
  FAIL; a record absent from the README table FAIL; a record with no consumer FAIL; `[D-009](D-009-x.md)` in ROADMAP with
  no such file FAIL; `[D-001](D-002-y.md)` FAIL. Today: 5 records, all OK.
- **R3 `links`.** In `<dev-loop>/**/*.md` (pruned as R1, so `cycles 2/` is skipped) and `--claude-md`: every Markdown
  link `[..](target)` / `![..](target)` **outside fenced code blocks and inline code** whose target has no URL scheme and is not
  `#…` only, must resolve (after stripping `#anchor`, relative to the file's directory) to an existing file or directory.
  count = links checked, unit `links`. Fixtures: `[x](missing.md)` FAIL; the same inside a ``` block OK; `[x](../cycles/)` OK.
  Today: 14 links, OK (the 15th is inside a fenced block in `cycle-h-plan/02-requirements.md`).
- **R4 `landed-hashes`.** In `--cycles-index`, the first table whose header row has a `Landed` column: every token
  `\b[0-9a-f]{7,40}\b` in that column of every row must name a commit that is an ancestor of (or equal to) `--head`
  (`git merge-base --is-ancestor`). count = hashes, unit `landed hashes`. Nothing else is scanned: ROADMAP rows and
  D-records may cite library or upstream commits, cycle plans/reviews may cite pre-merge branch commits. `--git none` → `DOCS-4 landed-hashes: 0 landed hashes — SKIP: no git`.
  Fixtures (synthetic repo, two commits on `main`, one on an unmerged branch): the branch hash in Landed → FAIL
  (`<hash>: not an ancestor of HEAD`); a non-existent hash → FAIL (`not a commit`); `see review` (no hash) → counted 0, OK.
  Today: 17 hashes, all ancestors.
- **R5 `append-only`.** For each D-record tracked in git: `intro` = the first commit that added the path; `baseline` = the
  path's content at the **last** commit touching it whose author date (calendar day in that commit's own timezone) equals
  `intro`'s author day. The working-tree content must equal `baseline`, or equal `baseline` with exactly one line prepended
  before the first line, matching `^Superseded by (\[D-\d{3}\]\([^)]*\)|D-\d{3})\b.*$`, optionally followed by one blank
  line. Anything else → FAIL (`<path>: modified after <intro day>`). An untracked record → OK (`new, unchecked`), counted.
  count = records, unit `decision records`. `--git none` → SKIP. Fixtures: commit `D-001-x.md` on day 1, commit an edit on
  day 2 → FAIL; edit on day 1 in a second commit → OK; day-2 commit that only prepends `Superseded by D-002` → OK; the same
  plus a changed word → FAIL; working-tree edit without commit → FAIL. Today: 5 records, all at their introducing commit.
- **R6 `typed-state`.** `--roadmap` has no line matching `^[\s*_]*State:` and no table row (`^\|`) containing `**DONE`.
  count = offending lines, unit `typed-state lines`; FAIL if > 0. Fixtures: a `State: all green` line FAIL; a row
  `| 1 | **DONE** …` FAIL; the word "state" in prose OK. Today: 0.
- **R7 `orphans` (WARN only).** Indexes = `<dev-loop>/README.md`, `decisions/README.md`, `cycles/README.md`,
  `hardware-procedures/README.md` (those that exist). Every other `.md` under `<dev-loop>` (pruned as R1) is *referenced* if
  the text of any index contains its basename, its stem, its ID prefix (`^[A-Z]{1,6}-\d{2,3}` of the stem, e.g. `HP-07`) or
  the path of any ancestor directory relative to `<dev-loop>` followed by `/` (e.g. `cycle-h-plan/`, `stored-data/`).
  count = orphans, unit `orphans`; WARN if > 0 with one `  - <path>` per orphan; never FAIL. Fixture: `design/stray.md`
  mentioned nowhere → WARN, exit 0. Today: 0 (this plan directory becomes 0 once `cycles/README.md` lists `cycle-i-plan/`).

## 3. `scripts/doc_status.py`
```
$PY scripts/doc_status.py [--root DIR] [--output PATH] [--check] [--stdout] [--cycles-index PATH] [--matrix PATH]
    [--capacity-output FILE] [--docs-output FILE] [--git auto|none]
```
Defaults: `--output <root>/docs-site/dev-loop/STATUS.md`; `--matrix <root>/docs-site/requirements-matrix.md`;
`--capacity-output` unset → runs `$PY scripts/check_capacity.py --dictionary none` (deterministic on any host);
`--docs-output` unset → runs `check_docs.py` with defaults; a FILE holds that script's stdout instead (tests use this).
- **S1 content**, exactly this shape, ending in one newline, no trailing spaces:
  ```
  <!-- GENERATED by scripts/doc_status.py — do not edit. Regenerate: VERIFY_ENV=host scripts/verify.sh -->
  # Status (generated)

  Generated at HEAD <40-hex> (<YYYY-MM-DD>, HEAD's commit date)        ← "Generated at HEAD unknown (no git)" with --git none

  ## Last landed cycle
  <the last table row of --cycles-index, verbatim>

  ## Capacity (scripts/check_capacity.py --dictionary none)
  ```text … every line of the capacity output, verbatim …```

  ## Requirements matrix
  <the first line of --matrix matching ^\*\*\d+\*\* requirements, verbatim>

  ## Documentation lint
  <the RESULT: line of the check_docs output>
  ```
- **S2 modes.** Default: write `--output` (parents created), exit 0, print `wrote <path>`. `--stdout`: print instead of
  writing. `--check`: write nothing; exit 0 iff the existing `--output`, with its `Generated at HEAD` line removed, equals the
  regenerated content with that line removed; else exit 1 with `STATUS.md: stale (<first differing line number>)` or
  `STATUS.md: missing`. Exit 2 when `--cycles-index`/`--matrix`/a given FILE is missing or the matrix has no count line.
- Fixtures: a synthetic matrix line, cycles index and two captured outputs → generated file byte-equals the expected
  text; `--check` on that file exits 0; after changing one character of the body exits 1; after changing only the
  `Generated at HEAD` line exits 0; a missing file exits 1 `missing`.

## 4. `scripts/verify.sh` results (V1–V4)
- **V1 `docs` stage.** `ALL_STAGES` becomes `host-tests,int-collect,audit,script-tests,docs,pre-commit,rtm`. With
  `VERIFY_STAGES=docs`: header `== documentation lint (scripts/check_docs.py)`, every stdout line of `check_docs.py`
  indented two spaces, exit 1 → `fail=1`, `unverified+=("documentation lint")`, exit 2 → `unverified+=("documentation lint:
  input error")`, every SKIP line → `deferred+=("documentation lint: <slug> (needs git)")`. Then `doc_status.py --check`
  (its one line indented); exit 1 → `fail=1`, `unverified+=("STATUS.md stale or hand-edited")`.
- **V2 `rtm` stage** runs `doc_status.py` after `generate_rtm.py` and prints `  STATUS.md: unchanged` or `  STATUS.md:
  regenerated (commit it)` (decided by `git diff --quiet -- docs-site/dev-loop/STATUS.md`); never fails on its own.
- **V3 configure errors visible.** New knob `VERIFY_UT_DIR` (default `PROVESFlightControllerReference/test/unit-tests`).
  Configure output goes to `$BUILD/configure.log`; on configure failure the stage prints `  host configure FAILED (see
  $BUILD/configure.log):` and the last 20 lines of that log indented four spaces, `fail=1`, `unverified+=("host unit tests")`.
  Fixture: `VERIFY_STAGES=host-tests VERIFY_UT_DIR=<tmp>` where `<tmp>/CMakeLists.txt` is
  `cmake_minimum_required(VERSION 3.16)\nproject(x)\nmessage(FATAL_ERROR "synthetic configure failure")` → exit 1, stdout
  contains `synthetic configure failure` and `unverified` lists `host unit tests`. A passing build prints nothing new.
- **V4 unchanged elsewhere.** Sections and wording of the other stages are today's; `VERIFY_ENV=host scripts/verify.sh`
  on the tree (conflict copies removed) → `result: PASS`, `unverified: (none)`; the three existing `test_verify_sh.py` cases pass unmodified.

## 5. Harm table (measurable)
| Existing behaviour | After | Proof |
|---|---|---|
| Flight code, `.fpp`, `project/`, `lib/` | untouched | `git diff --stat 6abcaabd -- lib/ PROVESFlightControllerReference/` empty |
| `check_capacity.py`, `check_packet_set.py`, `generate_rtm.py`, `req.py` | untouched | `git diff --stat 6abcaabd -- scripts/check_capacity.py scripts/check_packet_set.py scripts/generate_rtm.py scripts/req.py` empty |
| Existing gate stages' output | unchanged except V1–V3 | V4 transcript; 30 host binaries PASSED; 44 existing script tests pass |
| Gate runtime | +≤ 15 s | `time VERIFY_ENV=host scripts/verify.sh` before/after (both scripts run < 2 s) |
| Prose of every document | unchanged | diff under `docs-site/dev-loop/` is limited to `STATUS.md`, this plan dir, one new row in `README.md`'s table (STATUS.md's home) and the `cycles/README.md` row at close |
| Matrix | header counts + `DOCS` group + ten rows only | `git diff docs-site/requirements-matrix.md` |

## 6. Non-goals
Rewriting any document's prose; fixing orphans (WARN only); CI running `scripts/tests` (still a follow-up); the model repo
(`~/scalar`); checking `dev-loop-findings.md` hashes or `docs-site/components/*` copies (the `docs-sync` hook owns those);
moving the checkout out of iCloud (ROADMAP environment item; R1 is the alarm, not the cure); an mkdocs nav entry for STATUS.md.
