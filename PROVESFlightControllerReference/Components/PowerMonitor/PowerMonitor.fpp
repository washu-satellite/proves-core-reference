module Components {
    @ Power Monitor Manager
    passive component PowerMonitor {
        sync input port run: Svc.Sched

        @ Port for sending voltageGet calls to the System Driver
        output port sysVoltageGet: Drv.VoltageGet

        @ Port for sending currentGet calls to the System Driver
        output port sysCurrentGet: Drv.CurrentGet

        @ Port for sending powerGet calls to the System Driver
        output port sysPowerGet: Drv.PowerGet

        @ Port for sending voltageGet calls to the Solar Panel Driver
        output port solVoltageGet: Drv.VoltageGet

        @ Port for sending currentGet calls to the Solar Panel Driver
        output port solCurrentGet: Drv.CurrentGet

        @ Port for sending powerGet calls to the Solar Panel Driver
        output port solPowerGet: Drv.PowerGet

        @ Parameter for the power-monitor collection interval in seconds (1..60).
        @ The default of 1 samples on every 1 Hz tick, as before this parameter
        @ existed. Out-of-range or invalid values fall back to 1.
        param COLLECTION_INTERVAL_S: U8 default 1 id 0

        @ Command to reset the accumulated power consumption
        sync command RESET_TOTAL_POWER()

        @ Command to reset the accumulated power generation
        sync command RESET_TOTAL_GENERATION()

        @ Command to get the accumulated power consumption
        sync command GET_TOTAL_POWER()

        @ Telemetry channel for accumulated power consumption in mWh
        telemetry TotalPowerConsumption: F32

        @ Telemetry channel for accumulated solar power generation in mWh
        telemetry TotalPowerGenerated: F32

        @ Telemetry channel for the collection interval actually in force
        telemetry CollectionIntervalS: U8 update on change

        @ Event logged when total power consumption is reset
        event TotalPowerReset() \
            severity activity low \
            format "Total power consumption reset to 0 mWh"

        @ Event logged when total power generation is reset
        event TotalGenerationReset() \
            severity activity low \
            format "Total power generation reset to 0 mWh"

        @ Event for reporting total power consumption
        event TotalPowerConsumptionReading(power: F32) \
            severity activity low \
            format "Total power consumption: {} mWh"

        @ Event reporting that a requested collection interval was rejected and
        @ the 1 s default is in force instead
        event CollectionIntervalRejected(requested: U8) \
            severity warning low \
            format "Rejected collection interval {} s; using 1 s" \
            throttle 5

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

        @ Port for getting parameters
        param get port prmGetOut

        @ Port for setting parameters
        param set port prmSetOut

    }
}
