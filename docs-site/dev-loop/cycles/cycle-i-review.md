# Cycle I review — documentation lint and generated STATUS.md (plan c873d9a5, tree main @ 6abcaabd)

**Verdict: Ready, with amendments 1–5.** Checked against the tree on 2026-09-20: the 14 conflict copies were extensionless
directories plus `Makefile 2` (all removed before this review; identical or empty); the `rtm` stage is last in `verify.sh`
(`:141`), so STATUS.md regeneration belongs there; ROADMAP row 4 cites fprime-zephyr `b14101dd`, so rule 4 scanning only the
`Landed` column of `cycles/README.md` is the right scope; the one unresolvable link on the tree sits inside a fenced block
in `cycle-h-plan/02-requirements.md`, so rule 3's code-block exemption is required, not optional.

## Deliberate deviations accepted
- Rule 5's baseline is the last commit on the record's introducing calendar day, so same-day fixes are allowed and the
  "never edited after the day it is written" rule in `decisions/README.md` is what the tool enforces.
- Rule 7 (orphans) is WARN only; rule 1 fires on every future iCloud sync until the checkout moves (environment item).
- STATUS.md uses `--dictionary none` and HEAD's commit date, so the file is deterministic and committable.

## Amendments (normative; override the plan where they differ)
1. **No test asserts the tree's current counts** ("Today: 14 links", "17 hashes", "5 records"). On the real tree a test may
   assert only: exit 0 and every rule line well-formed with status ≠ FAIL (rules 1–3, 6, 7) — and for rules 4–5, status ∈
   {OK, SKIP}. Counts are proven on synthetic trees only. Same reason as Cycle H amendment 1.
2. **Synthetic git repos only for rules 4–5.** Tests never call `git` against the real checkout except through the script's
   default run on the tree (amendment 1). Dates via `GIT_AUTHOR_DATE`/`GIT_COMMITTER_DATE` with an explicit timezone.
3. **Rule 2 consumer scope is closed:** `docs-site/**/*.md` outside `--decisions`, plus `--claude-md`. Nothing under
   `PROVESFlightControllerReference/` counts as a consumer (component sdds may cite decisions later; that is a rule change).
4. **`doc_status.py --check` in the `docs` stage must not run `check_docs.py` a second time** with different inputs than the
   stage's own run: the stage captures the lint stdout to `$BUILD/check-docs.out` and passes it as `--docs-output`. Otherwise
   a FAIL in the lint and a FAIL in the status check would be reported twice with possibly different text.
5. **Interface fixed by `01-normative.md`** §1.1, §1.3, §3, §4: flags, defaults, slugs, line grammar (U+2014), exit codes, the
   STATUS.md shape S1, the verify.sh strings. Coder mismatches are plan defects, not test edits.

## Numbers on the tree at 6abcaabd (reference, not test input)
rule 1: 0 after cleanup; rule 2: 5 records, 5 consumers; rule 3: 14 links; rule 4: 17 hashes; rule 5: 5 records at their
introducing commit; rule 6: 0; rule 7: 0 once `cycles/README.md` lists `cycle-i-plan/`.

## Tests (Stage 3b) — filled in after the test-author reports
Hashes of every new/changed file under `scripts/tests/` and `docs-site/requirements/tooling.md` pinned here before the coder starts.

## Gate — filled in at Stage 5
