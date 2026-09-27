# 01 — Normative: results the tests are written from

Everything here is observable from a command line or a file diff. Nothing here says how the script parses anything (that
is `03-advisory.md`). `$PY` = `fprime-venv/bin/python3`, run from `$R`; `P` = `PROVESFlightControllerReference`;
`B` = `boards/bronco_space`.

## 1. Script contract — `scripts/check_hardware_consistency.py`

### 1.1 Command line
```
$PY scripts/check_hardware_consistency.py [--topology-cpp PATH] [--main-cpp PATH] [--dts PATH]
```
| Flag | Default | Read for |
|---|---|---|
| `--topology-cpp` | `P/ReferenceDeployment/Top/ReferenceDeploymentTopology.cpp` | every `<inst>.configure(state.tca9548aDevice, state.muxChannel<a>Device, state.<field>[, ...]);` call |
| `--main-cpp` | `P/ReferenceDeployment/Main.cpp` | `const struct device* <label> = DEVICE_DT_GET(DT_NODELABEL(<label>));` and `inputs.<field> = <label>;` |
| `--dts` | `B/proves_flight_control_board_v5/proves_flight_control_board_v5.dtsi` | the `mux_channel_<b>: i2c_mux@<b> { ... }` blocks and their node labels; the `&flash0 { reg = <...>; partitions { ... } }` block |

Standard library only, no fpp or devicetree tooling; runs in the host gate.

### 1.2 Rules
**HWC-1 mux-channels.** The set checked is every configure call in `--topology-cpp` whose instance name matches
`(tmp112|veml6031|drv2605)Face<n>Manager`. For each: `a` = the digit in `state.muxChannel<a>Device`; `<field>` (the third
argument, `state.<field>`) is resolved through `--main-cpp` to the devicetree node label assigned to it; `b` = the index of
the `mux_channel_<b>` block in `--dts` whose body declares that label (`<label>: <node>@<addr> {`). A manager is
*mismatched* iff `a != b`. On the tree at e1eced14: 17 managers checked (5 tmp112, 7 veml6031, 5 drv2605), 4 mismatched
(`drv2605Face1Manager`, `drv2605Face2Manager`, `drv2605Face3Manager`, `drv2605Face5Manager`, each passing
`muxChannel0Device`).

**HWC-2 flash-size.** `R` = the size cell of `&flash0 { reg = <0x10000000 SIZE>; }` in `--dts`, in bytes, where `SIZE` is
`DT_SIZE_M(n)` (= n × 1048576), `DT_SIZE_K(n)` (= n × 1024), a hex literal or a decimal literal. `E` = the maximum of
`offset + size` over every `reg = <OFFSET SIZE>;` inside the `partitions { ... }` block of that `&flash0` node. FAIL iff
`E > R`. On the tree at e1eced14: `R` = 4194304, `E` = 16777216 (storage_partition `0x400000 + 0xC00000`).

### 1.3 Output grammar (stdout, in this order)
```
HWC-1 mux-channels: <N> managers checked, <M> mismatched — <STATUS>
    <inst>: configure passes mux_channel_<a>, devicetree places <label> on mux_channel_<b>
HWC-2 flash-size: reg <R> bytes, partitions end <E> bytes — <STATUS>
RESULT: <OK|FAIL> — <a> ok, <b> warn, <c> skip, <d> fail
```
- One indented (four spaces) offender line per mismatched manager, in `--topology-cpp` order, only when `M > 0`.
- `N`, `M`, `R`, `E`, `a`–`d` are decimal integers. `STATUS ∈ {OK, FAIL}` (this script emits no WARN or SKIP; the tally
  prints them as 0 so the `RESULT:` line has the same shape as `check_capacity.py` and `check_docs.py`). The em dash is
  U+2014 with one space each side. `RESULT:` is the last line; `RESULT: FAIL` iff any rule line is FAIL.
- HWC-1 is FAIL iff `M > 0`. HWC-2 is FAIL iff `E > R`.

### 1.4 Exit codes
`0` no FAIL; `1` at least one FAIL; `2` an input cannot be used — a file unreadable, no configure call found, a `<field>`
with no `inputs.<field> = <label>;` in `--main-cpp`, a label declared in no `mux_channel_<b>` block, no `&flash0` block,
no `reg` in it, or no `partitions` block in it. On exit 2 nothing is printed to stdout; one line naming the input and the
reason goes to stderr.

### 1.5 Fixtures that fix each result (tests doctor a copy of a default input in `tmp_path`, never the tree)
| Result | Fixture | Expect |
|---|---|---|
| HWC-1 counts a drv2605 fix | `--topology-cpp` copy with `drv2605Face1Manager`'s `muxChannel0Device` → `muxChannel1Device` | `M` one lower than the tree's, still FAIL while `M > 0` |
| HWC-1 checks tmp112 and veml6031 too | copy with `tmp112Face2Manager`'s `muxChannel2Device` → `muxChannel0Device`; separately `veml6031Face7Manager`'s `muxChannel7Device` → `muxChannel4Device` | `M` one higher each; the offender line names the instance and both channels |
| HWC-1 OK | copy with all four drv2605 lines set to channels 1, 2, 3, 5 | `M` = 0, OK, exit 0 (given HWC-2 also OK, else exit 1 from HWC-2 only) |
| HWC-1 exit 2 | `--main-cpp` copy with the line `inputs.face3drv2605Device = face3_drv2605;` deleted | exit 2, stderr names `face3drv2605Device` |
| HWC-2 OK | `--dts` copy with `DT_SIZE_M(4)` → `DT_SIZE_M(16)` | `reg 16777216 bytes, partitions end 16777216 bytes — OK` |
| HWC-2 FAIL by a partition | `--dts` copy with `DT_SIZE_M(16)` and `storage_partition` size `0xC00000` → `0xC00001` | `E` = 16777217 > `R`, FAIL, exit 1 |
| HWC-2 literal forms | `--dts` copies with `reg = <0x10000000 0x1000000>` and `reg = <0x10000000 16777216>` | both `R` = 16777216 |
| HWC-2 exit 2 | `--dts` copy with the `partitions {` block removed | exit 2 |
| current tree | defaults | see §2: red before Stage 4, green after |

Tests on the real tree assert only: exit code, each rule line present and well-formed, and status (Cycle H amendment 1,
Cycle I amendment 1). Counts are proven on doctored copies only.

## 2. Flight results (what Stage 4 changes; each observable through §1 on the tree)
- **R1 (C-08).** `$PY scripts/check_hardware_consistency.py` on the tree prints `HWC-1 mux-channels: 17 managers checked, 0
  mismatched — OK`. Consequence in flight: `Drv2605Manager::initializeDevice` checks `device_is_ready` on the channel its own
  device sits on (`P/Components/Drv/Drv2605Manager/Drv2605Manager.cpp:153`), so `MuxUnhealthy` for face N now reports face
  N's channel. No I2C routing changes: transactions already went through `m_dev`, whose devicetree parent is the right
  channel (`04-findings.md` §1).
- **R2 (C-11).** The same run prints `HWC-2 flash-size: reg 16777216 bytes, partitions end 16777216 bytes — OK` and
  `RESULT: OK — 2 ok, 0 warn, 0 skip, 0 fail`, exit 0. In the build copy after the target build,
  `build-fprime-automatic-zephyr/zephyr/zephyr.dts` shows `flash0: flash@10000000 { ... reg = < 0x10000000 0x1000000 >; }`
  (was `0x400000`). The bootloader, current, golden, test and storage partition `reg` values are unchanged.

## 3. `verify.sh` results (no edit to `verify.sh` this cycle)
- `VERIFY_ENV=host scripts/verify.sh` → `result: PASS`, `unverified: (none)` once Stage 4 has landed; the `script tests`
  count rises by exactly the number of tests in `scripts/tests/test_check_hardware_consistency.py`; every other stage's
  output is unchanged in kind (the AUDIT-9 tests pass unchanged).
- Between Stage 3b and Stage 4 the two current-tree tests are FAILED and the gate is `result: FAIL` — expected, and the
  evidence that the tests preceded the code.

## 4. Harm table (existing behaviour → the observation that proves it unchanged)
| Existing behaviour | Observation |
|---|---|
| tmp112 and veml6031 configure lines (`ReferenceDeploymentTopology.cpp:160-164, 171-177`) | byte-identical in `git diff` |
| DetumbleManager → drv2605 axis wiring (`topology.fpp:395-405`) | `topology.fpp` not in `git diff` |
| Partition table (`v5.dtsi:67-96`) | only line 66 differs in `git diff -- boards/` |
| `check_capacity.py`, `check_packet_set.py`, `check_docs.py`, `doc_status.py`, `req.py`, `generate_rtm.py`, `verify.sh` | not in `git diff` beyond Cycle I's pre-existing hunks (`git diff --stat` before and after Stage 4 identical for these paths) |
| Host unit tests | 30 binaries PASSED, same names |
| Target image | builds; FLASH/RAM within ±0.1 % of the pre-change build; dictionary counts identical (no `.fpp` change) |

## 5. Non-goals
- No filesystem is mounted on `storage_partition` (the `fstab` at `v5.dtsi:18-21` is FATFS for the SD NAND; a LittleFS
  decision for flash is a separate row).
- No MCUboot rebuild (`make build-mcuboot`): the partition table is unchanged and MCUboot never touches `storage_partition`.
- No edit to `scripts/verify.sh`, `docs-site/requirements/tooling.md`, `scripts/check_capacity.py` or any Cycle I path.
- No manager for `face6_temp_sens` / `face6_drv2605` (C-07), no fix to the comment typo at
  `ReferenceDeploymentTopologyDefs.hpp:139` (`muxChannel6Device` documented as "channel 5").
- No upstream PR to Open-Source-Space-Foundation (C-08 is inherited PROVES code); worth reporting separately.
