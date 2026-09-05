module Components {

    @ Central fault detector for the flight controller. Producers report faults
    @ through faultIn; the component debounces them, counts them, telemeters
    @ them and emits events. It ships in SHADOW MODE: AUTHORITY_ENABLED is
    @ false and AUTHORITY_MASK is 0, so faultIn always answers OBSERVED, no
    @ output action port is ever reached, and every producer keeps performing
    @ exactly the recovery it performs today. Enabling authority per fault type
    @ (a mask bit plus AUTHORITY_ENABLED) hands that recovery to this component.
    @
    @ Passive with a guarded intake: faultIn is called from several threads
    @ (rate group, command dispatcher, event manager) but the periodic work is
    @ an O(9) table scan, so a thread and a queue would cost more than they buy.
    passive component FaultManager {

        # ----------------------------------------------------------------------
        # Input ports
        # ----------------------------------------------------------------------

        @ Fault intake, one slot per producer (see Components.FaultInPorts).
        @ Guarded: producers call from different threads. The handler only
        @ touches the fault table, never an output action port.
        guarded input port faultIn: [FaultInPorts] Components.FaultReport

        @ Port receiving calls from the rate group (1Hz), scheduled after every
        @ producer so a report and its decision land in the same tick
        sync input port run: Svc.Sched

        # ----------------------------------------------------------------------
        # Output ports (unreachable while in shadow mode)
        # ----------------------------------------------------------------------

        @ Port to force safe mode with the reason mapped from the fault type
        output port forceSafeMode: Components.ForceSafeModeWithReason

        @ Port to stop petting the watchdog, which reboots the board in ~26 s
        output port stopWatchdog: Fw.Signal

        # ----------------------------------------------------------------------
        # Commands
        # ----------------------------------------------------------------------

        @ Clear every recorded fault, confirmation and counter
        sync command CLEAR_FAULTS()

        @ Report the current fault summary as an event
        sync command GET_FAULT_STATUS()

        # ----------------------------------------------------------------------
        # Parameters
        # ----------------------------------------------------------------------

        @ Master authority gate. False (the default, and the value used for any
        @ invalid or uninitialised parameter) keeps the component in shadow
        @ mode: it observes and reports but never acts.
        param AUTHORITY_ENABLED: bool default false

        @ Per-fault-type authority bits, bit = 1 << (FaultType - 1). A fault is
        @ acted on only when AUTHORITY_ENABLED is true AND its bit is set here.
        param AUTHORITY_MASK: U8 default 0

        @ Consecutive 1 Hz samples of low voltage needed to confirm LOW_BATTERY.
        @ The default matches ModeManager.SafeModeDebounceSeconds so the
        @ confirmation lands on the same tick as today's AutoSafeModeEntry.
        param DEBOUNCE_LOW_BATTERY: U8 default 10

        @ Consecutive reports needed to confirm a thermal threshold fault. The
        @ default of 1 confirms on the event ThermalManager already emits.
        param DEBOUNCE_THERMAL: U8 default 1

        # ----------------------------------------------------------------------
        # Events
        # ----------------------------------------------------------------------

        @ A fault type met its debounce threshold
        event FaultConfirmed(
            faultType: FaultType @< The kind of fault confirmed
            source: FaultSource @< The component that reported it
            value: F32 @< Value of the report that confirmed it
        ) \
            severity warning high \
            format "FAULT CONFIRMED: {} from {} (value {})" \
            throttle 10

        @ A confirmed fault had a recovery action, but this component has no
        @ authority for it, so the reporting component's own action stands
        event FaultActionSuppressed(
            faultType: FaultType @< The kind of fault confirmed
            faultAction: FaultAction @< The action that was not taken
        ) \
            severity warning low \
            format "Shadow mode: fault {} would have triggered {}" \
            throttle 10

        @ A confirmed fault was acted on by this component
        event FaultActionTaken(
            faultType: FaultType @< The kind of fault confirmed
            faultAction: FaultAction @< The action performed
        ) \
            severity warning high \
            format "FAULT ACTION TAKEN: {} -> {}"

        @ Every recorded fault was cleared by command
        event FaultsCleared() \
            severity activity high \
            format "Fault table cleared"

        @ Fault summary, emitted in response to GET_FAULT_STATUS
        event FaultStatusReport(
            activeMask: U8 @< Bitmask of currently confirmed fault types
            detected: U32 @< Total fault reports received
            confirmed: U32 @< Total fault confirmations
            authority: U8 @< Effective authority mask (0 means shadow mode)
        ) \
            severity activity low \
            format "Faults: active mask {} detected {} confirmed {} authority {}"

        @ The effective authority changed through a parameter update
        event FaultAuthorityChanged(
            enabled: bool @< Whether the master authority gate is on
            mask: U8 @< The per-type authority mask
        ) \
            severity warning high \
            format "Fault authority changed: enabled={} mask={}"

        # ----------------------------------------------------------------------
        # Telemetry
        # ----------------------------------------------------------------------

        @ Total fault reports received on faultIn
        telemetry FaultsDetected: U32 update on change

        @ Total fault confirmations (debounce thresholds met)
        telemetry FaultsConfirmed: U32 update on change

        @ Bitmask of currently confirmed fault types
        telemetry ActiveFaults: U8 update on change

        @ Type of the most recently confirmed fault
        telemetry LastFaultType: FaultType update on change

        @ Source of the most recently confirmed fault
        telemetry LastFaultSource: FaultSource update on change

        @ Value carried by the most recently confirmed fault
        telemetry LastFaultValue: F32 update on change

        @ Recovery actions declined because this component had no authority
        telemetry ShadowActionsSuppressed: U32 update on change

        @ Recovery actions performed by this component
        telemetry ActionsTaken: U32 update on change

        @ Effective authority mask; 0 means shadow mode
        telemetry AuthorityState: U8 update on change

        @ Reports received for the four thermal threshold fault types
        telemetry FaultCountThermal: U32 update on change

        @ Reports received for LOW_BATTERY
        telemetry FaultCountLowBattery: U32 update on change

        @ Reports received for COMMAND_LOSS
        telemetry FaultCountCommandLoss: U32 update on change

        @ Reports received for WATCHDOG_STOPPED
        telemetry FaultCountWatchdogStop: U32 update on change

        ###############################################################################
        # Standard AC Ports: Required for Channels, Events, Commands, and Parameters  #
        ###############################################################################
        @ Port for requesting the current time
        time get port timeCaller

        @ Port for sending command registrations
        command reg port cmdRegOut

        @ Port for receiving commands
        command recv port cmdIn

        @ Port for sending command responses
        command resp port cmdResponseOut

        @ Port for sending textual representation of events
        text event port logTextOut

        @ Port for sending events to downlink
        event port logOut

        @ Port for sending telemetry channels to downlink
        telemetry port tlmOut

        @ Port for getting parameter values
        param get port prmGetOut

        @ Port for setting parameter values
        param set port prmSetOut
    }
}
