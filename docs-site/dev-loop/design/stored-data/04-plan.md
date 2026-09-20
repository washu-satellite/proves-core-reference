# 04: Delivery plan, budget, open decisions

Each phase is one `cdh-cycle` run: plan → review → `feat` → `test` → `docs(requirements)` commits, `VERIFY_ENV=host
scripts/verify.sh` green, target build from the clean-path copy (issue #3).

## Phases

| Phase | Deliverable | Gate |
|---|---|---|
| 0 | This design; `req.py` updates: DataRecorder-1..9 rows, criteria for DH-L2-03/04/05/08/12 and CDH-16, reason texts "needs TelemetryStore design" → "DataRecorder design (docs-site/dev-loop/design/stored-data)" | RTM regenerates without warnings |
| 1 | `SegmentCodec` + `PacketRing` (F´-free) with host tests: byte layout, every single-byte corruption and truncation, drop-oldest, flush trigger | `test_data_recorder_codec.cpp` green |
| 2 | `DataRecorder` component, tap wiring on both splitters, 1 Hz slot 20, channels in the `FileSystem` packet, config record, host tests against recorder stubs + `Os::File`/`FileSystem` fakes + new `Os::Directory` fake; target build; dictionary check | host suite green; FLASH/RAM deltas recorded in the ledger |
| 3 | `tools/recorder_reader.py`; per-pass sequence; int tests `data_recorder_test.py` (segment written, downlinked, decodes; event before reset retrievable); HP-12 extended with a segment power-cut loop; HP-13 extended | int collect + lint green; board rows stay ⏸ deferred until a runner exists |
| 4 (decision) | Inline mode: `tlmSend.PktSend → dataRecorder.tlmIn`, `dataRecorder.tlmOut → comSplitterTelemetry.comIn` with a `DOWNLINK_GROUP_MASK` so the vehicle records more than it sends; optionally move the RF-silence gate here | Only after the open decisions below |

## Harm table for Phase 2 (must not hurt the current system)

| Existing path | Change | Why identical by default |
|---|---|---|
| `tlmSend.PktSend → comSplitterTelemetry → comQueue` | none; one more splitter output | `ComSplitter` invokes outputs in order; the new sync handler is a `memcpy` of ≤ 233 B |
| `events.PktSend → comSplitterEvents → comQueue` | same | same |
| 1 Hz rate group | slot 20 added, after `fsSpace` | flush is ≤ one `write` + `flush` of ≤ 32 KB per tick; measure on target and cap `FLUSH_RECORDS` if a tick slips (`RateGroupCycleSlip`) |
| SD card | new directory `/rec/` | `FsSpace.FreeSpace` reflects it; capacity default 10 MB total leaves the card mostly free; PersistedRecord files untouched |
| Dictionary | +9 commands, +14 channels, +7 events | dispatch table 400 vs 361 used; packet count unchanged |

## Budget

| Resource | Now | Added | After |
|---|---|---|---|
| RAM | 342976 B (64.41 %) | 2 rings × 32 × 235 B ≈ 15 KB + segment write staging 0 (ring bytes are written in place) + component ≈ 1 KB | ≈ 67.5 % |
| FLASH | 715104 B (68.49 %) | codec + component, estimate 8 to 12 KB | ≈ 70 % |
| 1 Hz tick | 20 members | +1 | 21 of 25 |
| Opcodes | 361 | +9 | 370 of 400 |

## Open decisions (owner: user)

1. **Record during RF silence?** Today TelemetryGate DISABLED stops packet generation, so nothing is recorded. Moving the gate
   to the recorder's forward path keeps CH-L2-15 (cease transmission within one cycle) but changes CH-L2-17's wording
   ("inhibit telemetry scheduling"). Recommendation: do it in Phase 4, and reword CH-L2-17 to "inhibit telemetry transmission".
2. **Record level vs downlink level.** Phases 1 to 3 record what `tlmSend` emits at its current packet level. To record group-2/3
   housekeeping without also sending it on LoRa, Phase 4's inline mask is needed. Recommendation: accept the tap for now; ops
   raise the packet level when history matters more than link budget.
3. **Capacity defaults.** 8 MB tlm / 2 MB evt assume a card of ≥ 1 GB; confirm the flight card size and set `RESERVE_BYTES`.
4. **Ground reader home.** `tools/` in this repo, or the GDS plugin repo. Recommendation: `tools/`, next to the existing
   scripts, because it depends only on the deployment dictionary.

## Non-goals

- Replacing comQueue priorities or adding per-packet priority tags (DH-L2-06 clause 2): separate design.
- Payload-side storage (payload has its own MCU).
- Wear levelling or bad-block handling beyond what the card's controller does.
