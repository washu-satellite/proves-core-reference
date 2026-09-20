# ComCcsdsLora

This is a clone with a renamed module of the F Prime's Svc::ComCcsds.

See: [Svc::ComCcsds Documentation](https://github.com/nasa/fprime/tree/devel/Svc/Subtopologies/ComCcsds)

## Inspection records

Downlink queue service order, overflow behaviour and discard policy are
properties of the vendored `Svc::ComQueue` and of this subtopology's
configuration, not of any component in `Components/`. `Svc::ComQueue` is not
host-buildable (`lib/fprime/Svc/ComQueue/ComQueue.hpp:10-16` pulls in
`Fw/Buffer`, `Fw/Com/ComBuffer`, the autocoded base, `Utils/Types/Queue` and
`Os/Mutex`), and the `.fpp` constants are autocoded rather than includable, so
these clauses are recorded by inspection.

| ID | Clause | Evidence | Date |
|---|---|---|---|
| DH-L2-06 | comQueue serves EVENTS(0) before FILE(1) before TLM(2) when all are non-empty | Priorities `events = 0`, `file = 1`, `tlm = 2` in `project/config/ComCcsdsConfig.fpp:30-34`, applied to this subtopology's `QueueConfigurationTable` at `ComCcsdsLora/ComCcsds.fpp:14-25` (the UART subtopology mirrors it). `ComQueue::configure` sorts the queue metadata by priority (`lib/fprime/Svc/ComQueue/ComQueue.cpp:64-90`) and `ComQueue::processQueue` sends the first non-empty queue in that sorted order (`ComQueue.cpp:333-374`), round-robining only within one priority level (`ComQueue.cpp:376-386`). | 2026-09-05 |
| DH-L2-06 | Commandable per-packet priority tags | **Not implemented.** `ComCfg.FrameContext` carries `comQueueIndex, apid, sequenceCount, vcId, authenticated` and no priority field (`project/config/ComCfg.fpp:38-49`); a tag would need a new context field plus a `Svc::ComQueue` behaviour change under `lib/`. Out of scope. | 2026-09-05 |
| DH-L2-09 | When a comQueue is full the next packet is dropped and exactly one QueueOverflow event is emitted for that queue (latched) | `ComQueue::enqueue` (`ComQueue.cpp:246-271`): a `FW_SERIALIZE_NO_ROOM_LEFT` from the backing queue drops the *incoming* message — nothing already queued is evicted — and logs `QueueOverflow` only while `m_throttle[queueNum]` is clear, setting it (`:257-262`). The throttle is cleared when that queue next successfully sends (`ComQueue.cpp:369`), so one event per overflow episode per queue. Buffer-port async overflow is separate and returns the buffer rather than dropping it silently (`ComQueue::bufferQueueIn_overflowHook`, `ComQueue.cpp:237-240`). | 2026-09-05 |
| DH-L2-09 | FreeSpace never reaches 0 in a 70 s run | **Board-deferred.** Requires a running target; no host observation is possible. | 2026-09-05 |
| DH-L2-10 | Discard policy is drop-newest with a latched QueueOverflow warning; depths events 50, tlm 1, file 1 | Drop-newest: same code path as DH-L2-09, `ComQueue.cpp:246-271` — the failed `enqueue` returns false and the caller's message is discarded, leaving the queued backlog intact. Severity is `WARNING_HI` (`log_WARNING_HI_QueueOverflow`, `ComQueue.cpp:259`). Depths `events = 50`, `tlm = 1`, `file = 1` in `project/config/ComCcsdsConfig.fpp:24-28`, applied at `ComCcsdsLora/ComCcsds.fpp:14-25`. | 2026-09-05 |

## Change Log
| Date | Description |
|---| --- |
|Sep 2026| Added inspection records for DH-L2-06 (clause 1), DH-L2-09 (clause 1) and DH-L2-10 |
