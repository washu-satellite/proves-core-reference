# 05 — Verification (normative expectations; §fakes is advisory)

## 1. Host gate (every row)

From `$R`, clean `build-gtest`: `VERIFY_ENV=host scripts/verify.sh` → PASS, `unverified: (none)`. Board-level rows report **deferred**. New in this cycle: the host CMake configure must find `psa/crypto.h` and `libmbedcrypto` — on this machine `find_path` resolves to `/opt/homebrew/include` and `find_library` to `/opt/homebrew/lib/libmbedcrypto.dylib` through CMake's default `CMAKE_SYSTEM_PREFIX_PATH` (it contains `/opt/homebrew` on this host; probed with cmake 4.4.3 — `07-findings.md`). No `CMAKE_INCLUDE_PATH` is needed; if a machine lacks Homebrew mbedtls, `verify.sh` reports "host unit tests" unverified, which is the correct signal (`brew install mbedtls`).

## 2. Target compile (F1, F2, F3)

`rsync -a --delete --exclude 'build-fprime-automatic-zephyr*' --exclude 'build-gtest' --exclude 'build-artifacts' --exclude '.venv' --exclude 'fprime-venv' ./ ~/scalar-build/proves-core-reference/` then in the copy `fprime-util generate` (F1 only; forced by the pointer change) and `fprime-util build`. Record `Memory region … FLASH … RAM` lines and the dictionary counts (`build-artifacts/**/ReferenceDeploymentTopologyDictionary.json` — count `commands`, `parameters`, `telemetryChannels`, `events`, `telemetryPacketSets[0].packets`).

## 3. Packet-set arithmetic (F1)

`fprime-venv/bin/python3 scripts/check_packet_set.py` must print: packets 23 ≤ 24; channels 243 ≤ 256 (using `MAX_PACKETIZER_CHANNELS`). Derivation to check by hand:
- upstream a477893b: 21 packets, 128 in packets + 65 omitted = 193;
- fork keeps 51 − 1 = 50 channels (`CdhCore.tlmSend.SendLevel` dropped): 45 in packets (`Faults` 13, `PayloadHousekeeping` 22, `taskGate` 2, `tcFrameCorrector` 2, `telemetryGate` 2, four `CollectionIntervalS`) + 5 omitted (`driverBoardBufferManager.*`);
- 128 + 45 = 173 in packets, 65 + 5 = 70 omitted, 243 total; packets 21 + 2 = 23 (`Authenticate id 6` gone, `Security id 6` in).
Dictionary `telemetryChannels` must equal 243 (fpp-to-dict rejects a channel absent from the packet set, so the two counts coincide).

## 4. Requirement links (F1, F2, F3, F5)

`grep -E "^\| ?(MM0013|REQ-SM-009|REQ-SM-010|REQ-SM-011|REQ-SM-012|AUTH013) ?\|" docs-site/requirements-matrix.md` shows each row with its test after the row that adds it; `REQ-SM-008` shows only quiescence tests; `CH-L2-05` shows no test until followups §3 is decided; no `authentication_test.py` or `safe_mode_test.py` string remains in the matrix.

## 5. Docs copies (F1, F5)

`for f in PROVESFlightControllerReference/Components/*/docs/sdd.md; do n=$(basename $(dirname $(dirname $f))); cmp -s $f docs-site/components/$n.md || echo DIFF $n; done` prints nothing (except components deliberately without a copy today). `pre-commit run docs-sync --all-files` exits 0 (or is skipped by the F0 fallback with the same `cmp` result).

## 6. Per-row expected deltas

| Row | Host tests | Dictionary | RAM/FLASH |
|---|---|---|---|
| F0 | unchanged | upstream baseline recorded | upstream baseline recorded |
| F1 | −8 (`test_Authenticate_SequenceNumberStore`), +4 upstream (`TcSecurityDeframer_{Parser,Validator,Authenticator}`, `ProvesRouter_Bypasser` — count their cases), boot-count cases rewritten, quiescence cases unchanged | 23 packets, 243 channels; cmds/params/events = baseline + fork surface | recorded; RAM ≤ 70 % |
| F2 | +8 (`test_TcSecurityDeframer_SequenceNumberStore`) | +2 events (one per deframer instance) | ≈ 0 |
| F3 | +2 cases (OBSERVED/CLAIMED) | 0 | ≈ 0 |
| F5 | 0 | 0 | 0 |

## fakes (advisory — what the host build of the merged ModeManager/StartupManager needs)

`test/unit-tests/support/` today has `Os/File.hpp`, `Os/FileSystem.hpp`, `Fw/Time/Time.hpp`, `Fw/Types/*`, `Fw/Buffer/Buffer.hpp`, `zephyr/drivers/rtc.h` and one recorder stub per component. The merged sources add: `Os/Mutex.hpp` (`Os::Mutex`, `Os::ScopeLock` no-ops); `Fw/Serializable.hpp`-shaped `Fw::ExternalSerializeBuffer` with `serializeFrom`/`deserializeTo` for `FwSizeType` only, big-endian like F' so a corrupt-file test can craft bytes; `Fw::TimeIntervalValue` (`get_seconds/get_useconds`) if `FpTypesStub.hpp` lacks it; ModeManager stub: `packetRouted_handler` slot, `stopWatchdog_out` recorder, `sequenceDoneNotify_out` + `isConnected_sequenceDoneNotify_OutputPort`, `paramGet_COMM_LOSS_TIME`, `log_WARNING_HI_CommandLossDetected`; StartupManager stub: `startupsequenceStarted_handler(…, const Svc::SeqArgs&)`, `safeMode*`/`payload*` handlers, `loraFirstStart_handler`, `enableTransmit_out`/`disableTransmit_out`, `paramGet_TRANSMIT_ENABLE_TICKS`, `log_WARNING_HI_BootCountCorrupted`, `log_ACTIVITY_HI_HardcodedRadioEnable`; `Os/File.hpp` fake already models `flush()`, `OPEN_CREATE`/`OVERWRITE` and failure switches (`:29-32,49-50,121`). Keep the fakes F'-free (`test/unit-tests/README.md`).
