# 04 — Findings (verified 2026-09-20 at 6abcaabd; merge into the ledger)

1. **Conflict copies today: 14, none matching `* 2.*`.** `Makefile 2` (untracked file, 23 KB) plus 13 empty directories:
   `docs-site/dev-loop/{cycles,design,hardware-procedures} 2`, four under `test/unit-tests/support/`, six `Components/*/docs 2`.
   The brief's glob `* 2.*` requires a dot and would report 0. `git ls-files | grep ' [0-9]'` is empty: no tracked name has a
   space-digit, so the broad regex has no false positives today. (Brief claim "127" was the 2026-09-20 event, ledger `:232`; those were removed.)
2. **`verify.sh` hides configure output**: `scripts/verify.sh:63` `cmake -S "$UT" -B "$BUILD" >/dev/null 2>&1`; on failure only
   `build.log` is grepped (`:68`), which does not exist when configure fails. Brief claim confirmed; ledger `:232` lists it as a follow-up.
3. **Stage order**: `ALL_STAGES="host-tests,int-collect,audit,script-tests,pre-commit,rtm"` (`verify.sh:37`); `rtm` is last
   (`:141-148`), so a STATUS page that embeds the matrix count line cannot be generated "after audit and before pre-commit"
   as the brief puts it — hence V1/V2 split lint+check (docs stage) from regeneration (rtm stage). `git` is already used by the
   gate (`:135`); git 2.55.0, Python 3.13.5 in `fprime-venv`.
4. **Matrix header count line** is `generate_rtm.py:437-440`: `**N** requirements &middot; **N** linked to automated tests
   &middot; **N** verified by passing unit tests in this build &middot; **N** deferred to hardware (environment: host, …)`,
   line 15 of the matrix today (322 / 140 / 84 / 56). No `--sha` is passed by the gate, so no commit stamp appears (`:420`).
5. **`check_capacity.py` prints 12 lines** with `--dictionary none`: one `dictionary:` line, ten constant lines (three
   rate-group instances), `RESULT:`; 0.04 s. Brief's "ten lines" = the constant lines.
6. **D-records**: five files, all added in 6abcaabd, unchanged since; names already fit `D-\d{3}-slug.md`; every one is linked
   from `ROADMAP.md` (D-001..005) and `cycles/README.md` links D-002/D-003; `decisions/README.md` links all five. D-002 cites
   26041de4/6a1589b2 and D-003 cites upstream a477893b (all ancestors); `ROADMAP.md:31` cites `b14101dd`, a fprime-zephyr commit
   that is **not** in this repo — why R4 scans only the Landed column.
7. **Landed column**: 17 hashes (7–8 hex), all ancestors of HEAD (`merge-base --is-ancestor`); Cycle C says `see review`.
8. **Links**: 15 relative links in `dev-loop/**` + `CLAUDE.md`; 14 resolve; the one "broken" (`../requirements-matrix.md` in
   `cycle-h-plan/02-requirements.md`) is inside a fenced code block — R3 must skip code, or the current tree fails.
9. **Indexes reference by name, not link**: `cycles/README.md` uses backticks (`cycle-h-plan/`), `hardware-procedures/README.md`
   uses IDs (`HP-07`, 63 mentions, never a filename), `dev-loop/README.md` uses `stored-data/`. R7's "referenced" definition
   follows from this; with it, 0 orphans today among 77 `.md` files. `ROADMAP.md` has no `State:` line and no `**DONE` row.
10. **Requirements**: `tooling.md` holds group `Verification Tooling (AUDIT)` (AUDIT-1..9); `req.py add` cannot seed a group
    (`req.py:125-126`, `:271-278`, ledger `:171`); `mkdocs.yml:116` lists the file. Existing script tests: 44 cases in
    `scripts/tests/` (41 + 3 verify.sh); `test_verify_sh.py:43-66` (`gate_env`, `run_gate`) shows the env/knob pattern to extend.
11. **Pre-commit** runs `trailing-whitespace`, `end-of-file-fixer`, `codespell` on any `.md` (`.pre-commit-config.yaml:5-15`);
    `STATUS.md` must end with one newline and carry no trailing spaces (S1). `docs-sync` (`:53-59`) only touches `docs-site/components/`.
