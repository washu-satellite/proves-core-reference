# 05 — File-by-file steps, in order

Conventions as Cycle B/D: `req.py` for every requirements-table edit; `#ifndef` guards; clang-format 120 col; no `lib/` edits; every new channel in the packet set; `fprime-util generate` needed (new components, new ports).

**Step 0 — requirements first.** `fprime-venv/bin/python3 scripts/req.py add --group DriverBoardProtocol --id DriverBoardProtocol-1 ... -6` and `--group DriverBoardHandler --id DriverBoardHandler-1 ... -10` with the criteria in 01 (create the two sdd `## Requirements` table headers first; -10 is Board/Flatsat, Integration Test). `req.py set TM-L2-01 --reason "payload source registered by DriverBoardHandler (Cycle E); board evidence deferred"`; `req.py set CDH-17 --reason "link exists (Cycle E); STM32 reflash path absent"`; same for CDH-20.

**Step 1 — `Components/Crc16/Crc16.hpp`** (new, header-only, no CMakeLists). Move the bitwise CRC from `Components/TcFrameCorrector/TcFrameCorrectorCodec.cpp` into `namespace Crc16 { inline uint16_t ccitt(const uint8_t*, size_t); }`. Edit `TcFrameCorrectorCodec.cpp` to call it; keep the public `crc16Ccitt` symbol in `TcFrameCorrectorCodec.hpp` as a one-line forwarder so its test compiles unchanged.

**Step 2 — project config (row E1).** Test first: `scripts/check_packet_set.py` parses `ReferenceDeploymentPackets.fppi` (packet count, distinct channel count incl. the omit block) and `TlmPacketizerCfg.hpp`, exits non-zero if packets > `MAX_PACKETIZER_PACKETS` or channels > `TLMPACKETIZER_HASH_BUCKETS`; wired into `scripts/verify.sh` as a step. It FAILS at HEAD (214 > 202). Then `TlmPacketizerCfg.hpp:19` → 24, `:27` `TLMPACKETIZER_HASH_BUCKETS` → 256, `project/config/CommandDispatcherImplCfg.hpp:14` → 512. Ledger line: Cycle D introduced the latent boot assert. `PRMDB_NUM_DB_ENTRIES`: **do not raise in this cycle** (gated, 07); when the gate passes, confirm where the effective value comes from (`grep -rn PRMDB_NUM_DB_ENTRIES lib/fprime/config project/config`), override in `project/config/FpConfig.h`, target 128, fall back to 64 if the RAM guard trips.

**Step 3 — `Components/DriverBoardProtocol/`** (new): `DriverBoardProtocol.hpp/.cpp`, `DriverBoardMessages.hpp`, `CMakeLists.txt` (plain `register_fprime_module` with the `.cpp`, no fpp), `docs/sdd.md` (copy 02 into it as the normative spec section, plus the Requirements table). Register in `Components/CMakeLists.txt` alphabetically.

**Step 4 — `Components/DriverBoardHandler/`** (new): `DriverBoardHandler.fpp` per 3.3 (enums `DriverState`, `LinkState`, `DisarmReason`, `RefuseReason` declared `: U8` in the same module block); `DriverBoardLink.hpp/.cpp` (pure state machine, no F' includes); `DriverBoardHandler.hpp/.cpp` (thin: port handlers call the codec and the link module; `parameterUpdated` recomputes cached effective values; `run_handler` per 3.5; `uartRecv_handler` per 3.7); `CMakeLists.txt` (fpp + cpp + link), `docs/sdd.md` with the 3.x sections, Parameters/Commands/Telemetry/Events tables, Change Log. Register in `Components/CMakeLists.txt`.

**Step 5 — instances** (`ReferenceDeployment/Top/instances.fpp`): after `faultManager` (base id `0x1007C000`):
`instance driverBoardUart: Zephyr.ZephyrUartDriver base id 0x1007D000`;
`instance driverBoardBufferManager: Svc.BufferManager base id 0x1007E000` with the same three phases as `payloadBufferManager` (`instances.fpp:134-154`) but `bins[0] = {128, 4}`, manager id 2, store id 0, allocator `ComCcsds::Allocation::memAllocator`;
`instance driverBoardHandler: Components.DriverBoardHandler base id 0x1007F000`.

**Step 6 — topology** (`ReferenceDeployment/Top/topology.fpp`): add the three instances; new `connections DriverBoard { ... }` block per 3.1; `rateGroup50Hz.RateGroupMemberOut[1] -> driverBoardUart.schedIn`; `rateGroup1Hz.RateGroupMemberOut[12] -> driverBoardHandler.run`; `rateGroup10Hz.RateGroupMemberOut[5] -> driverBoardBufferManager.schedIn`; `driverBoardHandler.getMode -> modeManager.getMode`. Command/param/tlm/event wiring is automatic via the existing `command connections instance CdhCore.cmdDisp` etc.

**Step 7 — topology C++** (`ReferenceDeploymentTopology.cpp`): beside line 146, `driverBoardUart.configure(state.peripheralUart2, state.peripheralBaudRate2);`. `Main.cpp` already sets both (`:127-128`).

**Step 8 — packet set** (`ReferenceDeploymentPackets.fppi`): the `PayloadHousekeeping id 23 group 3` block from 3.4, placed after `Detumble id 15`. No `omit` entries.

**Step 9 — host stubs** (`test/unit-tests/support/PROVESFlightControllerReference/Components/DriverBoardHandler/DriverBoardHandlerComponentAc.hpp`, new, modelled on the FaultManager and ThermalManager stubs): recorders for `uartSend_out` (vector of byte vectors), `uartRecvReturn_out` (vector of buffer pointers), every `tlmWrite_*`, every `log_*`, `cmdResponse_out`; a settable `getMode_out` return; `paramGet_*` served from public members with `paramValidity`; `getTime()` from a settable tick. `Fw::Buffer` stub: check `support/Fw/` — add a minimal `Fw/Buffer/Buffer.hpp` (data pointer + size) if absent. `Drv::ByteStreamStatus` enum stub in `FpTypesStub.hpp`.

**Step 10 — tests** (`test/unit-tests/`, auto-globbed; CMake: add `crc16` interface target, `driver_board_protocol` STATIC, `driver_board_link` STATIC, `driver_board_handler_component` STATIC linking both and the stub include dir):
- `test_Crc16.cpp`: check value; empty buffer 0xFFFF.
- `test_DriverBoardProtocol_Codec.cpp` (DriverBoardProtocol-1..6): round-trip every type; bad CRC; LEN 33; truncated then valid; garbage then valid; two back-to-back frames; seq gap counted; `sizeof(Parser) <= 64`.
- `test_DriverBoardLink.cpp` (DriverBoardHandler-2, -5, -6 logic half): pure transitions with injected time and mode.
- `test_DriverBoardHandler_Component.cpp` (DriverBoardHandler-1, -3, -4, -7, -8, -9): a `FakeBoard` helper in the test that feeds host frames into a second `Parser`, answers ACK/PONG/HK per 02, and pushes the reply bytes into `uartRecv_handler`. This fake is the executable spec; keep it in `test/unit-tests/support/DriverBoardFake.hpp` so the future STM32 firmware tests can diff against it.
- `test/int/driver_board_test.py` (DriverBoardHandler-10, TM-L2-01, CDH-27, ADCS-L2-06): exemplar `telemetry_gate_test.py`; fixtures raise `SET_LEVEL 3` and `telemetryDelay.DIVIDER 0`, restore on teardown; marked `verifies` and a new `flatsat` marker in the repo-root `pytest.ini` (there is no `test/int/pytest.ini`); collected + ruff-linted only.

**Step 11 — hardware procedure** `docs-site/dev-loop/hardware-procedures/HP-15-driver-board-link.md`: bench setup (FC V5e + driver board on J18 9/10 + GND, PAYLOAD_PWR on via `payloadPowerLoadSwitch.TURN_ON`), the ten steps of DriverBoardHandler-10, loopback variant with TX1 jumpered to RX1 (host sees its own HEARTBEAT: `FramesRejected` stays 0, `FramesReceived` increments, `LinkState` stays DOWN because host-type frames are not board frames — a useful pre-board smoke test).

**Step 12 — docs.** `Components/PayloadCom/docs/sdd.md`: one sentence in the overview: "camera-specific: acknowledges every buffer with the Nicla `<MOISES>` token; not a generic payload transport (see DriverBoardHandler for the direct-driver pattern)". `docs-site/components/DriverBoardHandler.md`, `DriverBoardProtocol.md` copies; `mkdocs.yml` nav; ledger merge from 08.

**Step 13 — sequences (ops, not code):** `sequences/payload_on.seq` (`payloadPowerLoadSwitch.TURN_ON`, wait 2 s, `driverBoardHandler.PING`) and `payload_off.seq` (`DISARM`, `TURN_OFF`). Compiled with the existing seqgen path; documented in HP-15.

**Step 14 — SCALAR model** (`~/scalar`, separate repo): `SCALAR_DataBudget::PayloadHousekeepingPacket` channels 10 → 21, payload 28 → 48 B, wire 43 → 63 B, `bytesPerDayUpper` 181440, `packetId` 23, `group` 3, `packetExists` stays false until the cycle lands; `Payload.sysml` `DriverHostInterface` gains `wireProtocolSpec = "docs-site/dev-loop/cycles/cycle-e-plan/02-protocol.md"`. Done by the planner on 2026-09-17 (see 08).
