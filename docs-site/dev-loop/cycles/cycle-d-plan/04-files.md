# 04 — File-by-file steps (build order). Every path relative to `$R`; `P`, `UT`, `Top` as in README.

## §A New component dir `P/Components/FaultManager/` — pure module first
1. `FaultTypes.fpp` — enums, port, `FaultInPorts` (02 §2a). Module `Components`.
2. `FaultTable.hpp/.cpp` — 02 §2b. Header guard `Components_FaultTable_HPP` (cpplint `#ifndef`), `<cstdint>` only, no `Fw/`, no `Os/`.
   Keep any constant array in the `.cpp` anonymous namespace (ODR trap, ledger "Cycle A coder findings").
3. `UT/CMakeLists.txt`: add `fault_manager_table` STATIC lib (`FaultTable.cpp`, include `../../..`), and append it to the `target_link_libraries`
   list in the glob loop (`:131-144`).
4. `UT/test_FaultManager_FaultTable.cpp` (claims `FaultManager-3`, `-4`, `-7` clause "clear"): sampled debounce 10 confirms on the 10th consecutive
   report and not on the 9th; a tick without a report resets; confirmation edge fires once; event types confirm after `DEBOUNCE_THERMAL` reports;
   `defaultPolicy` table equals 02 §2b (oracle = today's behaviour in 03, restated as constants); `claims()` truth table over enabled×mask×action;
   `clear()` zeroes everything; `activeMask()` bit positions. Header comment names the oracle sources (`ModeManager.fpp:200`, `AuthenticationRouter.cpp:44-51`).

## §B Component + recorder stub
5. `FaultManager.fpp` — 02 §2c (ports, params, commands, events, telemetry; standard AC ports copied from `TelemetryGate.fpp:45-68` plus
   `param get/set` ports as in `ModeManager.fpp:229-232`). `passive component FaultManager`.
6. `FaultManager.hpp/.cpp` — handlers per 02 §2c; `parameterUpdated` override (`ComDelay.hpp:30`, `.cpp:22-35`); `static_assert` value alignment
   with `FaultLogic`; `reasonFor(type)` = LOW_BATTERY→`SafeModeReason::LOW_BATTERY`, COMMAND_LOSS→`EXTERNAL_REQUEST`. Generated signatures:
   enum args arrive as `const Components::FaultType&`; return `Components::FaultDisposition`.
7. `CMakeLists.txt` — `register_fprime_library(AUTOCODER_INPUTS FaultTypes.fpp FaultManager.fpp SOURCES FaultTable.cpp FaultManager.cpp)` (shape of
   `TelemetryGate/CMakeLists.txt`; no DEPENDS needed — the ModeManager port type resolves through fpp-depend like `AuthenticationRouter`).
8. `docs/sdd.md` — headings as `TelemetryGate/docs/sdd.md` (Introduction, Design incl. shadow/authority diagram and the threading contract, Ports,
   Commands, Parameters, Telemetry, Events, `## Requirements` with the 7-column header from `TelemetryGate/docs/sdd.md:67-69` and **no rows**, Change Log).
9. `UT/support/PROVESFlightControllerReference/Components/FaultManager/FaultTypesStub.hpp` — mirror classes `FaultType/FaultSource/FaultSeverity/
   FaultAction/FaultDisposition` in the generated shape (copy `ThermalManager_TempSensorType` pattern, `ThermalManagerComponentAc.hpp:33-46`), plus
   `Components::SafeModeReason` is already in the ModeManager stub — include it, do not redefine.
10. `UT/support/.../FaultManager/FaultManagerComponentAc.hpp` — recorder base: pure-virtual handlers (`faultIn_handler`, `run_handler`, two
    `_cmdHandler`s, `parameterUpdated`), `lock()/unLock()` no-ops (count calls to assert balance), recorded `forceSafeModeCalls`, `stopWatchdogCalls`,
    `cmdResponses`, per-channel `tlm*` vectors, per-event vectors; `isConnected_*` flags default true; test-settable params + `paramValidity`;
    `PARAMID_*` constants; `getNum_faultIn_InputPorts()` = 4.
11. `UT/CMakeLists.txt`: `fault_manager_component` lib (`FaultManager.cpp`, includes `support` then `../../..`, links `fault_manager_table`); add to the glob link list.
12. `UT/test_FaultManager_Component.cpp` (claims `FaultManager-1`, `-2`, `-4`, `-5`, `-7`, `-9`): shadow default → zero action calls, one
    `FaultActionSuppressed` per confirmed non-NONE type, `ShadowActionsSuppressed` increments; INVALID/UNINIT params → shadow; authority on + mask
    0x10 → `forceSafeMode(LOW_BATTERY)` once after 10 reports; mask 0x20 → `stopWatchdog` then `forceSafeMode(EXTERNAL_REQUEST)` in that order;
    mask without the type → suppressed; disposition CLAIMED/OBSERVED table; `run` never calls an action port while `lock` depth > 0 (recorder asserts);
    channels written on change only; `CLEAR_FAULTS`/`GET_FAULT_STATUS` responses and events; `FaultAuthorityChanged` on param change.

## §C Producers (one at a time; rebuild + run the existing tests after each)
13. `P/Components/ThermalManager/ThermalManager.fpp`: `output port faultOut: Components.FaultReport` beside the other output ports (`:32-39` at HEAD).
    `.hpp`: private `void reportFault(Components::FaultType type, F64 temperature)`. `.cpp`: guarded call in `reportFault`; one call beside each of
    the two `log_WARNING_LO_Temperature*Threshold` lines mapping (sensorType, above/below) → FACE/BATT_TEMP_HIGH/LOW. Apply on the **working-tree**
    file (Cycle B edits present). Stub `UT/support/.../ThermalManager/ThermalManagerComponentAc.hpp`: `faultOutConnected=false`, `faultOutCalls`
    vector, `faultOut_out` returning `faultOutDisposition` (default OBSERVED); include `../FaultManager/FaultTypesStub.hpp`.
    `UT/test_ThermalManager_Thresholds.cpp`: add two tests (claims `FaultManager-6`): connected → exactly one report per event with matching type and
    temperature; unconnected → zero calls and the existing event sequence unchanged (existing tests untouched).
14. `P/Components/ModeManager/ModeManager.fpp`: `output port faultOut: Components.FaultReport` after `voltageGet` (`:70`). `.hpp`: private
    `bool reportLowBattery(F32 voltage)`. `.cpp` `run_handler`: first statement inside `if (isFault) {` (`:83`) → `const bool claimed =
    this->reportLowBattery(valid ? voltage : 0.0f);` then `if (!claimed) { existing :84-95 }`. Nothing else moves. Stub `ModeManagerComponentAc.hpp`:
    `faultOutConnected=false`, `faultOutCalls`, `faultOutDisposition`. `UT/test_ModeManager_VoltageDebounce.cpp`: add three tests (claims `FaultManager-6`,
    `MM0009`): connected+OBSERVED → 10 reports and `AutoSafeModeEntry` exactly as before (same tick); connected+CLAIMED → 10 reports, **no**
    `AutoSafeModeEntry`, no `enterSafeMode`, no `runSequence`; unconnected → behaviour byte-identical to the existing MM0009 tests (they must still pass unmodified).
15. `P/Components/AuthenticationRouter/AuthenticationRouter.fpp`: `output port faultOut: Components.FaultReport` beside `SetSafeMode` (`:48-49`);
    `.cpp` `CallSafeMode` (`:44-51`): `claimed` at the top (guarded call, COMMAND_LOSS, AUTH_ROUTER, CRITICAL, 0.0f); wrap `:45-47` and `:51` in
    `if (!claimed)`; leave `:49` and `run_handler` untouched. Not host-buildable (`:19` `<zephyr/drivers/rtc.h>`): target compile only.
    Update `docs/sdd.md` Port table (`:28-43`) by hand (non-requirements section).
16. `P/Components/Watchdog/Watchdog.fpp`: `output port faultOut: Components.FaultReport` after `prepareForReboot` (`:35`); `.cpp` `stop_handler`: guarded
    call after `:51` (WATCHDOG_STOPPED, WATCHDOG, CRITICAL, `static_cast<F32>(m_transitions)`). Host stub (new): `UT/support/.../Watchdog/
    WatchdogComponentAc.hpp` (recorder for `gpioSet_out`, `prepareForReboot_out`, `tlmWrite_WatchdogTransitions`, `log_ACTIVITY_HI_WatchdogStart/Stop`,
    `cmdResponse_out`, `faultOut_out`), `UT/support/Fw/Types/OnEnumAc.hpp` (`Fw::On {OFF,ON}`) and `UT/support/config/FpConfig.hpp` (includes
    `FpTypesStub.hpp`); add `Fw::Logic {LOW,HIGH}` to `FpTypesStub.hpp` if absent. `UT/CMakeLists.txt`: `watchdog_component` lib + link list.
    `UT/test_Watchdog_FaultReport.cpp` (claims `FaultManager-6`): stop → `m_run` false (run no longer toggles GPIO) and exactly one report; start/stop
    events unchanged; unconnected → no call.

## §D Deployment wiring
17. `Top/ReferenceDeploymentPackets.fppi`: new block after `HealthAuxiliary` (`:149-154`): `packet Faults id 9 group 5 { ReferenceDeployment.faultManager.<13 channels> }`.
    Nothing in `omit`. (Trap: a channel missing from both breaks `fpp-to-dict`.)
18. `P/Components/CMakeLists.txt`: `add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/FaultManager/")` between `Drv/` and `FatalHandler` (alphabetical).
19. `Top/instances.fpp`: after `picoTempManager` (`:245`): `instance faultManager: Components.FaultManager base id 0x1007A000` (next free; grep first).
20. `Top/topology.fpp`: `instance faultManager` after `instance modeManager` (`:91`); `rateGroup1Hz.RateGroupMemberOut[20] -> faultManager.run` after
    `[19]` (`:290`) with a comment "must follow modeManager[16]/thermalManager[18]/authenticationRouter[19]"; new block before `connections FatalHandler`
    (`:494`): `connections FaultManager { thermalManager.faultOut -> faultManager.faultIn[0]; modeManager.faultOut -> faultManager.faultIn[1];
    ComCcsdsLora.authenticationRouter.faultOut -> faultManager.faultIn[2]; watchdog.faultOut -> faultManager.faultIn[3];
    faultManager.forceSafeMode -> modeManager.forceSafeMode; faultManager.stopWatchdog -> watchdog.stop }`.
21. `P/project/config/CommandDispatcherImplCfg.hpp:14`: if still 350, set 400 (03 §Dictionary).
22. Target: rsync per CLAUDE.md, `fprime-util generate` (new fpp files/types require it), `fprime-util build`; then 05 checks.

## §E Docs, requirements, board test
23. `scripts/req.py add --group FaultManager …` for `FaultManager-1..9` (texts in 01/02; `--method "Unit Test" --level Unit` for 1-7, 9;
    `-8` `--method "Integration Test" --level Board`). Run **before** adding `RecordProperty("verifies", …)` lines. Then `req.py set FD-L2-05 --reason …`,
    `req.py set FD-L2-06 --reason …` (01 last paragraph).
24. `mkdocs.yml:72` add `- Fault Manager: components/FaultManager.md` after Watchdog; `Makefile:101` add the `cp` line after Watchdog; create
    `docs-site/components/FaultManager.md` with `cp` (make is unusable at this path). Add a "Fault reporting" paragraph + Change Log line to the
    ModeManager, ThermalManager, Watchdog, AuthenticationRouter sdds (hand-editable sections only) and re-`cp` their docs-site copies.
25. `P/test/int/fault_manager_test.py` (exemplar `telemetry_gate_test.py`; helpers `common.proves_send_and_assert_command`, `_PRM_SET` pattern
    `antenna_deployer_test.py:43-52`; raise/restore `CdhCore.tlmSend.SET_LEVEL` to 5): `test_faults_telemetry_level5` (`FD-L2-09`, `FaultManager-8`);
    `test_thermal_report_shadow` (`FD-L2-01`): lower `FACE_TEMP_UPPER_THRESHOLD`, expect `TemperatureAboveThreshold` and `FaultConfirmed` ≤ 2 s,
    `ShadowActionsSuppressed` unchanged (thermal action NONE), restore param; `test_watchdog_stop_reports_then_reboots` (`FD-L2-05`): `STOP_WATCHDOG` →
    `FaultConfirmed(WATCHDOG_STOPPED)` then `BootCount` +1 ≤ 60 s — destructive, last; command-loss test (`FD-L2-06`, `MS-L2-04`) documented but
    `@pytest.mark.skip(reason="reboots and persists SAFE_MODE; run manually on flatsat")`. Collect-only must pass; runs are DEFERRED on host.
26. Append 07 to `docs-site/dev-loop-findings.md` under a "Cycle D planner findings" heading (ledger discipline) and regenerate the RTM (05).
