# Bench Safety Requirements

Requirements on the tooling that keeps a bench board from executing flight-only actions: the generated bench startup
sequence and the check that keeps it in step with `sequences/startup.seq`. Rows are added and edited only with
`scripts/req.py` (`req.py add --group "Bench Safety (BENCH)" ...`). Tests live in `scripts/tests/` and link to these IDs
with `@pytest.mark.verifies("BENCH-n")`.

## Bench Safety (BENCH)

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|BENCH-1|make_bench_sequence.py derives the bench startup sequence from startup.seq by removing only the deploy, transmit-enable, safe-mode-exit and detumble-mode commands|Unit Test|Unit|Output = the two fixed header lines then every source line byte for byte except command lines for antennaDeployer.DEPLOY, modeManager.EXIT_SAFE_MODE, detumbleManager.SET_MODE (any argument) and lora.TRANSMIT with first argument ENABLED, and a comment line immediately before a dropped line; TRANSMIT, DISABLED and blank lines inside the sequence are kept; blank lines at the end of the output are dropped so the file ends in exactly one newline; --stdout prints the same text and writes nothing; a missing --source exits 2 with one stderr line|||
|BENCH-2|make_bench_sequence.py --check fails the gate when sequences/bench_startup.seq is missing or differs from the regeneration|Unit Test|Unit|With --check: exit 0 when --output equals the generated text byte for byte; exit 1 with the line bench_startup.seq: stale after any byte differs; exit 1 with bench_startup.seq: missing when absent; nothing is written in --check mode; on the current tree --check exits 0|||
