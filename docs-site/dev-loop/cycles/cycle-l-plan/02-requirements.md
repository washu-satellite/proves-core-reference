# 02 — Requirement rows

Rows live in the owning component's `docs/sdd.md` table and are added with `req.py add --group "<group>"`; the test author
runs `$PY scripts/req.py list --group <group>` first and takes the next unused number. Methods/levels: Unit rows → host
gtest with `RecordProperty("verifies", …)`; Board rows → `test/int` pytest (collected and linted only) and an HP step.

| Row | Group (see `req.py list`) | Proposed ID | Level | Criterion (one sentence each; the author expands per §01) |
|---|---|---|---|---|
| L3 | Burnwire (`BW`) | next `BW-nnn` | Unit | Only-port-0-connected instance never invokes port 1; both-connected order unchanged (R3.1) |
| L3 | Burnwire (`BW`) | next `BW-nnn` | Board | `burnwireDeploy2` START/STOP into a dummy load (R3.3) |
| L4 | PowerMonitor (`PWR-MON-REQ`) | next `PWR-MON-REQ-nnn` | Unit | `Charging` follows `chargeStatusGet`, event on change and first read, nothing when unconnected (R4.2) |
| L4 | PowerMonitor (`PWR-MON-REQ`) | next `PWR-MON-REQ-nnn` | Board | R4.4 |
| L2 | ThermalManager (`Face Temperature Collection` group as listed by `req.py`) | next | Unit | six face ports called each collection tick; `sensorId` 5 in events (R2.3) |
| L1 | Hardware Consistency (`HWC`) | `HWC-3` | Board | R1.3 — the bench confirmation; Method Integration Test |

`HWC-3` goes in `docs-site/requirements/hardware-consistency.md` (Cycle J file; its Cycle J pin is historical once
Cycle J's tooling is committed — if that has not happened when this cycle runs, put the row in a new `bench.md`-style
file instead and say so in the review).
