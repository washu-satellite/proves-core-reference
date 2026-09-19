# 08 — Findings (verified facts, file:line, 2026-09-17, S3 @ cab7439)

- `boards/.../proves_flight_control_board_v5.dtsi:121-133`: `uart0` and `uart1` both `status = "okay"`, `current-speed = <115200>`, pinctrl `UART0_TX_P0` / `UART1_TX_P4`. v5e overlay does not touch them.
- `ReferenceDeployment/Main.cpp:26-27`: `peripheral_uart = uart0`, `peripheral_uart1 = uart1`; `:125-128` sets `inputs.peripheralUart/BaudRate` and `inputs.peripheralUart2/BaudRate2 = 115200`. `TopologyDefs.hpp:120-125` declares all four. **`peripheralUart2` is consumed nowhere** (`grep -rn peripheralUart2 Top/*.cpp` → 0).
- S8 netlist (`~/scalar/sources/FC_V5e_Production_Rev1.net.xml`): TX1 = U18.7 (GPIO4) = J18.9; RX1 = U18.8 (GPIO5) = J18.10; TX0/RX0 = J18.5/6 and J3.5/4; PAYLOAD_PWR = J18.1-2 and J5.2; PAYLOAD_BATT = J18.7-8; J18 carries no I2C.
- `Components/PayloadCom/PayloadCom.cpp:75-79`: sends the literal `"<MOISES>\n"` on every received buffer — camera-specific behaviour in the transport component. `PayloadCom.fpp:2` says "Barebones UART communication layer for payload (Nicla Vision camera)".
- `lib/fprime-zephyr/fprime-zephyr/Drv/ZephyrUartDriver/ZephyrUartDriver.hpp:17,25`: `RING_BUF_SIZE 1024`, `SERIAL_BUFFER_SIZE 64`. `.cpp:49-52` IRQ-driven RX into the ring; `:92-100` `schedIn_handler` allocates one `SERIAL_BUFFER_SIZE` buffer per tick, drains up to that, deallocates if empty; `:111` TX is `uart_poll_out` per byte, synchronous; `:75` a full ring drops bytes uncounted.
- `lib/fprime/Drv/Interfaces/ByteStreamDriver.fpp`: interface ports `ready`, `$recv`, `$send` (guarded, caller retains buffer), `recvReturnIn` (guarded).
- `topology.fpp:242-251,264`: `comDriver` direct wiring pattern to `ComCcsdsUart.comStub`.
- Rate-group slots in use: 1 Hz 0-11, 13-20 (**12 free**); 10 Hz 0-4, 6-13 (**5 free**); 50 Hz 0 only (**1 free**).
- Highest base id in `instances.fpp`: `0x1007C000` (faultManager). Next free: `0x1007D000`.
- `instances.fpp:134-154`: `payloadBufferManager` bins `{4096, 2}`, manager id 1, store id 0, allocator `ComCcsds::Allocation::memAllocator`.
- `ModeManager.fpp:58`: `output port modeChanged: [1]`; `:48` `sync input port getMode` (no array). `topology.fpp:385-386` wires both to DetumbleManager only. `:506-507` `loadSwitchTurnOff[6..7]` → payload switches (turn-on `:497-498` commented out, issue #7).
- `Components/FaultTypes/FaultTypes.fpp`: `FaultType` 1..8 double as U8 mask bits; `FaultInPorts = 4`; `FaultReport` port returns `FaultDisposition`.
- `project/config/TlmPacketizerCfg.hpp:19`: `MAX_PACKETIZER_PACKETS = 22`; `:40` `PACKET_UPDATE_ON_CHANGE`. Packet ids in use: 1-22 (`ReferenceDeploymentPackets.fppi`).
- `project/config/CommandDispatcherImplCfg.hpp:14`: 400. Last built dictionary (build copy, pre-Cycle D): 347 commands, 91 params, 191 channels; Cycle D added 10 commands / 4 params.
- `project/config/FpConstants.fpp:20`: `FW_COM_BUFFER_MAX_SIZE = 233`. `ComCfg.fpp:15-18`: `TmFrameFixedSize = 248`, aggregation area 233, Space Packet header 6. `Drv/LoRa/LoRa.hpp:19`: `MAX_PACKET_SIZE = 252`. `TlmPacketizer.cpp:64`: packet header = descriptor + `Fw::Time::SERIALIZED_SIZE` (2+1+4+4 = 11) + packet id.
- `Components/TcFrameCorrector/TcFrameCorrectorCodec.hpp:67-75`: bitwise CRC-16/CCITT-FALSE, no table — the function to extract. `PersistedRecordCodec` uses its own CRC (not inspected; leave).
- Host-test support present: stubs for ADCS, FaultManager, FaultTypes, ModeManager, PowerMonitor, StartupManager, TaskGate, TcFrameCorrector, TelemetryGate, ThermalManager, Watchdog; `support/Fw/Time/Time.hpp`; no `Fw::Buffer` stub yet.
- `Components/RunInterval/RunInterval.hpp` (Cycle B): `due(intervalTicks)`, `effective(requested, valid)`, `DEFAULT_INTERVAL_S 1`, `MAX_INTERVAL_S 60`.
