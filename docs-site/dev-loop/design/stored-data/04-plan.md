# 04: Delivery plan, budget, open decisions

Each phase is one `cdh-cycle` run: plan → review → `feat` → `test` → `docs(requirements)` commits, `VERIFY_ENV=host
scripts/verify.sh` green, target build from the clean-path copy (issue #3).

## Phases

Re-baselined 2026-09-27 against `main` @ 6296ef32. The phases map onto commit rows A8-0..A8-7 in
`../../cycles/commit-plan-E-A8-A9.md` (branch `feat/data-recorder`); the row is the unit of work, gate and revert.

| Phase | Rows | Deliverable | Gate | Needs a board? |
|---|---|---|---|---|
| 0 | A8-0 | This design (stale references fixed 2026-09-27, this commit); `req.py` updates: DataRecorder-1..9 rows, criteria for DH-L2-03/04/05/08/12 and CDH-16, reason texts "needs TelemetryStore design" → "DataRecorder design (docs-site/dev-loop/design/stored-data)" | RTM regenerates without warnings | no |
| 1 | A8-1 | `SegmentCodec` + `PacketRing` (F´-free) with host tests: byte layout, every single-byte corruption and truncation, drop-oldest, flush trigger. Slot size follows `FW_COM_BUFFER_MAX_SIZE` = 227 from the header, not a literal | `test_DataRecorder_Codec.cpp` green (host gate) | no |
| 2a | A8-2 | `DataRecorder` component **unwired** (not in the image): `.fpp/.hpp/.cpp`, sdd, config PersistedRecord with room for a third stream, recorder stub, `Os::Directory` fake against the b14101dd `DirectoryInterface`; host tests against recorder stubs + `Os::File`/`FileSystem` fakes | host gate; target compile from the build copy | no |
| 2b | A8-3 (**activation row**) | Constants first: `MAX_PACKETIZER_CHANNELS` 256 → ≥ 288 (`TlmPacketizerCfg.hpp:21`). Then tap wiring on both splitters (`comSplitterTelemetry.comOut[2]`, `comSplitterEvents.comOut[2]`), **1 Hz slot 21**, +7 channels into the `FileSystem` packet (no new packet) | target compile; `scripts/check_capacity.py --dictionary <build copy dictionary>` reads +9 commands / `check_packet_set.py` +14 channels; RAM ≤ 70 %; FLASH/RAM deltas in the ledger | **yes**: ROADMAP rule 3, gated on the bench milestone (`f-bench-ok`) |
| 3 | A8-4, A8-5, A8-6 | `tools/recorder_reader.py` (decodes `telemetryPacketSets[0].members`-era dictionaries); per-pass sequence `sequences/pass_recorder.seq`; int test `data_recorder_test.py` (segment written, downlinked, decodes; event before reset retrievable); HP-12 extended with a segment power-cut loop; HP-13 extended; matrix, ledger, component page | ruff; reader round-trips a host-generated segment; int collect; RTM | rows A8-4/A8-6 no; A8-5's board rows stay ⏸ deferred until a runner exists |
| 4 (decision) | A8-7, separate branch | Inline mode: `tlmSend.PktSend → dataRecorder.tlmIn`, `dataRecorder.tlmOut → comSplitterTelemetry.comIn` with a `DOWNLINK_GROUP_MASK` so the vehicle records more than it sends; optionally move the RF-silence gate here | Only after open decisions 1-2 below | yes |

Order of work while no board is on a cable: A8-0, A8-1, A8-2, A8-4 and the host halves of A8-5/A8-6 can all land now, each
standalone and revertible; only A8-3 waits, and it is the one commit that changes the image. A9 (`feat/burst-capture`) starts
after A8-3 is merged.

## Harm table for Phase 2 (must not hurt the current system)

| Existing path | Change | Why identical by default |
|---|---|---|
| `tlmSend.PktSend → comSplitterTelemetry → comQueue` | none; one more splitter output | `ComSplitter` invokes outputs in order; the new sync handler is a `memcpy` of ≤ 233 B |
| `events.PktSend → comSplitterEvents → comQueue` | same | same |
| 1 Hz rate group | slot 21 added, after `faultManager.run` (slot 20) | members run in index order, so the recorder's SD write follows the fault decision and precedes nothing; flush is ≤ one `write` + `flush` of ≤ 32 KB per tick; measure on target and cap `FLUSH_RECORDS` if a tick slips (`RateGroupCycleSlip`) |
| SD NAND (4 GB, soldered) | new directory `/rec/` | `FsSpace.FreeSpace` reflects it; capacity default 10 MB total (18 MB with A9) leaves the medium essentially empty; PersistedRecord files untouched. SPI0 is shared with the unwired S-band module (C-31): no contention today; revisit the flush bound if S-band is ever driven |
| Dictionary | +9 commands, +14 channels, +7 events | dispatch table 512 vs 387 used (Cycle E raised it); packet count unchanged at 23 of 24; channel limit raised to ≥ 288 in the same row *before* the channels are added, because the packetizer `FW_ASSERT`s at boot when the map is full and the target build does not check it |

## Budget

Measured after Cycle F (`feat/upstream-sync` @ f26d9e1d, 2026-09-19); the pre-sync figures (RAM 342976 B / 64.41 %, FLASH
715104 B / 68.49 %, 361 opcodes of 400, 20 of 25 1 Hz members) are superseded.

| Resource | Now | Added | After |
|---|---|---|---|
| RAM | 331376 B (62.23 %) | 2 rings × 32 × 229 B ≈ 14.7 KB + `MAX_PACKETIZER_CHANNELS` +32 (≈ 130 B each at 24 packets ≈ 4 KB) + segment write staging 0 (ring bytes are written in place) + component ≈ 1 KB | ≈ 66 %, under the 70 % guard; A9's third ring (+7.3 KB) lands ≈ 67.5 % |
| FLASH | 725984 B (69.53 % of the 1 MB slot) | codec + component, estimate 8 to 12 KB | ≈ 71 % |
| 1 Hz tick | 18 of 25 members, highest index 20 | +1 (slot 21) | 19 of 25, highest index 21; A9 takes 22 |
| Opcodes | 387 of 512 | +9 | 396 of 512 |
| Packets | 23 of 24 | 0 | 23 of 24 (A9's `BurstStatus` takes 24) |
| Channels | 244 of 256 | +14, limit → 288 | 258 of 288 |
| Parameters | 106, all RAM-only | 0 (config is a PersistedRecord) | unchanged; A8 is not on the persistence gate |

## Open decisions (owner: user)

1. **Record during RF silence?** Today TelemetryGate DISABLED stops packet generation, so nothing is recorded. Moving the gate
   to the recorder's forward path keeps CH-L2-15 (cease transmission within one cycle) but changes CH-L2-17's wording
   ("inhibit telemetry scheduling"). Recommendation: do it in Phase 4, and reword CH-L2-17 to "inhibit telemetry transmission".
2. **Record level vs downlink level.** Phases 1 to 3 record what `tlmSend` emits at its current packet level. To record group-2/3
   housekeeping without also sending it on LoRa, Phase 4's inline mask is needed. Recommendation: accept the tap for now; ops
   raise the packet level when history matters more than link budget.
3. ~~**Capacity defaults.** 8 MB tlm / 2 MB evt assume a card of ≥ 1 GB; confirm the flight card size and set `RESERVE_BYTES`.~~
   **Resolved 2026-09-08 by S8:** the medium is a soldered SD NAND, ZDSD04GLGEAG, about 4 GB by part number (inferred). The
   defaults stand (8 MB / 2 MB / 8 MB burst) and can be raised later; `RESERVE_BYTES` is set once `fsSpace.TotalSpace` has been
   read from a running board (an existing channel: a GDS command during the bench milestone, not code).
4. **Ground reader home.** `tools/` in this repo, or the GDS plugin repo. Recommendation: `tools/`, next to the existing
   scripts, because it depends only on the deployment dictionary.

## Non-goals

- Replacing comQueue priorities or adding per-packet priority tags (DH-L2-06 clause 2): separate design.
- Payload-side storage (payload has its own MCU).
- Wear levelling or bad-block handling beyond what the card's controller does.
