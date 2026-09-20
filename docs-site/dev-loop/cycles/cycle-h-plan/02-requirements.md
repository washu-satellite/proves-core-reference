# 02 — Requirement rows `AUDIT-1..9` (Unit / Unit Test)

There is no verification-tooling group today. `req.py add` cannot create a group: `parse_section_table` returns
`None` for a heading with no table rows (`scripts/req.py:125-126`), and `cmd_add` then reports "group not found"
(`:273-278`). So the group is seeded once by hand with its header and **first row only**, in the tool's exact column
format; every further row goes in with the tool. Both `req.py` (`:134-141`) and `generate_rtm.py` (`SECTION_RE`, `:58`)
take the group name from the `## ` heading of a file in `docs-site/requirements/`.

## Step 1 — seed file (the one hand edit, done by the test-author before any test claims an ID)

Create `docs-site/requirements/tooling.md` with exactly this content (the table header and separator are
`req.py`'s `HEADER`/`SEPARATOR`, `scripts/req.py:39-40`):

```markdown
# Verification Tooling Requirements

Requirements on the host-side gate itself (`scripts/verify.sh`, `scripts/check_capacity.py`): what the tooling must
print, when it must fail, and what it must leave unchanged. Rows are added and edited only with `scripts/req.py`
(`req.py add --group "Verification Tooling (AUDIT)" ...`) and appear in the
[Requirements Matrix](../requirements-matrix.md). Tests live in `scripts/tests/` and link to these IDs with
`@pytest.mark.verifies("AUDIT-n")`.

## Verification Tooling (AUDIT)

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|AUDIT-1|check_capacity.py checks the packet count against MAX_PACKETIZER_PACKETS|Unit Test|Unit|Line `MAX_PACKETIZER_PACKETS: U/L used, F free — S`; a --packets copy with L+1 packet blocks gives FAIL and exit 1; L blocks give exit 0; the current tree prints 23/24 WARN and exit 0|||
```

Add one line to `mkdocs.yml` under `- CDH System Requirements: requirements/cdh.md` (line 115):
`      - Verification Tooling Requirements: requirements/tooling.md`.

Then check the seed parsed: `$PY scripts/req.py show AUDIT-1` prints the row with group `Verification Tooling (AUDIT)`.

## Step 2 — the remaining rows, verbatim

```
$PY scripts/req.py add --group "Verification Tooling (AUDIT)" --id AUDIT-2 --method "Unit Test" --level Unit \
  --description "check_capacity.py checks distinct channels (packets and omit block) against MAX_PACKETIZER_CHANNELS" \
  --criteria "Line MAX_PACKETIZER_CHANNELS: U/L used, F free (n in packets + m omitted) — S; a --packets copy naming L+1 distinct channels gives FAIL and exit 1; WARN at U >= 0.9 L; the current tree prints 244/256 (174 in packets + 70 omitted) WARN and exit 0; scripts/check_packet_set.py run standalone still prints its six current lines byte-for-byte with exit 0"

$PY scripts/req.py add --group "Verification Tooling (AUDIT)" --id AUDIT-3 --method "Unit Test" --level Unit \
  --description "check_capacity.py checks the dictionary's distinct command opcodes against CMD_DISPATCHER_DISPATCH_TABLE_SIZE, or reports SKIP without a dictionary" \
  --criteria "Line CMD_DISPATCHER_DISPATCH_TABLE_SIZE: U/L used, F free — S with U = distinct commands[*].opcode in --dictionary and L from --cmd-config; L+1 opcodes give FAIL and exit 1, L give OK; --dictionary none gives ?/L used — SKIP and exit 0; a dictionary whose metadata.projectVersion -g<hash> is not the tree HEAD adds one line starting 'WARN: dictionary projectVersion'; a --dictionary path that is missing or lacks metadata/commands/parameters exits 2; the build copy's dictionary on the current tree prints 387/512 OK"

$PY scripts/req.py add --group "Verification Tooling (AUDIT)" --id AUDIT-4 --method "Unit Test" --level Unit \
  --description "check_capacity.py reports PRMDB_NUM_DB_ENTRIES headroom against the dictionary's parameter count as INFO, never FAIL" \
  --criteria "Line PRMDB_NUM_DB_ENTRIES: U/L used, F free (project override|lib default) — S with U = entries in parameters and L from --prmdb-config, which defaults to project/config/PrmDbImplCfg.hpp when present else lib/fprime/default/config/PrmDbImplCfg.hpp; U > L gives INFO and exit 0 (never FAIL); U <= L gives OK; --dictionary none gives SKIP; the current tree prints 106/25 (lib default) INFO"

$PY scripts/req.py add --group "Verification Tooling (AUDIT)" --id AUDIT-5 --method "Unit Test" --level Unit \
  --description "check_capacity.py checks every rate group's RateGroupMemberOut slot usage in topology.fpp against ActiveRateGroupOutputPorts" \
  --criteria "One line ActiveRateGroupOutputPorts[X]: U/L used, F free (max index k) — S per instance X connected as X.RateGroupMemberOut[i] in --topology, L from --ac-constants; a topology copy connecting rateGroup1Hz.RateGroupMemberOut[L] gives FAIL and exit 1; a copy connecting the same output index twice gives FAIL; the current tree prints rateGroup50Hz 2/25, rateGroup10Hz 14/25, rateGroup1Hz 20/25 (max index 20), all OK"

$PY scripts/req.py add --group "Verification Tooling (AUDIT)" --id AUDIT-6 --method "Unit Test" --level Unit \
  --description "check_capacity.py checks faultManager.faultIn slot usage against FaultInPorts" \
  --criteria "Line FaultInPorts: U/L used, F free (max index k) — S with U = distinct faultManager.faultIn[i] indices in --topology and L = FaultInPorts in --fault-types; a topology copy connecting faultIn[L] gives FAIL and exit 1; the current tree prints 3/4 (max index 3) OK"

$PY scripts/req.py add --group "Verification Tooling (AUDIT)" --id AUDIT-7 --method "Unit Test" --level Unit \
  --description "check_capacity.py checks TaskGate schedIn/schedOut slot usage and SchedTask ordinals against NUM_TASKS, resolving symbolic indices from the enum" \
  --criteria "Line NUM_TASKS: U/L used, F free (n SchedTask enumerators, max index k) — S with U = distinct indices on taskGate.schedIn and schedOut, an index Components.SchedTask.NAME resolved to its value in --task-gate; a topology copy connecting schedIn[L] gives FAIL and exit 1; a --task-gate copy with a sixth enumerator = 5 and NUM_TASKS = 5 gives FAIL; a topology copy using an unknown enumerator exits 2; the current tree prints 5/5 (5 SchedTask enumerators, max index 4) OK"

$PY scripts/req.py add --group "Verification Tooling (AUDIT)" --id AUDIT-8 --method "Unit Test" --level Unit \
  --description "check_capacity.py checks the highest FaultType enumerator against MAX_FAULT_TYPE and the AUTHORITY_MASK bit width" \
  --criteria "Line MAX_FAULT_TYPE: U/L used, F free (AUTHORITY_MASK T = b bits) — S with U = highest FaultType value in --fault-types, L = MAX_FAULT_TYPE in --fault-table, b from the param type in --fault-manager; a --fault-types copy adding an enumerator = L+1 gives FAIL and exit 1; a --fault-table copy with MAX_FAULT_TYPE = 9 against a U8 mask gives FAIL; F == 0 gives WARN; the current tree prints 8/8 (AUTHORITY_MASK U8 = 8 bits) WARN and exit 0"

$PY scripts/req.py add --group "Verification Tooling (AUDIT)" --id AUDIT-9 --method "Unit Test" --level Unit \
  --description "verify.sh starts the host build from an empty build directory, runs the capacity audit and the script tests, and labels partial runs" \
  --criteria "With VERIFY_ENV=host VERIFY_STAGES=host-tests VERIFY_BUILD_DIR=<tmp>: a file <tmp>/canary present before the run is absent after it, the run lists 30 test_* binaries PASSED, exits 0 and prints 'stages: host-tests (partial run — not a gate result)'; with VERIFY_STAGES=audit the output contains '== capacity audit' and every line of check_capacity.py; a nested run with VERIFY_NESTED=1 and no VERIFY_STAGES exits 2; the full default run on the current tree prints result: PASS with unverified (none)"
```

After the adds: `$PY scripts/req.py list --group "Verification Tooling"` shows nine rows; the RTM is regenerated by
`verify.sh` (or `$PY scripts/generate_rtm.py --junit build-gtest/junit.xml --env host`), never by `make rtm` at this path.

## What the tests claim
`scripts/tests/test_check_capacity.py` claims AUDIT-1..8 (one test function per bullet in a row's criteria is the
expected shape); `scripts/tests/test_verify_sh.py` claims AUDIT-9. A test claims an ID only if its assertions are the
row's criteria; the "current tree" assertions must be written against 6b22f72d's numbers above and updated by the
cycle that changes them (they are the same numbers `04-findings.md` records).
