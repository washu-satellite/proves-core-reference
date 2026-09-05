module Components {

    @ Kind of fault tracked by the FaultManager. The numeric values double as
    @ bit positions in every authority/active mask: bit = 1 << (value - 1), so
    @ FACE_TEMP_HIGH = 0x01 ... ADCS_UNSTABLE = 0x80. NONE has no bit.
    enum FaultType: U8 {
        NONE = 0 @< No fault
        FACE_TEMP_HIGH = 1 @< Face temperature above the configured upper threshold
        FACE_TEMP_LOW = 2 @< Face temperature below the configured lower threshold
        BATT_TEMP_HIGH = 3 @< Battery cell temperature above the configured upper threshold
        BATT_TEMP_LOW = 4 @< Battery cell temperature below the configured lower threshold
        LOW_BATTERY = 5 @< System voltage below the safe mode entry threshold
        COMMAND_LOSS = 6 @< No authenticated uplink within the command loss window
        WATCHDOG_STOPPED = 7 @< Watchdog petting stopped; hardware reset pending
        ADCS_UNSTABLE = 8 @< Reserved for FD-L2-07 (no producer this cycle)
    }

    @ Component that observed and reported a fault
    enum FaultSource: U8 {
        THERMAL_MANAGER = 0 @< Components.ThermalManager threshold evaluation
        MODE_MANAGER = 1 @< Components.ModeManager voltage monitoring
        AUTH_ROUTER = 2 @< Svc.AuthenticationRouter command loss timer
        WATCHDOG = 3 @< Components.Watchdog stop path
        DETUMBLE_MANAGER = 4 @< Reserved for FD-L2-07 (no producer this cycle)
    }

    @ How serious the reporting component considers the fault
    enum FaultSeverity: U8 {
        WARNING = 0 @< Degraded but not mission-threatening
        CRITICAL = 1 @< Requires a recovery action
    }

    @ Recovery action associated with a fault type. The values mirror what the
    @ producers already do today; see Components/FaultManager/docs/sdd.md.
    enum FaultAction: U8 {
        NONE = 0 @< No action; the fault is observed and counted only
        SAFE_MODE = 1 @< Force safe mode with the fault's reason
        SAFE_MODE_AND_REBOOT = 2 @< Stop the watchdog, then force safe mode
    }

    @ Whether the FaultManager took ownership of the reported fault. OBSERVED
    @ means the reporting component must keep performing its own action, which
    @ is always the case while the FaultManager is in shadow mode.
    enum FaultDisposition: U8 {
        OBSERVED = 0 @< FaultManager only counted the report
        CLAIMED = 1 @< FaultManager will perform the recovery action
    }

    @ Port used by any component to report a fault to the FaultManager. The
    @ return value tells the caller whether the FaultManager claimed the
    @ recovery action; OBSERVED means the caller keeps its own behaviour.
    port FaultReport(
        faultType: FaultType @< The kind of fault observed
        source: FaultSource @< The reporting component
        faultSeverity: FaultSeverity @< How serious the reporter considers it
        value: F32 @< Measured value behind the report (0 if not applicable)
    ) -> FaultDisposition

    @ Number of faultIn slots on the FaultManager. Index assignment (topology.fpp
    @ "connections FaultManager"): 0 thermalManager, 1 modeManager,
    @ 2 ComCcsdsLora.authenticationRouter, 3 watchdog.
    constant FaultInPorts = 4

}
