# 01 — Normative: results the tests are written from

Everything here is observable from a command line: arguments, exit codes, stdout lines. Nothing here says how the
script parses anything (that is `03-advisory.md`). `$PY` = `fprime-venv/bin/python3`, run from `$R`.

## 1. Script contract — `scripts/check_capacity.py`

### 1.1 Command line
```
$PY scripts/check_capacity.py [--packets PATH] [--packetizer-config PATH] [--cmd-config PATH]
    [--prmdb-config PATH] [--ac-constants PATH] [--topology PATH] [--task-gate PATH]
    [--fault-types PATH] [--fault-table PATH] [--fault-manager PATH]
    [--dictionary PATH|auto|none] [--build-copy DIR] [--warn-fraction F]
```
Defaults (all relative to `$R`; `P` = `PROVESFlightControllerReference`):

| Flag | Default | Read for |
|---|---|---|
| `--packets` | `P/ReferenceDeployment/Top/ReferenceDeploymentPackets.fppi` | packet count, distinct channels |
| `--packetizer-config` | `P/project/config/TlmPacketizerCfg.hpp` | `MAX_PACKETIZER_PACKETS`, `MAX_PACKETIZER_CHANNELS` (or `TLMPACKETIZER_HASH_BUCKETS`) |
| `--cmd-config` | `P/project/config/CommandDispatcherImplCfg.hpp` | `CMD_DISPATCHER_DISPATCH_TABLE_SIZE` |
| `--prmdb-config` | `P/project/config/PrmDbImplCfg.hpp` **if it exists, else** `lib/fprime/default/config/PrmDbImplCfg.hpp` | `PRMDB_NUM_DB_ENTRIES` |
| `--ac-constants` | `P/project/config/AcConstants.fpp` | `ActiveRateGroupOutputPorts` |
| `--topology` | `P/ReferenceDeployment/Top/topology.fpp` | `*.RateGroupMemberOut[i]`, `taskGate.schedIn[i]`, `taskGate.schedOut[i]`, `faultManager.faultIn[i]` connections |
| `--task-gate` | `P/Components/TaskGate/TaskGate.fpp` | `NUM_TASKS`, enum `SchedTask` |
| `--fault-types` | `P/Components/FaultTypes/FaultTypes.fpp` | `FaultInPorts`, enum `FaultType` |
| `--fault-table` | `P/Components/FaultManager/FaultTable.hpp` | `MAX_FAULT_TYPE` |
| `--fault-manager` | `P/Components/FaultManager/FaultManager.fpp` | type of `param AUTHORITY_MASK` (`U8` = 8 bits, `U16` = 16, `U32` = 32) |
| `--dictionary` | `auto` | `metadata.projectVersion`, `commands[*].opcode`, `parameters[*].id` — the only keys read |
| `--build-copy` | `$SCALAR_BUILD_COPY` if set, else `~/scalar-build/proves-core-reference` | second search root for `auto` |
| `--warn-fraction` | `0.9` | WARN threshold for the boot-assert constants (§1.4) |

`--dictionary auto`: the newest-by-mtime file matching `build-artifacts/**/*TopologyDictionary.json` or
`build-fprime-automatic-zephyr*/**/*TopologyDictionary.json`, searched first under `$R`, then under `--build-copy`.
`--dictionary none`: no dictionary; the two dictionary-backed lines are `SKIP`. `--dictionary PATH`: that file.

### 1.2 Exit codes
- `0` — no line is `FAIL` (any mix of `OK`, `WARN`, `INFO`, `SKIP`).
- `1` — at least one line is `FAIL`.
- `2` — an input could not be used: a path given on the command line (or a default) does not exist or cannot be parsed;
  a named constant is not found in its file or is not an integer literal; a connection index is symbolic and cannot
  be resolved from the enums in `--task-gate` / `--fault-types`; `--dictionary PATH` is missing, not JSON, or lacks
  one of the three keys. One `error: <what> (<file>)` line on stderr; stdout may be partial. `auto` finding nothing
  is **not** an error (it is `SKIP`).

### 1.3 Output grammar (stdout, in this order)
1. Zero or more `source:` lines are allowed before the constant lines but tests do not rely on them, except:
   `dictionary: <path> (projectVersion <v>)` when a dictionary is used, or `dictionary: none (<reason>)` when not.
2. When a dictionary is used and the `-g<hash>` in `metadata.projectVersion` is neither a prefix nor an extension of
   `git rev-parse HEAD` of `$R` (or the field has no `-g<hash>`): one line
   `WARN: dictionary projectVersion <v> does not match tree HEAD <hash>; dictionary-based lines may be stale`.
3. One line per constant, exactly one of:
   ```
   <NAME>: <USED>/<LIMIT> used, <FREE> free[ (<DETAIL>)] — <STATUS>[: <REASON>]
   <NAME>: ?/<LIMIT> used — SKIP: <REASON>
   ```
   `NAME` is the constant's C++/fpp identifier, suffixed `[<instance>]` for per-instance lines. `USED`, `LIMIT`,
   `FREE` are decimal integers, `FREE = max(0, LIMIT − USED)`. `STATUS ∈ {OK, WARN, INFO, FAIL, SKIP}`. The em dash is
   U+2014 with a space on each side. Line order: `MAX_PACKETIZER_PACKETS`, `MAX_PACKETIZER_CHANNELS`,
   `CMD_DISPATCHER_DISPATCH_TABLE_SIZE`, `PRMDB_NUM_DB_ENTRIES`, `ActiveRateGroupOutputPorts[<instance>]` (instances in
   the order first seen in the topology), `FaultInPorts`, `NUM_TASKS`, `MAX_FAULT_TYPE`.
4. Last line: `RESULT: OK — <a> ok, <b> warn, <c> info, <d> skip, <e> fail` or `RESULT: FAIL — ...` (FAIL iff `e > 0`).

The channel-limit constant name printed is whichever the config defines, as `check_packet_set.py` does today
(`MAX_PACKETIZER_CHANNELS` preferred over `TLMPACKETIZER_HASH_BUCKETS`).

### 1.4 Status rules
| Class | Constants | FAIL when | WARN when | INFO when |
|---|---|---|---|---|
| boot assert | packets, channels, dispatch table | `USED > LIMIT` | `USED ≥ warn-fraction × LIMIT` | never |
| bench constraint | `PRMDB_NUM_DB_ENTRIES` | never | never | `USED > LIMIT` |
| fpp-loud array | rate groups, `FaultInPorts`, `NUM_TASKS` | any connected index `≥ LIMIT`; for `RateGroupMemberOut` also the same output index connected twice; for `NUM_TASKS` also any `SchedTask` ordinal `≥ LIMIT` | never | never |
| silent mask | `MAX_FAULT_TYPE` | highest `FaultType` enumerator value `> MAX_FAULT_TYPE`, or `MAX_FAULT_TYPE >` bit width of `AUTHORITY_MASK` | `FREE == 0` | never |

## 2. Per-constant results (R1–R8) — each is one requirement row in `02-requirements.md`

Each result names (a) what `USED` and `LIMIT` are, (b) the synthetic overflow a test builds from a doctored copy, and
(c) the line the current tree at 6b22f72d prints. "Doctored copy" = the default file copied to a temp path and edited;
the tree is never modified.

- **R1 `MAX_PACKETIZER_PACKETS`.** USED = `packet` blocks in `--packets`; LIMIT from `--packetizer-config`. Overflow: a
  copy with LIMIT+1 packet blocks → `FAIL: ... TlmPacketizer::setPacketList asserts at boot`, exit 1. Current tree:
  `MAX_PACKETIZER_PACKETS: 23/24 used, 1 free — WARN` (23 ≥ 0.9 × 24).
- **R2 `MAX_PACKETIZER_CHANNELS`.** USED = distinct channel names across all packet blocks and the `omit` block (same
  qualification rule as `check_packet_set.py`); DETAIL `<n> in packets + <m> omitted`. Overflow: a copy naming LIMIT+1
  distinct channels → FAIL, exit 1. Current tree: `MAX_PACKETIZER_CHANNELS: 244/256 used, 12 free (174 in packets + 70
  omitted) — WARN`. `check_packet_set.py` run standalone still prints its six current lines byte-for-byte (see §4).
- **R3 `CMD_DISPATCHER_DISPATCH_TABLE_SIZE`.** USED = distinct `opcode` values in the dictionary's `commands`; LIMIT from
  `--cmd-config`. Overflow: a synthetic dictionary with LIMIT+1 distinct opcodes → FAIL (`CommandDispatcherImpl
  compCmdReg_handler asserts at boot`), exit 1; LIMIT opcodes → OK. `--dictionary none` →
  `CMD_DISPATCHER_DISPATCH_TABLE_SIZE: ?/512 used — SKIP: no dictionary (target build needed)`, exit 0. A dictionary whose
  `projectVersion` hash is not HEAD → the `WARN: dictionary projectVersion` line, status of the constant unchanged.
  Current tree with the build copy's dictionary: `387/512 used, 125 free — OK`.
- **R4 `PRMDB_NUM_DB_ENTRIES`.** USED = entries in the dictionary's `parameters`; LIMIT from `--prmdb-config`, and the
  line's DETAIL says `project override` or `lib default`. This is **not** a boot overflow (`PrmDbImpl.cpp:96-113`: the
  26th distinct saved id is dropped with `PrmDbFull`; `readParamFile` returns ERROR on a dropped record, `:590-592`), so
  the line never FAILs: USED > LIMIT → `INFO: at most <LIMIT> parameters can be saved; save only the parameters under
  test (HP-07)`, exit 0; USED ≤ LIMIT → OK; no dictionary → SKIP. Current tree: `PRMDB_NUM_DB_ENTRIES: 106/25 used, 0
  free (lib default) — INFO: ...`. Deviation from the roadmap's "fails on each": deliberate, stated here.
- **R5 `ActiveRateGroupOutputPorts`.** One line per instance `X` that appears as `X.RateGroupMemberOut[i]` in
  `--topology`; USED = number of connections from that array; DETAIL `max index <k>`; LIMIT from `--ac-constants`.
  Overflow: a topology copy connecting `rateGroup1Hz.RateGroupMemberOut[25]` → FAIL, exit 1; a copy connecting
  `RateGroupMemberOut[3]` twice → FAIL (`duplicate output index 3`). Current tree: `[rateGroup50Hz]: 2/25 used, 23 free
  (max index 1) — OK`, `[rateGroup10Hz]: 14/25 used, 11 free (max index 13) — OK`, `[rateGroup1Hz]: 20/25 used, 5 free
  (max index 20) — OK`.
- **R6 `FaultInPorts`.** USED = distinct `faultManager.faultIn[i]` indices; LIMIT from `--fault-types`. Overflow: a
  topology copy connecting `faultIn[4]` → FAIL. Current tree: `FaultInPorts: 3/4 used, 1 free (max index 3) — OK`.
- **R7 `NUM_TASKS`.** USED = distinct indices connected on `taskGate.schedIn[...]` ∪ `taskGate.schedOut[...]`, where an
  index written as `Components.SchedTask.<NAME>` (or `SchedTask.<NAME>`) is the enumerator's value in `--task-gate`;
  LIMIT = `NUM_TASKS` there; DETAIL `<n> SchedTask enumerators, max index <k>`. Overflow: a topology copy connecting
  `schedIn[5]` → FAIL; a `--task-gate` copy adding a sixth enumerator `= 5` with `NUM_TASKS = 5` → FAIL; a topology copy
  using `Components.SchedTask.NOPE` → exit 2. Current tree: `NUM_TASKS: 5/5 used, 0 free (5 SchedTask enumerators, max
  index 4) — OK`.
- **R8 `MAX_FAULT_TYPE`.** USED = highest `FaultType` enumerator value in `--fault-types`; LIMIT = `MAX_FAULT_TYPE` in
  `--fault-table`; DETAIL `AUTHORITY_MASK <type> = <bits> bits` from `--fault-manager`. Overflow: a `--fault-types` copy
  adding `PAYLOAD_LINK_LOST = 9` → FAIL (`silent: FaultTable::bit() returns 0 for this type`); a `--fault-table` copy with
  `MAX_FAULT_TYPE = 9` against a `U8` mask → FAIL. Current tree: `MAX_FAULT_TYPE: 8/8 used, 0 free (AUTHORITY_MASK U8 = 8
  bits) — WARN: the next FaultType needs a wider mask`.
- **R9 whole run on the current tree** (ten lines: eight constants, rate groups counted three times). With defaults
  and the build copy's dictionary present: exit 0 and `RESULT: OK — 6 ok, 3 warn, 1 info, 0 skip, 0 fail` (OK: dispatch,
  three rate groups, `FaultInPorts`, `NUM_TASKS`; WARN: packets, channels, `MAX_FAULT_TYPE`; INFO: PrmDb). With
  `--dictionary none`: exit 0 and `RESULT: OK — 5 ok, 3 warn, 0 info, 2 skip, 0 fail`. Runtime < 2 s either way.

## 3. `verify.sh` results (V1–V6) — requirement row AUDIT-9

- **V1 clean host build.** The host-test stage deletes the build directory before `cmake -S ... -B`. Observable: a
  file created at `<build dir>/canary` before the run is absent after it, and the run still lists the 30 `test_*`
  binaries with `PASSED`.
- **V2 knobs.** `VERIFY_BUILD_DIR` (default `build-gtest`) is the build directory for every stage that used
  `build-gtest` (host build, junit, logs, RTM inputs). `VERIFY_STAGES` (default: all) is a comma list drawn from
  `host-tests,int-collect,audit,script-tests,pre-commit,rtm`; stages not listed are skipped and the summary prints
  `stages: <list> (partial run — not a gate result)` before `result:`. With both unset the output sections and
  wording are today's, except V3–V5.
- **V3 audit stage** replaces `== packet set vs TlmPacketizer config`: header `== capacity audit
  (scripts/check_capacity.py)`, the script's stdout indented by two spaces, exit 1 → `fail=1` and
  `unverified+=("capacity audit")`, exit 2 → the same with `unverified+=("capacity audit: input error")`; every `SKIP`
  line → `deferred+=("capacity audit: <NAME> (needs a target-build dictionary)")`.
- **V4 script-tests stage** (after host-tests, before pre-commit): `"$PY" -m pytest scripts/tests -q
  --junitxml="$VERIFY_BUILD_DIR/scripts-junit.xml"`; a non-zero exit → `fail=1`, `unverified+=("script tests")`; it
  exports `VERIFY_NESTED=1` so a test that runs `verify.sh` itself runs it with `VERIFY_STAGES=host-tests` and a
  temporary `VERIFY_BUILD_DIR` and never recurses further (a nested run with `VERIFY_NESTED` already set and no
  `VERIFY_STAGES` exits 2 with `nested verify.sh without VERIFY_STAGES`).
- **V5 RTM stage** passes `--script-junit "$VERIFY_BUILD_DIR/scripts-junit.xml"` to `generate_rtm.py`; with all
  script tests green the matrix header reads 322 requirements, 140 linked, 84 passing on host (today 313 / 131 / 75).
- **V6 gate on the current tree.** `VERIFY_ENV=host scripts/verify.sh` → `result: PASS`, `unverified: (none)`,
  deferred = today's two lines (board tests, Zephyr build) plus, when no dictionary is found, the two audit SKIP lines.

## 4. Harm table (measurable)
| Existing behaviour | After this row | Proof |
|---|---|---|
| Firmware, `.fpp`, `project/config`, `lib/` | untouched | `git diff --stat 6b22f72d -- lib/ PROVESFlightControllerReference/` empty |
| `check_packet_set.py` standalone | byte-identical six lines, exit 0 | test runs it and compares to the lines recorded in `04-findings.md` §13 |
| `verify.sh` sections and summary wording | unchanged except V3–V5 | V6 transcript |
| Gate runtime | + ≤ 90 s (clean build measured 29 s vs 2.8 s no-op; script tests ≤ 60 s incl. one nested clean build) | `time VERIFY_ENV=host scripts/verify.sh` before/after |
| Requirements tables of every component and `cdh.md` | unchanged | `git diff --stat -- 'PROVESFlightControllerReference/Components/*/docs/sdd.md' docs-site/requirements/cdh.md` empty |
| CI (`make test-unit`) | unchanged | `Makefile:228-231` untouched |

## 5. Non-goals
Raising any limit (A8 raises `MAX_PACKETIZER_CHANNELS`); the bench milestone; a target build or `fpp-check` in CI or
in the host gate; running `scripts/tests` in CI (follow-up: add to `Makefile test-unit` and `ci.yaml`);
`HealthPingPorts`, `CmdDispatcherComponentCommandPorts`, `RateGroupDriverRateGroupPorts`, `CmdDispatcherSequencePorts`
(same fpp-loud class; add as lines later); checking that `FaultTable.hpp`'s enum mirrors `FaultTypes.fpp` name-for-name
(a `static_assert` job, not a script's); verifying a rate-group instance is `Svc.ActiveRateGroup` rather than
`PassiveRateGroup` (all three are Active today, `instances.fpp:31-41`; the script assumes it and says so in its docstring).
