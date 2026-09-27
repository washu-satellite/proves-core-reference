# 04 — Findings (verified facts, file:line) — 2026-09-27, tree fix/c08-c11 @ 6296ef32

1. **Mux channel ↔ connector (netlist S8, U3 TCA9548A).** SD/SC0 → `F0_SDA/SCL` … SD/SC3 → `F3_*`; SD/SC4 → `BATT_SDA/SCL`;
   **SD/SC5 → `F4_SDA/SCL`; SD/SC6 → `F5_SDA/SCL`**; SD/SC7 → `SDA_Top/SCL_Top`. Face connectors: J6 F0, J9 F1, J11 F2,
   J13 F3, **J1 F4, J2 F5**, each `1,2 VSOLAR / 3 GND / 4 F<n>_PWR / 5 F<n>_SCL / 6 F<n>_SDA`. So the devices the
   devicetree puts on `mux_channel_5` ("Z- Face x1", `v5.dtsi:386`) are on the F4 connector/rail and `mux_channel_6`
   ("Z- Face x2", `:422`) on F5. **[model]** `SCALAR_PowerRails.face4Rail` ("powered, unsensed") and `C-07`/`Q7` in the
   model repo are wrong on this point: face 4's rail powers the channel-5 sensors.
2. **Topology wiring today.** `face4LoadSwitch` has only `gpioSet/gpioGet` (`topology.fpp:340-341`);
   `face5LoadSwitch.loadSwitchStateChanged[0..2]` → the channel-5 managers (`:369-371`); `veml6031Face6Manager` is read by
   `adcs.visibleLightGet[5]` (`:492`) and has no `loadSwitchStateChanged` source; `face6_temp_sens` / `face6_drv2605`
   (`v5.dtsi:429,443`) have no `DEVICE_DT_GET` and no manager (`Main.cpp:40-60`).
3. **Effect of the mis-wiring is bounded.** `Tmp112Manager::temperatureGet_handler` initialises lazily
   (`Tmp112Manager.cpp:57`), `loadSwitchStateChanged_handler` de-initialises on OFF (`:37-43`); Veml6031Manager likewise
   (`Veml6031Manager.cpp:36-42, 56`). So a manager told OFF by the wrong switch re-initialises on its next read — the
   symptom is `DeviceNotReady` noise and one lost sample per wrong OFF, not a dead sensor. Still wrong.
4. **ThermalManager** `numFaceTempSensors = 5` (`ThermalManager.fpp:32`), `faceTempGet[4] → tmp112Face5Manager`
   (`topology.fpp:478`); the sweep at `ThermalManager.cpp:65` calls every port with no `isConnected` guard;
   `test_ThermalManager_CollectionInterval.cpp:38` pins `FACE_SENSORS = 5`.
5. **Burnwire** writes both `gpioSet` ports unconditionally (`Burnwire.cpp:44-45, 58-59`); `SAFETY_TIMER` default 10
   (`Burnwire.fpp:42`); `burnwire0/1` = GPIO28/29 = nets `FIRE_DEPLOY1_A/B` (netlist); `antennaDeployer` drives it
   (`topology.fpp:386-387`). `rateGroup1Hz` slot 19 is free (`:329`).
6. **TPS4H160 channel map (netlist U6).** OUT1 `DEPLOY1`, OUT2 `DEPLOY1_AUX` (J23 pins 1/3), OUT3 `DEPLOY2` (J24 pins 1/2)
   ← `Deploy2_EN` ← R103 4.7 k ← `FIRE_DEPLOY2_B` (U9 GPA2); OUT4 `Heater Output` ← `Heater_EN` ← R100 4.7 k ←
   `ENABLE_Heater` (U9 GPA0).
7. **CHARGE.** `~{CHARGE}` = IC6.4 `~CHRG` + R70.2 (R70 = 10 k, other end +3V3) + U9.8 GPB7; `v5.dtsi:624-628` declares it
   `GPIO_ACTIVE_HIGH` under `gpio-keys`. `ZephyrGpioDriver.hpp:24-27` `GpioConfiguration { IN, OUT }`. `PowerMonitor.fpp`
   has no GPIO port today; `powerMonitor.run` is on `rateGroup1Hz[15]` via `taskGate` (`topology.fpp:320-321`).
8. **Packets.** `PowerMonitor` id 11 group 2 (`ReferenceDeploymentPackets.fppi:57-64`); `Thermal` id 12 group 3 (`:91-…`,
   `tmp112Face5Manager.Temperature` at `:96`); `LoadSwitches` id 10 group 3; Burnwire has no telemetry channels.
   Distinct channels today 244/256 (Cycle J gate).
