# 01 — Normative: results the tests are written from

Names below are the interface. `RD.` = `ReferenceDeployment.` in GDS.

## L1 — face rail ↔ mux channel wiring
- **R1.1** `topology.fpp` connections: `face4LoadSwitch.loadSwitchStateChanged[0..2]` → `tmp112Face5Manager`,
  `veml6031Face5Manager`, `drv2605Face5Manager` `.loadSwitchStateChanged`; `face5LoadSwitch.loadSwitchStateChanged[0..1]`
  → `veml6031Face6Manager`, `tmp112Face6Manager` (L2) `.loadSwitchStateChanged`. No other `loadSwitchStateChanged`
  connection changes (faces 0–3 and the payload switches byte-identical).
- **R1.2** `v5.dtsi` comments on `mux_channel_5` and `mux_channel_6` name the connector and rail (`// F4 connector J1,
  rail F4_PWR (FACE4_ENABLE)`, `// F5 connector J2, rail F5_PWR (FACE5_ENABLE)`); no devicetree *content* changes.
- **R1.3 (Board, HP-02/HP-11 addition)** With every face switch OFF, `RD.face4LoadSwitch.TURN_ON` alone: within 45 s
  `tmp112Face5Manager.Temperature`, `veml6031Face5Manager` light and a `drv2605Face5Manager` `START` all succeed with no
  `DeviceNotReady`; `RD.face5LoadSwitch.TURN_ON` alone: `veml6031Face6Manager` and `tmp112Face6Manager` succeed and the
  channel-5 managers report `DeviceNotReady`. (This is the bench confirmation of the netlist inference.)

## L2 — `tmp112Face6Manager`
- **R2.1** Instance `tmp112Face6Manager`, same component as `tmp112Face5Manager`, configured on `mux_channel_6` with
  `face6_temp_sens` (`v5.dtsi:429`), scheduled where the other face managers are; `thermalManager.faceTempGet[5]` →
  `tmp112Face6Manager.temperatureGet`; `ThermalManager` constant `numFaceTempSensors = 6`.
- **R2.2** `tmp112Face6Manager.Temperature` is in packet `Thermal` (id 12) after `tmp112Face5Manager.Temperature`.
- **R2.3 (Unit)** ThermalManager threshold events report `sensorId` 5 for the sixth face; the sweep calls all six
  `faceTempGet` ports each collection tick; existing five-face behaviour unchanged (existing thermal host tests stay
  green with the constant raised, `FACE_SENSORS` in `test_ThermalManager_CollectionInterval.cpp:38` updated to 6).
- **R2.4** `scripts/check_hardware_consistency.py` HWC-1 reports `18 managers checked, 0 mismatched — OK` (the new
  manager is in the checked set and on the right channel).

## L3 — DEPLOY2
- **R3.1 (Unit)** `Components.Burnwire` with only `gpioSet[0]` connected: `START_BURNWIRE` → `SetBurnwireState(ON)`, first
  `schedIn` tick writes HIGH on port 0 and does **not** assert; `STOP_BURNWIRE` (or the safety timer) writes LOW on port 0.
  With both ports connected the writes are byte-for-byte the existing behaviour (pin: HIGH/LOW on 0 and 1 in the same order).
- **R3.2** Instances `burnwireDeploy2: Components.Burnwire` and `gpioDeploy2: Zephyr.ZephyrGpioDriver`;
  `gpioDeploy2` opened OUT on `DT_NODELABEL(fire_deploy2_b)` (`v5.dtsi:592-596`, MCP23017 GPA2);
  `burnwireDeploy2.gpioSet[0] → gpioDeploy2.gpioWrite`; `gpioSet[1]` unconnected;
  `rateGroup1Hz.RateGroupMemberOut[19] → burnwireDeploy2.schedIn` (slot 19 is free, `topology.fpp:329`).
  `SAFETY_TIMER` default 10 s unchanged. `antennaDeployer` still drives only `burnwire`.
- **R3.3 (Board, HP-11 addition)** `RD.burnwireDeploy2.START_BURNWIRE` into a dummy load on J24: `SetBurnwireState(ON)`
  ≤ 2 s, `ina219Sys` power rises, `STOP_BURNWIRE` → OFF ≤ 2 s and `BurnwireEndCount`; without STOP, OFF at 10 s.

## L4 — CHARGE
- **R4.1** `v5.dtsi:626` `charge` gpios flag `GPIO_ACTIVE_HIGH` → `GPIO_ACTIVE_LOW` with a comment: LT3652 `~CHRG` is
  open-drain, pulled up by R70 (10 k) to +3V3, **low while charging**; logical 1 = charging.
- **R4.2** `PowerMonitor` gains: `output port chargeStatusGet: Drv.GpioRead`; `telemetry Charging: Fw.On update on change`;
  `event ChargeStateChanged(state: Fw.On) severity activity low format "Battery charging: {}"`. On every `run` tick the
  port is read (if connected); `Charging` is ON iff the read is `Fw::Logic::HIGH`; the event fires only when the value
  changes and once on the first read. Existing PowerMonitor commands, channels, events and the collection-interval
  behaviour are unchanged; with the port unconnected nothing is read and nothing new is emitted.
- **R4.3** Instance `gpioCharge: Zephyr.ZephyrGpioDriver` opened IN on `DT_NODELABEL(charge)`;
  `powerMonitor.chargeStatusGet → gpioCharge.gpioRead`. `powerMonitor.Charging` in packet `PowerMonitor` (id 11).
- **R4.4 (Board)** With a supply on VSOLAR above the pack voltage, `Charging` ON within 2 s; supply removed, OFF within 2 s;
  one `ChargeStateChanged` per transition.

## Harm table
| Existing behaviour | Observation |
|---|---|
| Faces 0–3 and payload switch wiring | those `topology.fpp` lines byte-identical |
| `burnwire` (DEPLOY1 A/B) and `antennaDeployer` | `burnwire_test.py`, `antenna_deployer_test.py` unchanged and collected; host pin R3.1 both-ports case |
| PowerMonitor totals and interval | `test_PowerMonitor_*` green; `power_monitor_test.py` collected |
| Thermal thresholds | `test_ThermalManager_Thresholds` green |
| Dictionary | commands +2 (`burnwireDeploy2.START/STOP_BURNWIRE`), channels +2, events +≈5 (Burnwire ×4, PowerMonitor ×1) — recorded |
| HWC-1 / HWC-2 | still OK; HWC-1 count 17 → 18 |

## Non-goals
Heater (excluded); `drv2605Face6` manager (PROVES hardware SCALAR does not fly, `C-08` note); S-band; `5V_RESET`;
runtime expander/mux reset; renaming `ina219Sys`; any change to `LoadSwitch`, `Tmp112Manager`, `Veml6031Manager` code.
