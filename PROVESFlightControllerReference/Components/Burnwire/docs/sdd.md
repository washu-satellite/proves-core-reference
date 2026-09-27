# Components::Burnwire

Driving the Burnwire on and off. This component activates the two pins that are required to heat the burnwire resisitor. The burnwire deployment will be handled by the Antenna Deployment, that will call the ports in the burnwire deployment. For testing, the commands to directly call the burnwire have been left in.

Burnwire is agnostic to the ideal safety count, it simply sets it to be whatever the port or command passes onto

## Sequence Diagrams
Add sequence diagrams here

## Requirements
Add requirements in the chart below
| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|BW-001|The burnwire shall turn on and off in response to a port calls (TBR for antenna deployer component)|Hardware Test|||||
|BW-002|The burnwire shall turn on and off in response to commands (TBR for testing for now)|Integration Test|Board|START_BURNWIRE -> SetBurnwireState ON within 2 s and system power > 1 W; STOP_BURNWIRE -> SetBurnwireState OFF within 2 s and BurnwireEndCount|||
|BW-003|The burnwire component shall provide an event when it is turned on and off|Integration Test|Board|SetBurnwireState ON and OFF events are emitted within 2 s of START_BURNWIRE and STOP_BURNWIRE respectively|||
|BW-004|The burnwire component shall activate by turning both the GPIO pins that activate the burnwire|Hardware Test|||||
|BW-005|The burnwire component shall be controlled by a safety timeout attached to a 1Hz rate group|Integration Test|||||
|BW-006|The safety timeout shall emit an event when it is changes|Integration test|||||
|BW-007|The burnwire safety time shall emit an event when it starts and stops|Integration Test|||||
|BW-008|A Burnwire instance with only gpioSet[0] connected shall drive port 0 and never invoke gpioSet[1]; with both ports connected its GPIO writes shall be unchanged|Unit Test|Unit|Host stub, SAFETY_TIMER 10: with only gpioSet[0] connected, START_BURNWIRE emits SetBurnwireState(ON) and responds OK; the first schedIn tick writes HIGH on port 0; STOP_BURNWIRE, and separately 10 ticks without STOP (safety timer), write LOW on port 0 and emit SetBurnwireState(OFF); gpioSet[1] is invoked zero times throughout. With both ports connected the writes are exactly (0,HIGH),(1,HIGH) on the first tick and (0,LOW),(1,LOW) on STOP_BURNWIRE or on safety-timer expiry|||
|BW-009|burnwireDeploy2 shall start and stop the DEPLOY2 burn channel on command, with the safety timer bounding an unstopped burn|Integration Test|Board|With a dummy load on J24: RD.burnwireDeploy2.START_BURNWIRE gives SetBurnwireState(ON) within 2 s and ina219SysManager power at least 0.3 W above the pre-START reading; STOP_BURNWIRE gives SetBurnwireState(OFF) within 2 s and a BurnwireEndCount event; START_BURNWIRE without STOP gives SetBurnwireState(OFF) 8.5-11 s (FSW event time) after the ON event (SAFETY_TIMER 10 s)|||

## Port Descriptions
Name | Type | Description |
|----|---|---|
|burnStop|`Fw::Signal`|Receive stop signal to stop the burnwire|
|burnStart|`Fw::Signal`|Receive start signal to start burnwire|
|gpioSet|`Drv::GpioWrite`|Control GPIO state to driver|
|schedIn|[`Svc::Sched`]| run | Input | Synchronous | Receive periodic calls from rate group|


## Commands
| Name | Description |
| ---- | -----------  |
|START_BURNWIRE|Starts the Burn|
|STOP_BURNWIRE|Stops the Burn|

## Events
| Name | Description |
|---|---|
|SetBurnwireState| Emits burnwire state when the burnwire turns on or off|
|SafetyTimerStatus| Emits safety timer state when the Safety Time has stopped or started|
|SafetyTimerState| Emits the amount of time the safety time will run for when it starts|
| BurnwireEndCount| How long the burnwire actually burned for |

## Component States
Add component states in the chart below
| Name | Description |
|----|---|
|m_state|Keeps track if the burnwire is on or off|

##  Tests
Add unit test descriptions in the chart below
| Name | Description | Output | Coverage |
|------|-------------|--------|----------|
|test_01_start_and_stop_burnwire|Tests the burnwire functionality by enabling it and asserting that the power consumption of the satellite increases.|Integration|---|


## Parameter
| Name | Description |
| -----|-------------|
|   SAFETY_TIMER   | By Default set in fpp (currently 10) is the max time the burnwire should ever run|
