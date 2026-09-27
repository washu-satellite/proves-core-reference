# 04 — Harm table

| Existing behaviour | After this cycle | How preserved / proof |
|---|---|---|
| Camera path: `peripheralUartDriver` on uart0, `PayloadCom`, `CameraHandler`, `payloadBufferManager` 2 x 4 KB | untouched — not one line changes in those four | `git diff --stat` shows no change under `Components/PayloadCom`, `Components/CameraHandler`; `instances.fpp:130-154` unchanged |
| `TcFrameCorrector` CRC | same function, now in `Crc16.hpp` | `test_TcFrameCorrector_Codec.cpp` unmodified and green; `test_Crc16.cpp` check value |
| `rateGroup50Hz` runs only `detumbleManager.run` | + `driverBoardUart.schedIn` at slot 1 | passive driver, ≤ 64 B copy per tick; no change to slot 0 ordering |
| `rateGroup1Hz` slots 0-11, 13-20 | + `driverBoardHandler.run` at slot 12 (free) | no existing member moves; TaskGate untouched |
| `rateGroup10Hz` slots 0-4, 6-13 | + `driverBoardBufferManager.schedIn` at slot 5 (free) | telemetry-only tick |
| uart1 enabled in devicetree, no consumer | consumed by `driverBoardUart` | `state.peripheralUart2` already populated in `Main.cpp:128`; zero devicetree change |
| Board absent (the flight case until integration) | handler sends DISARM once after `uartReady`, then one 7-byte HK_REQUEST per second (default HK_INTERVAL_S = 1) into an unconnected TX pin; LINK_DOWN from boot; **no LinkLost event** (never was UP); no HK; telemetry writes only the static channels once | event throttles; `LinkTimeouts` stays 0; verify with DriverBoardHandler-1 |
| Safe mode entry depowers payload rails | unchanged; handler additionally sends DISARM and logs Disarmed(SAFE_MODE) | 3.6 |
| Dispatch table 361 used of 400 (Cycle D dictionary, ledger) | + 6 commands + 10 param commands = 377; constant raised to 512 | one line, `CommandDispatcherImplCfg.hpp:14`; RAM delta from the memory lines |
| PrmDb persisted entries = 25 (F' default) | **unchanged this cycle** (gated on the persistence bench check, 07) | no RAM delta; parameters are RAM-only until the gate passes |
| Packetizer 22 packets of 22; **214 distinct channels against `TLMPACKETIZER_HASH_BUCKETS = 202`** — a latent boot assert since Cycle D (`TlmPacketizer.cpp` `findBucket` asserts `free < BUCKETS`; every distinct channel in the packet set or the omit block takes a bucket; asserts are compiled in, `FpConfig.h:111`) | 23 of 24 packets; 236 channels of 256 buckets | E1 raises `MAX_PACKETIZER_PACKETS` → 24 **and** `TLMPACKETIZER_HASH_BUCKETS` → 256; E1 adds `scripts/check_packet_set.py` to `verify.sh` (fails at HEAD, passes after); E5 bench check proves the image boots |
| Dictionary | +6 cmds, +10 param cmds, +5 params, +22 channels, +12 events, +1 packet | 06 |
| RAM | + ring buffer 1024 B + 4 x 128 B pool + handler ≈ 300 B + PrmDb delta | target build memory lines, must stay < 70 % |
| Flight default flight behaviour (no board) | identical except one extra 1 Hz passive tick and one 50 Hz drain of an empty ring | — |

Deliberate behaviour additions (none are changes to an existing path): the payload link exists; five parameters; six commands; one packet at group 3.
