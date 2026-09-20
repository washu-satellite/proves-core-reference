# 04 — Findings (verified facts, file:line) — 2026-09-19, tree 6b22f72d

Brief claims found wrong or imprecise are marked **[brief]**.

1. **`PRMDB_NUM_DB_ENTRIES` lives in the lib default, not the project.** `lib/fprime/default/config/PrmDbImplCfg.hpp:15`
   (`= 25`); `project/config/CMakeLists.txt` lists no `PrmDbImplCfg.hpp` override; used by
   `lib/fprime/Svc/PrmDb/PrmDbImpl.hpp:42` (`Fw::ArrayMap<..., PRMDB_NUM_DB_ENTRIES>`). **[brief]** "likely
   `lib/fprime/config/` or `project/config/`" — neither; and ledger line 22's `PrmDbImpl.hpp:118` is stale at 4.3.0.
2. **PrmDb overflow is not a boot assert.** `setPrm_handler` (`PrmDbImpl.cpp:96-113`): `NO_SLOTS` → `PrmDbFull`
   WARNING_HI, value dropped. `readParamFile` loop bounded by the constant (`:468`), a dropped record → `PrmDbFull` and
   `PrmLoadStatus::ERROR` (`:583-592`). A file can only hold what the map held, so a static overflow cannot occur;
   the constraint is HP-07's "save only the parameters under test" (ledger line 228).
3. **Dispatch-table overflow asserts at boot.** `CommandDispatcherImpl::compCmdReg_handler`
   (`lib/fprime/Svc/CmdDispatcher/CommandDispatcherImpl.cpp:26-38`): `FW_ASSERT(status == SUCCESS)` on a failed
   insert (`:35`), i.e. the (LIMIT+1)th distinct opcode; also asserts when an opcode re-registers from a different port
   (`:30`). Registration runs from `regCommands` during topology setup. `CMD_DISPATCHER_DISPATCH_TABLE_SIZE = 512`
   at `project/config/CommandDispatcherImplCfg.hpp:16` (as claimed).
4. **Dictionary.** Build copy `~/scalar-build/proves-core-reference` at 6b22f72d (7 dirty paths);
   `build-artifacts/zephyr/fprime-zephyr-deployment/dict/ReferenceDeploymentTopologyDictionary.json` (779 KB, 21:47):
   387 commands = 387 distinct opcodes, 106 parameters, 244 channels, 697 events, `telemetryPacketSets[0]` 23 members /
   70 omitted; `metadata.projectVersion = "v1.2.0-65-g6b22f72d"` (git-describe form — the staleness key),
   `frameworkVersion v4.3.0`. Its `constants` list (21 entries) holds none of `ActiveRateGroupOutputPorts`,
   `FaultInPorts`, `NUM_TASKS`, so those come from fpp. Past counts: ledger 121 (339 → 387), 189 (361), 201 (377), 217
   (387) — as claimed. Host tree: no dictionary anywhere under `$R`.
5. **`fpp-check` claim verified.** `fprime-venv/bin/fpp-check` runs at this path. Fixture (scratchpad): out-of-range
   index → `error: invalid port number 2 for port M.c.out (max is 1)`, rc 1; same output index twice → `error:
   duplicate connection at output port 0`, rc 1; same **input** index twice → rc 0 (allowed). Symbolic enum indices
   are accepted. It needs the transitive definition set via `-i` (the target build's `locs.fpp`, present only in the
   build copy's `build-fprime-automatic-zephyr/locs.fpp`), so it cannot run in the host gate, and this fork's CI never
   runs the target build — the loudness exists only for whoever builds in the copy. Hence the host script re-implements
   range + duplicate-output checks.
6. **Rate-group usage** (`topology.fpp`): `rateGroup1Hz` 20 connections, max index 20 (slot 19 free → 5 free);
   `rateGroup10Hz` 14, max 13; `rateGroup50Hz` 2, max 1; no duplicate output index. `ActiveRateGroupOutputPorts = 25`
   at `project/config/AcConstants.fpp:7` (as claimed); all three instances are `Svc.ActiveRateGroup`
   (`instances.fpp:31-41`); array declared `lib/fprime/Svc/ActiveRateGroup/ActiveRateGroup.fpp:15`.
7. **TaskGate indices are symbolic**: `taskGate.schedIn[Components.SchedTask.IMU]` etc. (`topology.fpp:309-326`), five
   pairs; enum `SchedTask` 0..4 and `constant NUM_TASKS = 5` (`Components/TaskGate/TaskGate.fpp:9-18`, as claimed);
   `TaskGate.cpp:32,73` `FW_ASSERT` on an out-of-range port/ordinal at runtime.
8. **FaultIn**: `faultIn[0]`, `[1]`, `[3]` connected (`topology.fpp:540-544`), slot 2 free; `constant FaultInPorts = 4`
   at `Components/FaultTypes/FaultTypes.fpp:65` (as claimed).
9. **FaultType mask**: enumerators `NONE = 0 .. ADCS_UNSTABLE = 8` (`FaultTypes.fpp:6-16`); `MAX_FAULT_TYPE = 8`
   (`Components/FaultManager/FaultTable.hpp:39`, as claimed), `NUM_FAULT_TYPES = 9` (`:36`); `param AUTHORITY_MASK: U8`
   (`FaultManager.fpp:60`); `FaultTable.cpp:67-70` returns bit 0 for `NONE` or `type > MAX_FAULT_TYPE` → silent,
   confirmed. **[brief]** ledger line 226 is about `0x0F` vs `0x10` in FaultManager-10, not the mask width.
10. **Packetizer**: `MAX_PACKETIZER_PACKETS = 24` (`TlmPacketizerCfg.hpp:19`), `MAX_PACKETIZER_CHANNELS = 256`
    (`:21-22`); `check_packet_set.py` in `verify.sh:50-56` (**[brief]** said 50-54). Standalone run on the tree, 0.06 s:
    ```
    packet set:  PROVESFlightControllerReference/ReferenceDeployment/Top/ReferenceDeploymentPackets.fppi
    config:      PROVESFlightControllerReference/project/config/TlmPacketizerCfg.hpp
    packets:     23 declared, MAX_PACKETIZER_PACKETS = 24
    channels:    244 distinct (174 in packets + 70 omitted), MAX_PACKETIZER_CHANNELS = 256
    WARN: 244 distinct channels is above 90% of MAX_PACKETIZER_CHANNELS (256); raise the limit before adding more
    OK: packet set fits the packetizer configuration
    ```
11. **Stale-object finding is not on record.** **[brief]** "Cycle G finding": neither `cycle-g-review.md` nor the ledger
    mentions it (grep `stale|build-gtest|restore|mtime|cp`); the only record is ROADMAP row 2. Mechanism is standard:
    make rebuilds by mtime, so a source restored with an mtime older than its object (`cp -p`, an iCloud restore) is not
    recompiled. `verify.sh:38-40` configures and builds incrementally. CI is unaffected: `make test-unit`
    (`Makefile:228-231`) on a fresh runner; CI does not call `verify.sh`.
12. **Clean-build cost**: fresh configure + serial `cmake --build` of `test/unit-tests` = 29 s wall (30 binaries;
    googletest in-tree, `test/unit-tests/CMakeLists.txt:11`); incremental no-op 2.8 s. Bound for V1: +30 s.
13. **`req.py` cannot create a group**: `parse_section_table` returns `None` without rows (`scripts/req.py:125-126`);
    `cmd_add` matches existing groups only (`:273-278`); system groups are `## ` headings of `docs-site/requirements/*.md`
    (`:134-141`); `generate_rtm.py:58` `SECTION_RE` agrees. `req.py add` prints "make rtm", which fails at this path.
14. **RTM link sources are closed**: `generate_rtm.py` links only `test/unit-tests/test_*.cpp` `RecordProperty` (`:159-175`)
    and `test/int/*_test.py` markers (`:177-196`); int-only rows are ⏸ deferred on host (`:332`, `:218-228`); an unlinked
    row shows `⬜ No automated test` (`:253`); a linked unit ref missing from junit shows `⚠️ Unit (no result)`
    (`:244`); pytest junit marks a skipped test with no `failure` element, which `parse_junit` (`:199-208`) would
    count as passed — so script tests must not `skip` a claiming test inside the gate. Matrix today: 313 requirements,
    131 linked, 75 passing, 56 deferred.
15. **pytest config**: `PROVESFlightControllerReference/pytest.ini` registers `verifies`; it is not an ancestor of
    `scripts/tests`, so a conftest there must register the marker. `interrogate --fail-under=40` runs on every commit
    (`.pre-commit-config.yaml:42-49`); ruff import-sort hook (`:33-37`).
16. **Consumers of the packet script's name/lines** (all remain valid because the script stays): `verify.sh:50,54`;
    `CLAUDE.md:61`; `project/config/TlmPacketizerCfg.hpp:26` (comment); `Components/FaultManager/docs/sdd.md:110` and its
    `docs-site/components/FaultManager.md:110` copy; ROADMAP rule 5; historical cycle docs.
17. **Same-class constants not in the roadmap row** (`AcConstants.fpp`): `CmdDispatcherComponentCommandPorts = 100` (`:16`;
    74 instances register commands per the dictionary), `HealthPingPorts = 25` (`:31`), `RateGroupDriverRateGroupPorts
    = 5` (`:13`), `CmdDispatcherSequencePorts = 6` (`:19`) — all fpp-loud at the target build; follow-up lines.
18. `verify.sh` runs `set -u` (`:15`), resolves `$PY` to the venv (`:18-19`), writes every log under `build-gtest/`
    (`:39,85,90`), and the RTM stage reads `build-gtest/junit.xml` (`:90`) — the reason `VERIFY_BUILD_DIR` must be
    threaded through every stage, not only the build.
