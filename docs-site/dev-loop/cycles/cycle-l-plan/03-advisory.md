# 03 — Advisory: edit sites and method (the coder may deviate if every result in 01/02 holds)

## L1 wiring (`P/ReferenceDeployment/Top/topology.fpp`)
- `:340-341` keep `face4LoadSwitch.gpioSet/gpioGet`; add three `face4LoadSwitch.loadSwitchStateChanged[0..2] →
  tmp112Face5Manager / veml6031Face5Manager / drv2605Face5Manager.loadSwitchStateChanged`.
- `:369-371` change the three `face5LoadSwitch.loadSwitchStateChanged[…]` targets to `veml6031Face6Manager` ([0]) and
  `tmp112Face6Manager` ([1]); leave [2] unconnected (LoadSwitch guards its fan-out? — check `LoadSwitch.cpp`; if it
  invokes all three unconditionally, connect [2] to `tmp112Face6Manager` is wrong — instead keep the port array at 3 and
  point [2] at a manager that tolerates a duplicate ON, or add an `isConnected` guard in `LoadSwitch.cpp` as L3 does for
  Burnwire. Verify before choosing; record in the report.)
- Devicetree comments only at `v5.dtsi:386` and `:422`.

## L2 `tmp112Face6Manager`
- `instances.fpp`: copy the `tmp112Face5Manager` line with the next free base id in file order.
- `Main.cpp:44` add `face6_temp_sens = DEVICE_DT_GET(DT_NODELABEL(face6_temp_sens));` and the `inputs.face6TempDevice`
  assignment next to `:113`; `ReferenceDeploymentTopologyDefs.hpp` add the field.
- `ReferenceDeploymentTopology.cpp:164` add `tmp112Face6Manager.configure(state.tca9548aDevice, state.muxChannel6Device,
  state.face6TempDevice, true);`
- `topology.fpp`: rate-group membership where `tmp112Face5Manager` is scheduled (grep it — the face managers are
  scheduled through `thermalManager`'s sweep, so possibly no rate-group line is needed); `thermalManager.faceTempGet[5]`
  (`:478`).
- `ThermalManager.fpp:32` `numFaceTempSensors = 6`. `ThermalManager.cpp:65` loops to the constant with no
  `isConnected` guard — all six must be connected (they will be).
- `ReferenceDeploymentPackets.fppi:96` add `tmp112Face6Manager.Temperature`.
- Existing test `test_ThermalManager_CollectionInterval.cpp:38` `FACE_SENSORS = 5` → 6 (Stage 3b author's edit; note it
  in the review as an existing-test change, not a Stage 3b test).

## L3 DEPLOY2
- `Burnwire.cpp:44-45, 58-59`: wrap each `gpioSet_out(i, …)` in `if (this->isConnected_gpioSet_OutputPort(i))`. Four
  sites, no other change; default behaviour identical when both are connected.
- `instances.fpp`: `burnwireDeploy2: Components.Burnwire`, `gpioDeploy2: Zephyr.ZephyrGpioDriver`, next free base ids.
- `ReferenceDeploymentTopology.cpp`: `static const struct gpio_dt_spec deploy2Gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(fire_deploy2_b), gpios);`
  next to `:20`; `gpioDeploy2.open(deploy2Gpio, …::OUT);` next to `:85`.
- `topology.fpp`: `rateGroup1Hz.RateGroupMemberOut[19] → burnwireDeploy2.schedIn` (replace the "slot 19 is free" comment);
  `burnwireDeploy2.gpioSet[0] → gpioDeploy2.gpioWrite` in `connections BurnwireGpio`.
- Hardware path for the bench step: GPA2 → R103 (4.7 k) → U6 pin 6 `Deploy2_EN` → OUT3 (pins 17/18) → `DEPLOY2` on J24
  pins 1–2 (netlist). Dummy load on J24, never a real deployment wire.

## L4 CHARGE
- `v5.dtsi:626` flag + comment. Netlist: `~{CHARGE}` = IC6.4 (`~CHRG`) + R70.2 (R70 = 10 k to +3V3) + U9.8 (GPB7): a
  pull-up exists, so no `GPIO_PULL_UP` is needed; polarity is the only fix.
- `PowerMonitor.fpp`: port, channel, event per R4.2; `PowerMonitor.cpp` `run_handler`: after the existing sampling,
  `if (this->isConnected_chargeStatusGet_OutputPort(0)) { Fw::Logic v; this->chargeStatusGet_out(0, v); … }` — keep a
  `bool m_chargeKnown` / `Fw::On m_charging` pair (declare new members last, `-Wreorder`).
- `instances.fpp`: `gpioCharge: Zephyr.ZephyrGpioDriver`; `ReferenceDeploymentTopology.cpp`: `GPIO_DT_SPEC_GET(DT_NODELABEL(charge), gpios)`
  and `gpioCharge.open(…, GpioConfiguration::IN)` (`ZephyrGpioDriver.hpp:24-27` has `IN`).
- `topology.fpp` `connections sysPowerMonitor` (`:464-471`): add `powerMonitor.chargeStatusGet → gpioCharge.gpioRead`.
- `ReferenceDeploymentPackets.fppi:57-64` add `powerMonitor.Charging`.

## Build and gates
Every row touches `.fpp` → in the build copy `fprime-util generate --force` then `fprime-util build`; record FLASH/RAM
and dictionary counts (baseline in `cycle-j-review.md` §Stage 4/5). `check_capacity.py`: channels → ~246/256 (WARN,
not FAIL); `ActiveRateGroupOutputPorts[rateGroup1Hz]` gains slot 19 — check its limit line. HWC-1 count becomes 18.

## Open risks
- `LoadSwitch.cpp` fan-out behaviour with an unconnected `loadSwitchStateChanged[2]` (see L1 note) — resolve before
  writing the L1 test.
- The "same connector ⇒ same rail" inference (README) — if the Stage 2 bench shows channel-5 sensors answering with only
  `face5LoadSwitch` ON, L1 is reverted and the netlist reading re-examined; nothing else in the cycle depends on it.
- Which rail powers the top-cap board (mux channel 7, `SDA_Top/SCL_Top`, J16 pin 6 = +3V3) — unknown; `veml6031Face7Manager`
  keeps no switch, as today.
