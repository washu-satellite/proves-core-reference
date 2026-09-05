module Components {
    @ Attitude Determination and Control Component for F Prime FSW framework.
    passive component ADCS {
        sync input port run: Svc.Sched

        @ The number of light sensors on the ADCS
        constant numLightSensors = 6

        @ Port for visible light from the light sensors
        output port visibleLightGet: [numLightSensors] Drv.lightGet

        ### Parameters ###

        @ Parameter for the light-sensor collection interval in seconds (1..60).
        @ The default of 1 reads the sensors on every 1 Hz tick, as before this
        @ parameter existed. Out-of-range or invalid values fall back to 1.
        param COLLECTION_INTERVAL_S: U8 default 1 id 0

        ### Telemetry channels ###

        @ Telemetry channel for the collection interval actually in force
        telemetry CollectionIntervalS: U8 update on change

        ### Events ###

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

        @ Port for emitting telemetry
        telemetry port tlmOut

        @ Port for emitting events
        event port logOut

        @ Port for emitting text events
        text event port logTextOut

        @ Port for getting parameters
        param get port prmGetOut

        @ Port for setting parameters
        param set port prmSetOut

        @ Port for sending command registrations
        command reg port cmdRegOut

        @ Port for receiving commands
        command recv port cmdIn

        @ Port for sending command responses
        command resp port cmdResponseOut
    }
}
