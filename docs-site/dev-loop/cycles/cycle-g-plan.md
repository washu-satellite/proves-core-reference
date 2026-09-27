# Cycle G plan — A10: saved parameters are applied at boot

Branch `feat/upstream-sync` @ e99b26ef (post-sync, ROADMAP queue item 1). Upstream check: 0 commits behind
`proves-origin/main` (2026-09-19). Stage 0 gate: PASS. One row.

## Normative

### The defect
F' generated `loadParameters()` reads `/prmDb.dat` into the parameter database but never calls
`parameterUpdated()` (ledger "Upstream sync" section; `build-fprime-automatic-zephyr/.../ThermalManagerComponentAc.cpp`).
Any component that caches an effective value only inside `parameterUpdated()` therefore runs on its compiled default
after every reboot, whatever was saved. Confirmed by grep on 2026-09-19: the components below override
`parameterUpdated` and cache there, and none re-reads at boot except DriverBoardHandler (`DriverBoardHandler.cpp:141-147`,
`applyAllParameters()`), which is the pattern to copy.

| Component | Cached parameters | Origin |
|---|---|---|
| ThermalManager | COLLECTION_INTERVAL_S | fork (Cycle B) |
| ADCS | COLLECTION_INTERVAL_S | fork (Cycle B) |
| PowerMonitor | COLLECTION_INTERVAL_S | fork (Cycle B) |
| ImuManager | COLLECTION_INTERVAL_S | fork (Cycle B); target-only (Zephyr headers), no host test |
| FaultManager | AUTHORITY_ENABLED, AUTHORITY_MASK, debounces (`refreshParameters()` only from `parameterUpdated`) | fork (Cycle D) — a saved AUTHORITY_ENABLED is silently ignored at boot, which defeats the authority-enable procedure |
| ComDelay | DIVIDER | fork |
| RtcManager | TIMEBASE | upstream #455 (modified upstream 3× since the fork point) |
| DetumbleManager | GAIN, BDOT_MAX_THRESHOLD, DEADBAND_LOWER/UPPER_THRESHOLD, COOLDOWN_DURATION, HYSTERESIS_AXIS | upstream #266 |

### Results (tests derive from these only)
- R1 For each component above, with a valid parameter value present in the database before the first scheduler
  tick and **no** `parameterUpdated` call, the first tick uses that value (the observable is the component's own:
  decimation count for the four interval components, `FaultManager` authority/mask in `AuthorityState`, ComDelay
  divider period, RtcManager time base in its `TimeBaseChanged`/telemetry, DetumbleManager gain/thresholds via its
  strategy or telemetry), and the component's effective-value telemetry channel, where one exists, is written once.
- R2 With an INVALID/UNINIT parameter at boot, behaviour is exactly today's (compiled default, and only the events
  today's `parameterUpdated` path emits for invalid values, at most once).
- R3 A later `PRM_SET` still takes effect through `parameterUpdated` as today (no double application, no duplicate
  event).
- R4 No output port is invoked from the boot refresh that is not already invoked from `parameterUpdated`; no new
  ports, params, channels, events or packets; dictionary unchanged (387/106/244/697/23); `git diff --stat -- lib/` empty.
- R5 Host gate PASS, unverified (none); every existing binary green; target build succeeds, FLASH/RAM recorded
  (expect ≤ a few hundred bytes).
- R6 One commit for all eight (the change is one mechanical pattern), or one per component if the coder prefers —
  either way the gate runs before the commit.
- R7 For the two upstream-maintained components, the diff is minimal and self-contained so it can be offered
  upstream as a fix: a first-tick flag plus one call, no restructuring.

### Requirements
Add via `req.py add` (Unit, Unit Test) `ParamBoot-1..N` under a new group `ParameterBoot`? No — requirements belong
to their components: add one row per component group (e.g. `ThermalManager-3`, `ADCS-3`, `PWR-MON-REQ-010`,
`FaultManager-10`, `ComDelay-1` if the sdd has a table, RtcManager and DetumbleManager rows in their tables) with
the criterion "a parameter value present in PrmDb at boot is the effective value on the first run tick without any
PRM_SET; an invalid value yields today's default and today's event". ImuManager's row is Board level (no host build).

## Advisory (method)
Copy `DriverBoardHandler`: a `bool m_paramsApplied` member (declared last, `-Wreorder`), and at the top of the run
handler (or the first scheduled entry point for components without a `run`, e.g. ComDelay's tick port) `if
(!m_paramsApplied) { m_paramsApplied = true; <refresh>; }` where `<refresh>` calls the existing per-parameter apply
logic (`parameterUpdated(PARAMID_X)` for each id, or the component's existing `refreshParameters()`). Do not touch
`parameterUpdated` itself. Keep the constructor initialisations as they are so a never-run component behaves as
today. For host tests, extend the existing recorder stubs' `paramGet_*` with settable values + `paramValidity`
(most already have them from Cycle B/D) and add one test per component: set the stub value, construct, run one
tick, assert the observable and the single telemetry write; a second test with `INVALID` pins R2; a third with a
`PRM_SET` after boot pins R3.

## Harm table (results)
| Existing behaviour | After | Proof |
|---|---|---|
| Compiled defaults when nothing is saved | identical | R2 tests; stub default validity UNINIT |
| `PRM_SET` path | identical | R3 tests; `parameterUpdated` untouched |
| First-tick cost | + one parameter read per cached param, once | code inspection |
| Dictionary, packets, opcodes | unchanged | R4 |
| Upstream files RtcManager, DetumbleManager | minimal diff | R7 |

## Verification
Host: `VERIFY_ENV=host scripts/verify.sh` PASS with the new rows passing (Unit) and ImuManager's deferred. Target:
build from the copy (rsync excludes per CLAUDE.md, SDK 1.0.1), memory lines; no `generate` unless an `.fpp` changes
(it should not). Board: covered by the existing HP-07 reboot-persistence procedure once PRM_SAVE_FILE is trusted
(persistence gate); add one line there.

## Non-goals
Raising `PRMDB_NUM_DB_ENTRIES` (gated); `telemetryDelay`/`downlinkDelay` (lib `Utilities.RateDelay`: same defect,
lib-owned — report upstream, do not patch); any change to what is cached or how it is validated.
