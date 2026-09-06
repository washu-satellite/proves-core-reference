module Components {

    @ Telemetry transmission enable/disable state
    enum TelemetryTxState {
        ENABLED
        DISABLED
    }

    @ Gates scheduled telemetry downlink based on a persisted enable/disable
    @ state. Inserted between telemetryDelay.runOut and CdhCore.tlmSend.Run: when
    @ DISABLED the scheduler tick is not forwarded, so TlmChan stops running and
    @ all channelized telemetry ceases within one scheduler cycle. State persists
    @ to a flash file (NullPrmDb does not persist fprime parameters).
    passive component TelemetryGate {

        @ Rate schedule tick input (from telemetryDelay.runOut)
        sync input port runIn: Svc.Sched

        @ Rate schedule tick output (to CdhCore.tlmSend.Run); only forwarded while ENABLED
        output port runOut: Svc.Sched

        @ Set the telemetry transmission enable/disable state. Latches immediately
        @ (effective on the next scheduler tick) and persists to flash.
        sync command SET_TRANSMIT_STATE(txState: TelemetryTxState)

        @ Current telemetry transmission state
        telemetry TransmitState: TelemetryTxState

        @ Number of scheduler ticks gated (not forwarded) while disabled
        telemetry GatedTicks: U32

        @ Emitted when the telemetry transmission state is set
        event TransmitStateSet(txState: TelemetryTxState) severity activity high \
            format "Telemetry transmission state set to {}"

        @ Emitted when persisting the telemetry transmission state to flash fails
        event StateFileWriteFailure severity warning high \
            format "Failed to persist telemetry transmission state to flash" throttle 5

        @ Emitted when the persisted telemetry transmission state file is corrupt;
        @ state defaults to ENABLED (fail-operational)
        event StateFileCorrupt severity warning high \
            format "Telemetry transmission state file corrupt; defaulting to ENABLED" throttle 5

        ###############################################################################
        # Standard AC Ports: Required for Channels, Events, and Commands              #
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
    }
}
