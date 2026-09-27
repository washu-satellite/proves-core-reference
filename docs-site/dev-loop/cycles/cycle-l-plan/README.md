# Cycle L plan — EPS channels commandable and correctly wired (index) — PLAN ONLY, not yet approved

Written 2026-09-27 against `fix/c08-c11` @ 6296ef32. No code, tests or commits. `$R`, `$PY`, `P`, `B` as in Cycle J.
**Heater excluded** by Jesse (2026-09-27): whether a heater is fitted or flies is undecided (model `A5`), so
`enable_heater` (`v5.dtsi:580-584`, TPS4H160 OUT4) gets no software in this cycle.

**What this cycle delivers**, four rows, each a copy of a pattern the tree already has:

| Row | What | Blocks today |
|---|---|---|
| **L1** | Face rail ↔ mux channel wiring: the channel-5 managers hang off `face4LoadSwitch`, the channel-6 managers off `face5LoadSwitch` | Every face test on the F4/F5 connectors: the software tells the wrong managers when their rail switches |
| **L2** | `tmp112Face6Manager` for `face6_temp_sens` (mux channel 6 = F5 connector); ThermalManager sweeps 6 faces | The Z-face second sensor site is unreadable (`C-07`) |
| **L3** | DEPLOY2 commandable: `Burnwire` tolerates an unconnected second output; a second instance drives `fire_deploy2_b` | TPS4H160 OUT3 (`DEPLOY2`, J24) cannot be commanded |
| **L4** | CHARGE readable: `charge` polarity corrected in the devicetree; PowerMonitor reads it every tick and reports `Charging` | LT3652 charge state invisible; the charge test has no telemetry oracle |

**The finding behind L1** (netlist S8, `FC_V5e_Production_Rev1.net.xml`, U3 TCA9548A): SD/SC0–3 → `F0..F3_SDA/SCL`,
SD/SC4 → `BATT_*`, **SD/SC5 → `F4_SDA/SCL`, SD/SC6 → `F5_SDA/SCL`**, SD/SC7 → `SDA_Top/SCL_Top`. Each face connector
carries its rail and its I2C pair on the same plug (J1 = `F4_PWR` + `F4_SCL/SDA`; J2 = `F5_PWR` + `F5_SCL/SDA`,
`sc_ports.csv`/model `SCALAR_PowerRails`), so the devices on mux channel 5 are powered by `FACE4_ENABLE` and those on
channel 6 by `FACE5_ENABLE`. The topology wires `face5LoadSwitch → {tmp112,veml6031,drv2605}Face5Manager` (channel 5)
(`topology.fpp:367-371`) and gives `face4LoadSwitch` no consumers (`:340-341`) and `veml6031Face6Manager` no switch at all.
The devicetree comments (`v5.dtsi:386 "Z- Face x1"`, `:422 "Z- Face x2"`) name sites, not rails, which is how it went
unnoticed. **Assumption to confirm at Stage 2 bench:** "same connector ⇒ same rail" is inferred from the flight-board
netlist; the face-board schematics are not in the source set. Turning `face4LoadSwitch` on alone and reading the
channel-5 sensors is the confirmation (HP-02 style).

| File | Read it when |
|---|---|
| `01-normative.md` | writing the tests — names, ports, channels, events, defaults, the observable of each row, harm table |
| `02-requirements.md` | adding rows — which component tables, which IDs, the `req.py` commands |
| `03-advisory.md` | coding — exact edit sites, the port-guard shape, packet placement, base-id rule, risks |
| `04-findings.md` | merging into the ledger — verified facts (file:line) |

**Hazards:** Cycle I, Cycle J tooling and Cycle K are all uncommitted in the tree at the time of writing; their paths are
frozen for this cycle. `.fpp` changes ⇒ `fprime-util generate --force` in the build copy before the target build;
dictionary counts change (record them). Channel headroom 244/256 → ~246 (`check_capacity.py` WARN stays; not a FAIL).
