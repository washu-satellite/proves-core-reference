---
name: hw-verify
description: Execute one hardware verification procedure group (HP-nn) from docs-site/dev-loop/hardware-procedures against a connected board, record results per requirement with scripts/req.py, and regenerate the matrix. Use when a board, bench supply, or radio pair is connected and the user names a procedure group or tier.
---

# hw-verify — run one HP group and record evidence

Inputs you need before starting (ask once, all at once, if any is missing): the group ID (`HP-nn`), `UART_DEVICE` (`ls /dev/cu.usb*`), whether a second board is connected as LoRa receiver (`--radio`), the bench-supply device if the group is tier T2, and the operator's name for the evidence record.

## 1. Preflight
- Read only `docs-site/dev-loop/hardware-procedures/README.md` (mapping) and the one `HP-nn-*.md` file.
- Confirm the flashed image matches `git rev-parse --short HEAD`: the build artifacts live in the clean-path copy (`~/scalar-build/proves-core-reference/build-artifacts/`, see CLAUDE.md); if the copy is behind HEAD, rsync + `fprime-util build` there first. Flash by UF2 (BOOTSEL) or `make debug-install` equivalent via OpenOCD if a probe is present.
- Apply the group's preconditions exactly (SET_LEVEL, `telemetryDelay.DIVIDER`, parameters) and note the restore step; the group file is authoritative.
- Sequence-number sync first when the group uses authenticated commands: `pytest -m sync_sequence_number`.

## 2. Automated part
Run only the tests the group lists, from the repo root:
```
UART_DEVICE=<dev> fprime-venv/bin/python3 -m pytest PROVESFlightControllerReference/test/int -k "<test names from the HP file>" --junitxml=build-gtest/int-junit-HP-nn.xml -q [--radio]
```
Never run `uart_only`-marked tests over the radio link. If a test fails, record the failure verbatim; do not edit the test to pass.

## 3. Manual part
Walk the numbered steps with the operator. For each criterion row marked "manual", capture the evidence the row names (channel value with timestamp, event text, log file path). Keep a running `evidence.md` in `$TMPDIR` and copy it to `docs-site/dev-loop/hardware-procedures/results/HP-nn-<date>.md` at the end.

## 4. Record
For every requirement in the group's criteria table:
- Automated and passed: nothing to set; the matrix links the test. Regenerate with `--env desk` or `--env rig`: `fprime-venv/bin/python3 scripts/generate_rtm.py --junit build-gtest/junit.xml --env desk`.
- Manual, or automated but the RTM cannot see it: `fprime-venv/bin/python3 scripts/req.py set <ID> --status "Pass" --reason "HP-nn <date> <operator>: <evidence pointer>"` (or `--status "Fail" --reason "<what was observed>"`). `req.py` refuses a status while criteria are TBD; that is a stop, not something to work around.
- Restore the board (the group's restore step) before disconnecting.

## 5. Commit and report
`git add` the results file, the matrix, and any sdd/cdh.md rows `req.py` changed; commit `docs(verification): HP-nn results <date>`. Report: rows passed, rows failed with the observed value, rows blocked and why, and any wrong assumption discovered (which then follows the CLAUDE.md rule: ticket + note edits).
