# Cycle L review — EPS channels commandable and correctly wired (plan `cycle-l-plan/`, branch `cycle-l` from `main` @ 7228ba85)

**Verdict: Ready, with amendments 1–6.** Self-reviewed by the orchestrator on 2026-09-27 (no second reviewer in this session;
recorded). Load-bearing claims checked at source: the mux↔connector map (S8 netlist U3 pins 4–20), the topology fan-out
(`topology.fpp:340-341, 367-371, 478, 492`), the lazy-init behaviour that bounds the L1 defect (`Tmp112Manager.cpp:37-57`,
`Veml6031Manager.cpp:36-56`), `ThermalManager.cpp:65` (no port guard), `Burnwire.cpp:44-45, 58-59` (unconditional writes),
`~{CHARGE}` net (IC6.4 + R70 10 k + U9.8) and `v5.dtsi:624-628`, `ZephyrGpioDriver.hpp:24-27` (`IN`), rate-group slot 19
(`topology.fpp:329`).

## Deliberate behaviour changes accepted
- Channel-5 managers follow `face4LoadSwitch`, channel-6 managers follow `face5LoadSwitch` (L1). Until the Stage 2 bench
  confirms the netlist inference, this is the only row that could be reverted; nothing else depends on it.
- A sixth face temperature in the ThermalManager sweep; `sensorId` 5 appears in threshold events (L2).
- A second Burnwire instance with commands `burnwireDeploy2.START_BURNWIRE/STOP_BURNWIRE` (L3). `antennaDeployer` unchanged.
- `charge` read as active-low; new `powerMonitor.Charging` channel and `ChargeStateChanged` event (L4).

## Amendments (normative; override the plan where they differ)
1. **Host tests assert behaviour through recorder stubs only**; board rows are `test/int` pytest (collected + linted) plus
   HP steps. No test asserts the real tree's dictionary counts; those are recorded, not asserted.
2. **Interface fixed by `01-normative.md`** (instance names, port names, channel/event names and formats, the dts flag).
   Coder mismatches are plan defects, not test edits.
3. **L1 fan-out resolved:** `LoadSwitch::setLoadSwitchState` already skips unconnected `loadSwitchStateChanged` ports
   (`LoadSwitch.cpp:68-73`), so `face5LoadSwitch.loadSwitchStateChanged[2]` is simply left unconnected. No change to
   `LoadSwitch`. (`03-advisory.md` L1 note is closed by this.)
4. **Environment:** this cycle runs in the linked worktree `proves-core-reference-cycle-l` on branch `cycle-l`, not in the
   shared tree — the DataRecorder cycle (M, `feat/data-recorder`, `~/scalar-wt/data-recorder`) is active in parallel. The
   venv is a symlink to the main tree's; Cycle J's `check_hardware_consistency.py`, its test and `hardware-consistency.md`
   are present untracked so R2.4 can run — **they are not part of this cycle's commits**. The build copy
   `~/scalar-build/proves-core-reference` is shared with Cycle M: exactly one rsync + target build for L, at the end, with
   the DataRecorder session told first; a build or rsync already running there is a stop.
5. **Requirement rows:** L1's Board row goes in the **LoadSwitch** component table (`Components/LoadSwitch/docs/sdd.md`,
   next `LoadSwitch-n`), not in `hardware-consistency.md` (a Cycle J file that must stay untouched here). L3 rows →
   Burnwire (`BW-nnn`), L4 rows → PowerMonitor (`PWR-MON-REQ-nnn`), L2 row → ThermalManager's face group. IDs = next unused
   per `req.py list --group`.
6. **Commit granularity (Stage 5):** three commits — L3 (Burnwire guard + `burnwireDeploy2` + its tests/rows), L4 (dts flag +
   PowerMonitor + `gpioCharge` + tests/rows), L1+L2 (topology fan-out, `tmp112Face6Manager`, ThermalManager constant,
   packets, tests/rows). Every `.fpp` change ⇒ `generate --force` before the one target build.

## Tests (Stage 3b) — pinned after the test author reports
_(hashes recorded here by the orchestrator before Stage 4)_
7. **Coexistence with Cycle M (DataRecorder), agreed 2026-09-27 by message.** Cycle M claims 1 Hz rate-group slot **21**
   and instance base id **0x10080000**; it appends `instance dataRecorder` after `driverBoardHandler` in `instances.fpp`,
   a new `connections DataRecorder { … }` block after `connections DriverBoard` in `topology.fpp`, 14 channels inside
   `packet FileSystem id 5`, and raises `MAX_PACKETIZER_CHANNELS` 256 → 288 in `project/config/TlmPacketizerCfg.hpp`. It
   touches nothing else in L's edit set. Therefore, for L: **slot 19** for `burnwireDeploy2.schedIn` (as planned); new
   instances at **0x10081000 and above** (`tmp112Face6Manager` 0x10081000, `gpioDeploy2` 0x10082000, `burnwireDeploy2`
   0x10083000, `gpioCharge` 0x10084000), placed after `ina219SolManager` (`instances.fpp:179`), not near
   `driverBoardHandler`; packet additions only inside `Thermal` (id 12) and `PowerMonitor` (id 11); no change to
   `TlmPacketizerCfg.hpp` (244 + L's 2 channels stays under 256; after the merge the count is ≈ 260/288, WARN not FAIL).
   **Build copy protocol:** Cycle M's A8-3 build is "within the hour"; each side messages before and after its rsync +
   build and waits if the other is mid-build. **Worktree rsync:** the CLAUDE.md rsync line plus `--exclude 'lib/'
   --exclude '.git'` (a linked worktree's `.git` is a file; the copy's `lib/` submodules and west modules are already
   current).
8. **Pinned counts that L's channels move (Stage 3b updates existing tests; red until Stage 4).** (a) `scripts/tests/test_check_capacity.py:27-34`
   `PINNED_PACKET_SET_OUTPUT` byte-pins `check_packet_set.py`: after L it reads `channels:    246 distinct (176 in packets + 70 omitted), MAX_PACKETIZER_CHANNELS = 256`
   and `WARN: 246 distinct channels is above 90% of MAX_PACKETIZER_CHANNELS (256); raise the limit before adding more`
   (packets line unchanged, 23 declared). (b) `scripts/tests/test_check_hardware_consistency.py` pins `17 managers`
   (`(base_report.n, base_report.m) == (17, 4)` and the exact `17 managers checked` line): after L it is **18** and `(18, 4)`.
   Both files are existing tests the author edits at Stage 3b for exactly these values and nothing else; their new hashes are
   pinned. Consequence for Stage 5: Cycle J's tooling (`check_hardware_consistency.py`, its test, `hardware-consistency.md`)
   must land as its own commit **before** L's, because L's gate and R2.4 depend on it — flagged to Jesse, who had deferred it.
   After the eventual merge with Cycle M (288 channels) the packet-set pin moves again (≈ 260 of 288); that belongs to the merge.
   Cycle M's A8-3 numbers, for reference only (not L's baseline — L is based on `main` @ 7228ba85): dictionary 396 / 106 / 258 / 705,
   FLASH 745344 B, RAM 359440 B, 1 Hz slots 19/25 used, max index 21. The build copy is free as of this note.
9. **Stage 3b decisions (author's report, 2026-09-27), accepted.** (a) `test_ThermalManager_Thresholds.cpp:269` `EXPECT_EQ(thermal.faceTempReads, 5u)`
   → `6u`: a direct consequence of the six-port stub, not in the plan's edit list; accepted as an existing-test edit.
   (b) `test_ThermalManager_SixthFace` is green before any code: the host stub *is* the six-port generated base, so it pins
   the sweep's behaviour over the port count but cannot see the `.fpp` constant. R2.3's results-first evidence is therefore
   the target build (six `faceTempGet` ports in the generated base), HWC-1 = 18 (R2.4) and the `topology.fpp` diff
   (`faceTempGet[5] → tmp112Face6Manager`), all Stage 4/5 observables. (c) `req.py add` normalised
   `Components/LoadSwitch/docs/sdd.md` from a 3-column to the 7-column table; row text unchanged. (d) The author read
   `Burnwire`, `PowerMonitor` and `ThermalManager` production files to shape the generated-base stubs; no assertion depends
   on them (stated; spot-checked on `test_Burnwire_SinglePort.cpp` and `test_PowerMonitor_ChargeStatus.cpp`). (e) Board-test
   environment gates chosen by the author and now part of the interface: `PROVES_J24_DUMMY_LOAD=1` (DEPLOY2 fires for real;
   skipped without it), `PROVES_VSOLAR_ON_CMD` / `PROVES_VSOLAR_OFF_CMD` (shell commands that switch the bench supply);
   `POWER_RISE_MIN_W = 0.3` reused from `drv2605_test.py`. The coder documents these in HP-11 (DEPLOY2, CHARGE steps) and HP-02
   (face-rail step); `hardware-procedures/README.md` is not edited in L (Cycle M edits it). (f) IDs assigned: BW-008 (Unit),
   BW-009 (Board), PWR-MON-REQ-011 (Unit), PWR-MON-REQ-012 (Board), ThermalManager-4 (Unit), LoadSwitch-1 (Board).
   (g) One more moved pin the brief missed: `scripts/tests/test_verify_sh.py` `HOST_TEST_BINARIES = 30` → 33 (three new
   host binaries); author resumed for that literal. Cycle M is complete on `feat/data-recorder` @ 4d183791 (nine commits on
   6296ef32, unmerged); it also moves the packet-set pin (to 288) and `HOST_TEST_BINARIES` (to 32) — whichever of L and M
   merges second re-pins both, per Cycle M's note.

Pinned 2026-09-27 after the Stage 3b reports (test author: Opus under the `cdh-test-author` contract; two follow-ups for moved
pins). 20 files; on the tree before Stage 4: host build 33 binaries, 31 pass (Burnwire single-port 3/5 red, PowerMonitor charge 3/5 red,
SixthFace green — amendment 9b); script-test pins red as intended (packet set 246, HWC-1 18, HOST_TEST_BINARIES 33).

| File | sha256 |
|---|---|
| `PROVESFlightControllerReference/test/unit-tests/test_Burnwire_SinglePort.cpp` | `76a2b820a37bc68c8a8f3265f58beb11d78a581b042509fae35ed16f87b59ba7` |
| `PROVESFlightControllerReference/test/unit-tests/test_PowerMonitor_ChargeStatus.cpp` | `fa293b6213d769a6ad664121525877168a5f46d3a7ab8d177862bf64f9ddcbf2` |
| `PROVESFlightControllerReference/test/unit-tests/test_ThermalManager_SixthFace.cpp` | `2408f94ca4e2e7230cf0a7dbffc3189d96147b451e9cb056be8df6c521922e40` |
| `PROVESFlightControllerReference/test/unit-tests/test_ThermalManager_CollectionInterval.cpp` | `ca68eaa396f3f47360a53de15aaaec7387aed88285af125f0b7e671dc3d8defd` |
| `PROVESFlightControllerReference/test/unit-tests/test_ThermalManager_Thresholds.cpp` | `159c9a479a8ebc094a7cbb129d1e32076e5d08b2c9fe2725770c2f7e3b3c7d23` |
| `PROVESFlightControllerReference/test/unit-tests/CMakeLists.txt` | `d8fb693cbbeae28c8275341bf40a43e664f1b7e0ca0724ae43bb8b17ced17b7c` |
| `PROVESFlightControllerReference/test/unit-tests/support/FpTypesStub.hpp` | `1abe734a64ac4e9f74fb20a5827715a025066565dd7e20b7da8247c2484c50b5` |
| `PROVESFlightControllerReference/test/unit-tests/support/PROVESFlightControllerReference/Components/Burnwire/BurnwireComponentAc.hpp` | `63915a10d00b5e2bd570122478ed590a364ca5d4322b5791eeca848999123f06` |
| `PROVESFlightControllerReference/test/unit-tests/support/PROVESFlightControllerReference/Components/PowerMonitor/PowerMonitorComponentAc.hpp` | `40e4b9d02712582ded0a273c3213a516a6e10e9e2b4762e61c2065f878e5234b` |
| `PROVESFlightControllerReference/test/unit-tests/support/PROVESFlightControllerReference/Components/ThermalManager/ThermalManagerComponentAc.hpp` | `b99e14c2745892fa90bb40ac7d58cfab6ce943a44999251fa2276d26205493c5` |
| `PROVESFlightControllerReference/test/int/face_rail_wiring_test.py` | `51000b2395e65373705ad8404a1911c75ef12b023d1d7d94b0a20b6a65aa762a` |
| `PROVESFlightControllerReference/test/int/burnwire_deploy2_test.py` | `d3c19e5973b30ecfe1fe526772a161fcf778bfc5408a6a8668eda61189bbfff0` |
| `PROVESFlightControllerReference/test/int/charge_status_test.py` | `7e4f9bd8aa35f8660533fab56625bf796dfedde4724ff190992b1cb6f3abd969` |
| `PROVESFlightControllerReference/Components/Burnwire/docs/sdd.md` | `c5585585647ad03d72bd7636125021b5ffc67b5acc10f29765c0cdcf7bd2f9f4` |
| `PROVESFlightControllerReference/Components/PowerMonitor/docs/sdd.md` | `4df44030ee5c6847451f4511fe6268d321ab440ca89278d4acb4a4b7551bf9ce` |
| `PROVESFlightControllerReference/Components/ThermalManager/docs/sdd.md` | `7eb6b687d31f0b9d75e54a26a642c2d5c26461e9509c140e0867bbce2f2825a0` |
| `PROVESFlightControllerReference/Components/LoadSwitch/docs/sdd.md` | `a81d104026a6e6c1ed165b0d2afb5e4774d7b8e67908aa06ddfe81a0bf15c17d` |
| `scripts/tests/test_check_capacity.py` | `3ca5cd4fef121f7445047cb31ac14011154c7fa038798063e8396a7ab383a545` |
| `scripts/tests/test_check_hardware_consistency.py` | `f013dc49121a8308b9740e091002a0f03da4461a8987bf9b449a9f568ca359f4` |
| `scripts/tests/test_verify_sh.py` | `b48fef637e5eabe9770e796b0a62530142c85ae8525de63e130729e2a7e4a0be` |

Merge note from Cycle M (2026-09-27): `cycles/README.md` gained a Cycle M row on `feat/data-recorder`; add L's row below it, not at the same anchor.

## Stage 4/5 record (2026-09-27)

Coder: Opus under the `cdh-coder` contract, in the `cycle-l` worktree. Stage 5 by the orchestrator from a clean `build-gtest`
(`scratchpad/cycle-l/stage5-verify.sh`, log `stage5-run.log`).

**Contract:** 20/20 pinned files unchanged after Stage 4; frozen files (Cycle J tooling, LoadSwitch, Tmp112/Veml managers, `lib/`)
unchanged; tracked diff 31 files, +247/−47, all within L's planned paths plus the gate-generated `docs-site/components/*.md` and
`requirements-matrix.md`. `TlmPacketizerCfg.hpp` untouched; only slot 19 taken; ids 0x10081000–0x10084000; packet additions only
in `Thermal` (12) and `PowerMonitor` (11) — amendment 7 respected.

**Gate:** `VERIFY_ENV=host scripts/verify.sh` → PASS, unverified (none). 33 host binaries, 33 pass (new: Burnwire_SinglePort 5/5,
PowerMonitor_ChargeStatus 5/5, ThermalManager_SixthFace 4/4). 93 int tests collected, lint clean. 57 script tests. pre-commit
all hooks passed. RTM 330 / 148 / 89 / 59. HWC-1 `18 managers checked, 0 mismatched — OK` (R2.4), HWC-2 OK.
`check_capacity --dictionary none`: channels 246/256 WARN (expected, pinned), rateGroup1Hz 19/25, RESULT OK.

**Target (build copy, sources cmp-identical to the worktree):** FLASH 728772 B (+2116 vs 726656), RAM 331648 B (+272 vs 331376).
`zephyr.dts` `charge` → `gpios = <&mcp23017 0xf 0x1>` (ACTIVE_LOW, R4.1). Generated base `NUM_FACETEMPGET_OUTPUT_PORTS = 6`,
`NUM_CHARGESTATUSGET_OUTPUT_PORTS = 1` (amendment 9b evidence for R2.3). Dictionary 392 / 246 / 711 / 107 (baseline 387 / 244 /
697 / 106): `burnwireDeploy2.{START,STOP}_BURNWIRE`, `SAFETY_TIMER_PRM_{SET,SAVE}`, `tmp112Face6Manager.GetTemperature`;
channels `powerMonitor.Charging`, `tmp112Face6Manager.Temperature`; event `powerMonitor.ChargeStateChanged` (param `state`,
"Battery charging: {}"). Console still disabled in the built `.config`.

**Coder deviations, all accepted:** (1) `event ChargeStateChanged($state: Fw.On)` — `state` is an FPP keyword; the escape is
upstream's own (`Drv/Ports/GpioDriverPorts.fpp:21`); dictionary name unchanged. Plan defect in R4.2's spelling, interface intact.
(2) The charge read is a private `updateChargeStatus()` called at the top of `run_handler`, before the interval check, so it runs
on every tick as R4.2 says (the advisory "after sampling" would have put it behind the decimator). (3) Slot-19 line placed before
the faultManager ordering comment, which belongs to slot 20. (4) HP-11 "Restore" renumbered 8 → 10. (5) ASCII hyphen in the dtsi
comments (file has no non-ASCII). (6) Dictionary deltas larger than the harm table's estimate (+5 commands, +14 events, +1
parameter, not +2/+≈5/+0): the estimate forgot Burnwire's `SAFETY_TIMER` parameter commands and the Tmp112 manager's own
command/events. Harm-table estimate error, not a code defect; recorded here.

**Findings (to the ledger / cleanup):** (a) `scripts/verify.sh:129` reads `.git/hooks/pre-commit`, which does not exist in a
linked worktree (`.git` is a file); the gate then reports "not installed here" and cannot reach `unverified: (none)`. Stage 5 used a
PATH shim running the hook's recorded interpreter; the fix is `git rev-parse --git-path hooks/pre-commit`, owned by verify.sh, not
this cycle. (b) `lib/fprime-zephyr/.../ZephyrGpioDriver.cpp:57-59` returns OP_OK unconditionally, so a failed `gpio_pin_get_dt`
reads as LOW = "not charging" rather than an error. Upstream behaviour; noted for the CHARGE channel's interpretation. (c) HP-02
now says "9 TMP112" (Hardware row, step 6, CDH-31 row) and step 4 "tmp112 face0-3,5"; after L there are 10 (face6 added). Not edited
in L (additions-only docs rule); cleanup item. (d) Pre-existing: `ReferenceDeploymentTopologyDefs.hpp:139` comments
`muxChannel6Device` as "channel 5"; `instances.fpp:193` declares `tmp112Face4Manager` (0x10046000) which is in no topology. Cleanup
items. (e) The build copy now holds L's tree; Cycle M's DataRecorder files there are orphaned until M rsyncs again.

**Not verified here:** the Board rows R1.3/LoadSwitch-1, R3.3/BW-009, R4.4/PWR-MON-REQ-012 (need a bench: face boards on J1/J2,
dummy load on J24, VSOLAR supply); boot of the image with 246 packetized channels (static check only); the netlist inference
"same connector ⇒ same rail" (R1.3 is its bench test).

**Commits:** none yet. Order when authorised: Cycle J tooling first (`check_hardware_consistency.py`, its test — red at 18 until
L's third commit — and `hardware-consistency.md`), then L3, L4, L1+L2 per amendment 6 with the coder's drafted messages;
shared files (`instances.fpp`, `topology.fpp`, `ReferenceDeploymentTopology.cpp`, `ReferenceDeploymentPackets.fppi`, `v5.dtsi`,
HP-11) hunk-split. Intermediate commits do not pass the moved pins (33 binaries, 246 channels, HWC 18) until all three land.
`cycles/README.md` row for L goes below Cycle M's at merge.

10. **Commit granularity at landing (2026-09-27, orchestrator):** amendment 6's three commits (L3, L4, L1+L2) are folded into one
    `feat(EPS)` commit. Their hunks interleave inside `instances.fpp`, `ReferenceDeploymentTopology.cpp`, HP-11 and the packet
    file, so splitting by hand costs more than it documents; the per-item content is in the Stage 4/5 record above and in the
    commit body. Landed after main took Cycle M (1498c1bb) and PR #12 (3938ae8b, Cycle J tooling): the merge re-pins
    `HOST_TEST_BINARIES` 32 → 35 and the packet set to 260 of 288, HWC-1 to 18, HP-02/cdh.md "9 TMP112" → 10, and adds
    `antennaDeployer.RESET_DEPLOYMENT_STATE` to HP-11's Restore (steps 6–7 write the deployed flag on FAILED,
    `AntennaDeployer.cpp:221-226`; finding from the Cycle M session).
