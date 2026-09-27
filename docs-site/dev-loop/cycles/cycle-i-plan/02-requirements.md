# 02 — Requirement rows `DOCS-1..10` (Unit / Unit Test)

`req.py add` still cannot create a group (`scripts/req.py:125-126` returns `None` for a header-only table; `cmd_add`
`:271-278` exits "group not found"), so the new group `Verification Tooling (DOCS)` is seeded by hand in the existing
`docs-site/requirements/tooling.md` with its heading, `req.py`'s header/separator and the first row only; DOCS-2..10 go in
with the tool. `mkdocs.yml:116` already lists `tooling.md`; nothing to add there. Criteria may not contain a bare `|`.

## Step 1 — seed (append to `docs-site/requirements/tooling.md`, after the AUDIT table)
```markdown
## Verification Tooling (DOCS)

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|DOCS-1|check_docs.py fails on iCloud conflict copies (files or directories) anywhere under the tree outside the pruned dirs|Unit Test|Unit|Line `DOCS-1 conflict-copies: N conflict copies — S`; on a --root tree a file `README 2.md` or a directory `docs 2` gives FAIL, exit 1 and an indented offender line; the same names under lib/ or build-gtest/ give 0 and OK; on the current tree the line is well-formed and the RESULT line is last|||
```
Check: `$PY scripts/req.py show DOCS-1` prints the row with group `Verification Tooling (DOCS)`.

## Step 2 — the remaining rows, verbatim (`G="Verification Tooling (DOCS)"`, all `--method "Unit Test" --level Unit`)
```
$PY scripts/req.py add --group "$G" --id DOCS-2 --description "check_docs.py checks D-record filenames, unique IDs, README listing, at least one consumer link outside decisions/, and that every [D-nnn](...) link resolves to that record" --criteria "Line DOCS-2 decision-records: N decision records — S; a synthetic tree with a two-digit ID, a duplicate ID, a record missing from decisions/README.md, a record with no consumer link, or a [D-nnn](...) link to a missing or differently numbered file gives FAIL and exit 1 naming the file; a tree where every record is listed and linked from ROADMAP.md gives OK"
$PY scripts/req.py add --group "$G" --id DOCS-3 --description "check_docs.py resolves every relative Markdown link in docs-site/dev-loop/** and CLAUDE.md, ignoring anchors, URLs and links inside fenced or inline code" --criteria "Line DOCS-3 links: N links — S; a link to a missing file gives FAIL and exit 1 with the file and target; the same link inside a fenced code block is not counted; a link to an existing directory or with an #anchor is OK; on the current tree the line is well-formed and not FAIL"
$PY scripts/req.py add --group "$G" --id DOCS-4 --description "check_docs.py checks that every commit hash in the Landed column of cycles/README.md is an ancestor of HEAD, and only that column" --criteria "Line DOCS-4 landed-hashes: N landed hashes — S; in a synthetic git repo a Landed hash on an unmerged branch or a non-existent hash gives FAIL and exit 1; a hash on an unmerged branch cited in ROADMAP.md or a D-record does not fail; --git none gives SKIP and exit 0"
$PY scripts/req.py add --group "$G" --id DOCS-5 --description "check_docs.py fails when a D-record differs from its content at the end of its introducing day, except one prepended Superseded-by line" --criteria "Line DOCS-5 append-only: N decision records — S; in a synthetic repo a record edited in a commit dated a later day, or edited in the working tree, gives FAIL and exit 1; a same-day second commit is OK; a later commit that only prepends a line matching Superseded by D-nnn is OK; that plus any other change is FAIL; an untracked record is OK; --git none gives SKIP"
$PY scripts/req.py add --group "$G" --id DOCS-6 --description "check_docs.py fails when ROADMAP.md contains a typed State: line or a **DONE table row" --criteria "Line DOCS-6 typed-state: N typed-state lines — S; a --roadmap copy with a line starting State: or a table row containing **DONE gives FAIL and exit 1 with the line; the word state in prose is OK; the current ROADMAP.md gives 0 and OK"
$PY scripts/req.py add --group "$G" --id DOCS-7 --description "check_docs.py warns, never fails, on dev-loop files referenced from no index" --criteria "Line DOCS-7 orphans: N orphans — S; a synthetic design/stray.md mentioned in no index gives WARN, one indented path line and exit 0; a file referenced by basename, stem, ID prefix (HP-07) or ancestor directory (cycle-h-plan/) is not an orphan; RESULT counts it as warn"
$PY scripts/req.py add --group "$G" --id DOCS-8 --description "doc_status.py writes docs-site/dev-loop/STATUS.md from generators only, deterministically, and --check detects a hand edit" --criteria "With --cycles-index, --matrix, --capacity-output and --docs-output pointing at fixture files the written file equals the S1 text byte for byte with HEAD from --git or unknown; --check exits 0 on that file, 1 with STATUS.md: stale after any body change, 0 after changing only the Generated at HEAD line, 1 with STATUS.md: missing when absent; a missing matrix count line exits 2"
$PY scripts/req.py add --group "$G" --id DOCS-9 --description "verify.sh runs a docs stage (lint plus STATUS.md --check) before pre-commit and regenerates STATUS.md in the rtm stage" --criteria "With VERIFY_STAGES=docs the output has the header == documentation lint and every line of check_docs.py indented, then the doc_status.py --check line; a check_docs exit 1 lists documentation lint under unverified; ALL_STAGES contains docs between script-tests and pre-commit; the three existing AUDIT-9 tests pass unchanged"
$PY scripts/req.py add --group "$G" --id DOCS-10 --description "verify.sh prints the tail of the cmake configure log when the host configure fails instead of hiding it" --criteria "With VERIFY_STAGES=host-tests VERIFY_UT_DIR=<tmp> holding a CMakeLists.txt whose configure step calls message(FATAL_ERROR synthetic configure failure): exit 1, stdout contains host configure FAILED and synthetic configure failure, <tmp build>/configure.log exists, unverified lists host unit tests"
```
After the adds `$PY scripts/req.py list --group "Verification Tooling (DOCS)"` shows ten rows. Tests: `scripts/tests/
test_check_docs.py` claims DOCS-1..7, `test_doc_status.py` DOCS-8, `test_verify_sh.py` (extended) DOCS-9..10. Per Cycle H
amendment 1, no test asserts the tree's counts; per amendment 2, no test reads `~/scalar-build` (`--dictionary none`).
