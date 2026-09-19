# 06 — Verification

Host gate (`VERIFY_ENV=host scripts/verify.sh` from `$R`, clean `build-gtest`):
- New binaries green: `test_Crc16`, `test_DriverBoardProtocol_Codec`, `test_DriverBoardLink`, `test_DriverBoardHandler_Component`. Existing binaries unchanged and green, **in particular `test_TcFrameCorrector_Codec` with zero edits**.
- Int collect: +1 test file; `flatsat` marker registered in `pytest.ini`; ruff clean.
- RTM: DriverBoardProtocol-1..6 and DriverBoardHandler-1..9 Pass (Unit); DriverBoardHandler-10, TM-L2-01, CDH-27, ADCS-L2-06 deferred (board/flatsat), never "unverified".

Target compile (mandatory: new `.fpp`, new ports): rsync to `~/scalar-build/proves-core-reference`, `fprime-util generate` then `fprime-util build`. Expect and record:
- Dictionary (`build-artifacts/.../ReferenceDeploymentTopologyDictionary.json`), expected after E5 from the Cycle D baseline (361 / 98 / 215 / 676 / 22): commands **377**, parameters **103**, channels **237**, events **688**, packets **23**; `telemetryPacketSets` shows `PayloadHousekeeping` id 23 with **22** members.
- Memory-region lines: FLASH and RAM percentages against the last recorded 65.7 % / 64.1 %; RAM must stay under 70 % with the PrmDb raise; if not, PrmDb 64 and re-record.
- `fpp-to-dict` passes (all channels in a packet).
- `git diff --stat lib/` empty.

Static checks the reviewer does by eye: `uartRecv_handler` returns the buffer on every path (early returns included); no F' include in `DriverBoardProtocol/` or `DriverBoardLink.*`; enums in the fpp are `: U8`; `driverBoardHandler.run` is not routed through TaskGate; `PayloadCom`/`CameraHandler` untouched.

Bench (later, HP-15): loopback smoke without the board, then the ten-step DriverBoardHandler-10 with the STM32 answering 02.
