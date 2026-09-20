# 03 — Advisory: how (the coder may deviate if every result in 01/02 still holds)

1. **Layout.** `check_docs.py`: one `Rule` dataclass (`id, slug, unit, count, status, reason, offenders`) and seven
   functions returning one; `main()` mirrors `check_capacity.py:675-696` (argparse → run → print → tally → exit). Reuse
   nothing from `check_capacity.py` by import (keep it untouched); share only the dash constant by copy.
2. **Markdown scanning (R2, R3, R7).** Strip fenced blocks (``` … ```) and inline code spans before link extraction; one
   regex `(?<!\\)!?\[[^\]]*\]\(([^)\s]+)\)`. Rule 7's "referenced" test is plain substring search over the four index texts.
3. **Git queries (R4, R5)** via `subprocess.run(["git", "-C", root, …])`: `cat-file -e <h>^{commit}`, `merge-base
   --is-ancestor <h> <head>`, `log --diff-filter=A --format=%H%x09%ad --date=format:%Y-%m-%d --follow -- <path>` (last line =
   intro; `--date=format` honours the commit's own zone), `log --format=%H%x09%ad … -- <path>` for the day filter,
   `show <commit>:<relpath>` for the baseline, `ls-files --error-unmatch` for tracked-ness. Compare after normalising `\r\n`.
4. **`doc_status.py`** builds the body as a list of lines and writes `"\n".join(lines) + "\n"`; `--check` reads the target,
   drops lines starting `Generated at HEAD` on both sides, compares lists, reports the first differing 1-based line.
   HEAD date: `git log -1 --format=%cd --date=format:%Y-%m-%d`.
5. **`verify.sh`** diff: `ALL_STAGES` string; `UT="${VERIFY_UT_DIR:-…}"`; configure line becomes
   `cmake -S "$UT" -B "$BUILD" > "$BUILD/configure.log" 2>&1` with a separate `else` branch that tails the log (`mkdir -p
   "$BUILD"` must follow the `rm -rf`); `docs` block modelled on the `audit` block (same sed indent, same rc handling);
   two lines appended to the `rtm` block. Wall time: both scripts ~0.1 s; the tests add one more nested gate run (~2 s, no build).
6. **Tests.** `scripts/tests/conftest.py` gains a `DocTree` builder (writes the synthetic dev-loop skeleton and an optional
   git repo with `env={"GIT_AUTHOR_DATE": …, "GIT_COMMITTER_DATE": …}` commits) and `run_docs()` / `run_status()` runners in
   the style of `run_capacity()`; `test_verify_sh.py` keeps its module-level skip guard; new cases claim DOCS-9/10 only.
7. **Order (one commit per step, gate before each).** (a) rows + tests (DOCS-1 seed, adds, tests red at "No such file");
   (b) `check_docs.py`; (c) `doc_status.py` + `DL/README.md` row for `STATUS.md`; (d) `verify.sh` + first generated
   `STATUS.md`; (e) delete the 14 conflict copies (no commit; environment) and run the full gate for the review.
8. **Open risks.** R1 will re-fire whenever iCloud syncs mid-edit — intended, but noisy until the checkout moves; R5's
   `--follow` may mis-attribute a renamed record (records are never renamed by rule); `git diff --quiet` in V2 needs the
   file tracked (first run prints `regenerated`); the matrix count line embeds `environment: host`, so a `desk` gate run
   makes STATUS.md differ — accepted (the gate owner runs host; document in STATUS header if it bites).
