module Components {
    @ Thermal Manager Component for F Prime FSW framework.
    @ Orchestrates temperature sensor readings from 11 TMP112 sensors
    passive component ThermalManager {
        ### Parameters ###
        @ Parameter for face temperature lower threshold in °C
        param FACE_TEMP_LOWER_THRESHOLD: F64 default -40.0 id 0

        @ Parameter for face temperature upper threshold in °C
        param FACE_TEMP_UPPER_THRESHOLD: F64 default 60.0 id 1

        @ Parameter for battery cell temperature lower threshold in °C
        param BATT_CELL_TEMP_LOWER_THRESHOLD: F64 default 5.0 id 2

        @ Parameter for battery cell temperature upper threshold in °C
        param BATT_CELL_TEMP_UPPER_THRESHOLD: F64 default 60.0 id 3

        @ Parameter for the sensor-sweep collection interval in seconds (1..60).
        @ The default of 1 runs the sweep on every 1 Hz tick, as before this
        @ parameter existed. Out-of-range or invalid values fall back to 1.
        param COLLECTION_INTERVAL_S: U8 default 1 id 4

        @ Enum for temperature sensor types
        enum TempSensorType {
            FACE,
            BATTERY
        }

        sync input port run: Svc.Sched

        @ The number of face temperature sensors
        constant numFaceTempSensors = 5

        @ The number of battery cell temperature sensors
        constant numBattCellTempSensors = 4

        @ Port for face temperature sensors
        output port faceTempGet: [numFaceTempSensors] Drv.temperatureGet

        @ Port for battery cell temperature sensors
        output port battCellTempGet: [numBattCellTempSensors] Drv.temperatureGet

        @ Port for Pico temperature sensor
        output port picoTempGet: Drv.picoTemperatureGet

        @ Port reporting each threshold crossing to the FaultManager. Purely
        @ observational: the disposition is ignored because the threshold
        @ events are this component's whole response to the condition.
        output port faultOut: Components.FaultReport

        @ Event for temperature reading below threshold
        event TemperatureBelowThreshold(sensorType: TempSensorType, sensorId: U32, temperature: F64) \
            severity warning low \
            format "{} temperature below threshold: Sensor {} at {} °C"

        @ Event for temperature reading above threshold
        event TemperatureAboveThreshold(sensorType: TempSensorType, sensorId: U32, temperature: F64) \
            severity warning low \
            format "{} temperature above threshold: Sensor {} at {} °C"

        @ Event reporting that a requested collection interval was rejected and
        @ the 1 s default is in force instead
        event CollectionIntervalRejected(requested: U8) \
            severity warning low \
            format "Rejected collection interval {} s; using 1 s" \
            throttle 5

        @ Telemetry channel for the collection interval actually in force
        telemetry CollectionIntervalS: U8 update on change

        ###############################################################################
        # Standard AC Ports: Required for Channels, Events, Commands, and Parameters  #
        ###############################################################################
        @ Port for requesting the current time
        time get port timeCaller

        @ Port for emitting telemetry
        telemetry port tlmOut

        @ Port for emitting events
        event port logOut

        @ Port for getting parameters
        param get port prmGetOut

        @ Port for setting parameters
        param set port prmSetOut

        @ Port for emitting text events
        text event port logTextOut

        @ Port for sending command registrations
        command reg port cmdRegOut

        @ Port for receiving commands
        command recv port cmdIn

        @ Port for sending command responses
        command resp port cmdResponseOut
    }
}
