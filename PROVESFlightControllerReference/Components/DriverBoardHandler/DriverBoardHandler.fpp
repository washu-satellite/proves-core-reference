module Components {

    @ Driver-board state as this component last knew it. Local transitions
    @ (ARM acknowledged, DISARM, ABORT, link loss, safe mode, board fault)
    @ write DISARMED/ARMED; a housekeeping frame writes the state the board
    @ reports (DISARMED, ARMED, PULSING, FAULT). UNKNOWN is written when a
    @ housekeeping frame carries a state byte outside the protocol table.
    enum DriverState: U8 {
        DISARMED = 0 @< Coils off; the board refuses PULSE
        ARMED = 1 @< The board acknowledged ARM and accepts PULSE
        PULSING = 2 @< A pulse is in progress (reported by the board)
        FAULT = 3 @< The board is in its fault state (reported by the board)
        UNKNOWN = 4 @< The board reported a state byte outside the table
    }

    @ Host-side supervision of the serial link to the driver board
    enum LinkState: U8 {
        DOWN = 0 @< No valid board frame within LINK_TIMEOUT_MS (or ever)
        UP = 1 @< At least one valid board frame within LINK_TIMEOUT_MS
    }

    @ Why the handler left the ARMED state
    enum DisarmReason: U8 {
        COMMAND = 0 @< DISARM command
        LINK_LOST = 1 @< No valid board frame for LINK_TIMEOUT_MS
        SAFE_MODE = 2 @< ModeManager reported SAFE_MODE on the 1 Hz poll
        BOARD_FAULT = 3 @< The board reported a fault (FAULT frame or HK state FAULT)
        ABORT = 4 @< ABORT command
    }

    @ Why a command was refused before or after reaching the board
    enum RefuseReason: U8 {
        LINK_DOWN = 0 @< The link is not up
        NOT_ARMED = 1 @< The handler is not ARMED
        SAFE_MODE = 2 @< ModeManager reports SAFE_MODE
        DRIVER_NOT_READY = 3 @< The UART driver has not signalled ready
        BOARD_REFUSED = 4 @< The board answered ACK with a non-zero status
    }

    @ One SAMPLE frame from the driver board (A9 burst-capture hook). Values are
    @ the wire integers: mA and signed percent, board clock in ms.
    port PayloadSample(
        boardTimeMs: U32 @< Board clock at the sample
        currentMa0: I16 @< Coil 0 current, mA
        currentMa1: I16 @< Coil 1 current, mA
        currentMa2: I16 @< Coil 2 current, mA
        dutyPct0: I8 @< Coil 0 duty, signed percent (negative = reversed)
        dutyPct1: I8 @< Coil 1 duty, signed percent
        dutyPct2: I8 @< Coil 2 duty, signed percent
    )

    @ Owns the SCALAR driver board (STM32L031) over a ByteStreamDriver: sends
    @ one host frame per 1 Hz tick (HEARTBEAT or HK_REQUEST), supervises the
    @ link, exposes ARM/DISARM/PULSE/ABORT/PING/GET_STATUS, and turns the
    @ board's housekeeping into telemetry. The wire protocol is
    @ Components/DriverBoardProtocol (docs/sdd.md there is the spec).
    @
    @ Boots DISARMED and LINK_DOWN. Safety by structure: PULSE needs ARMED,
    @ ARM needs LINK_UP and mode != SAFE_MODE and completes only on the
    @ board's ACK; link silence or SAFE_MODE forces DISARMED.
    @
    @ Locking: uartRecv is guarded (called from the driver's 50 Hz thread);
    @ run, uartReady and every command are sync and bracket their state work
    @ with the explicit component lock. No frame is ever sent while the lock
    @ is held (see docs/sdd.md "Locking rule").
    passive component DriverBoardHandler {

        # ----------------------------------------------------------------------
        # Ports
        # ----------------------------------------------------------------------

        @ Bytes from the driver (driverBoardUart.$recv). Guarded: runs on the
        @ driver's thread; parses, returns the buffer, never sends.
        guarded input port uartRecv: Drv.ByteStreamData

        @ Every received buffer goes back to the driver here, always
        output port uartRecvReturn: Fw.BufferSend

        @ Host frames to the driver (synchronous; the caller keeps the buffer)
        output port uartSend: Drv.ByteStreamSend

        @ The driver is configured; the first host frame (DISARM) goes out here
        sync input port uartReady: Drv.ByteStreamReady

        @ 1 Hz: link supervision, mode poll, heartbeat / housekeeping request
        sync input port run: Svc.Sched

        @ Polled each tick: SAFE_MODE refuses ARM and forces DISARMED
        output port getMode: Components.GetSystemMode

        @ One call per SAMPLE frame, only when connected (A9 hook)
        output port sampleOut: Components.PayloadSample

        # ----------------------------------------------------------------------
        # Parameters (RAM-only until PRM_SAVE_FILE; validated by fallback to
        # the default in parameterUpdated, one ParameterRejected event each)
        # ----------------------------------------------------------------------

        @ PULSE frame duration, ms. Valid 50..5000; out of range falls back to 2000.
        param PULSE_DURATION_MS: U16 default 2000

        @ PULSE frame duty, percent. Valid 0..100; out of range falls back to 50.
        param PULSE_DUTY_PCT: U8 default 50

        @ PULSE frame channel mask, bits 0-2. Valid 0x01..0x07; otherwise 0x07.
        param PULSE_CHANNEL_MASK: U8 default 0x07

        @ Silence on the link that drops it to LINK_DOWN and forces DISARMED,
        @ ms. Valid 100..10000; otherwise 1000. Measured on the 1 Hz tick.
        param LINK_TIMEOUT_MS: U16 default 1000

        @ HK_REQUEST cadence in seconds; HEARTBEAT fills the other ticks.
        @ Valid 1..60; otherwise 1.
        param HK_INTERVAL_S: U8 default 1

        # ----------------------------------------------------------------------
        # Commands (all sync; the handler takes its own lock for state work)
        # ----------------------------------------------------------------------

        @ Send ARM to the board. Needs LINK_UP and mode != SAFE_MODE. OK means
        @ the frame was sent; ARMED follows only on the board's ACK(ARM, 0).
        sync command ARM()

        @ Send DISARM and go DISARMED immediately, without waiting for the ACK
        sync command DISARM()

        @ Send PULSE with the current PULSE_* parameters. Needs ARMED.
        sync command PULSE(
            polarityMask: U8 @< Bits 0-2, 1 = reversed polarity on that coil
        )

        @ Send ABORT (coils off within 1 ms on the board) and go DISARMED
        sync command ABORT()

        @ Send PING; PongReceived reports the board's firmware version
        sync command PING()

        @ Emit StatusReport with the link and driver state as last known
        sync command GET_STATUS()

        # ----------------------------------------------------------------------
        # Telemetry (all 22 channels travel in the PayloadHousekeeping packet)
        # ----------------------------------------------------------------------

        @ Coil 0 current from housekeeping, A (wire mA / 1000)
        telemetry CoilCurrent0: F32

        @ Coil 1 current from housekeeping, A
        telemetry CoilCurrent1: F32

        @ Coil 2 current from housekeeping, A
        telemetry CoilCurrent2: F32

        @ Driver temperature 0 from housekeeping, degC (wire 0.1 degC / 10)
        telemetry CoilTemperature0: F32

        @ Driver temperature 1 from housekeeping, degC
        telemetry CoilTemperature1: F32

        @ Coil 0 PWM duty from housekeeping, signed percent
        telemetry PwmDuty0: I8

        @ Coil 1 PWM duty from housekeeping, signed percent
        telemetry PwmDuty1: I8

        @ Coil 2 PWM duty from housekeeping, signed percent
        telemetry PwmDuty2: I8

        @ Driver-board state as last known (local transitions and housekeeping)
        telemetry DriverState: DriverState

        @ Board fault flags from the last HK or FAULT frame (bits per the protocol)
        telemetry FaultFlags: U8

        @ Link supervision state
        telemetry LinkState: LinkState

        @ Board uptime from housekeeping, ms
        telemetry BoardUptime: U32

        @ Frames delivered by the parser (any TYPE)
        telemetry FramesReceived: U32

        @ Parser rejections plus frames dropped for an unknown TYPE or bad length
        telemetry FramesRejected: U16

        @ SAMPLE frames parsed (forwarded on sampleOut when connected)
        telemetry SamplesReceived: U32

        @ Link transitions UP to DOWN
        telemetry LinkTimeouts: U16

        @ Board firmware version from the last PONG (0 until one arrives)
        telemetry FirmwareVersion: U16

        @ PULSE frames sent
        telemetry PulsesCommanded: U16

        @ Effective PULSE_DURATION_MS (default if the parameter was rejected)
        telemetry PulseDurationMs: U16

        @ Effective PULSE_DUTY_PCT
        telemetry PulseDutyPct: U8

        @ Effective LINK_TIMEOUT_MS
        telemetry LinkTimeoutMs: U16

        @ Effective HK_INTERVAL_S
        telemetry HkIntervalS: U8

        # ----------------------------------------------------------------------
        # Events (throttles chosen so a missing board is quiet)
        # ----------------------------------------------------------------------

        @ First valid board frame after LINK_DOWN
        event LinkUp(
            version: U16 @< Board firmware version if a PONG has been seen, else 0
        ) \
            severity activity high \
            format "Driver board link up (firmware {})"

        @ No valid board frame for LINK_TIMEOUT_MS; the handler is now DISARMED
        event LinkLost(
            silentMs: U32 @< Milliseconds since the last valid board frame
        ) \
            severity warning high \
            format "Driver board link lost after {} ms of silence"

        @ A frame or byte stretch was rejected (1 SYNC, 2 LENGTH, 3 CRC,
        @ 4 unknown TYPE, 5 payload length does not match TYPE)
        event FrameRejected(
            reason: U8 @< Rejection reason code
        ) \
            severity warning low \
            format "Driver board frame rejected (reason {})" \
            throttle 5

        @ The board acknowledged ARM with status OK
        event Armed() \
            severity activity high \
            format "Driver board ARMED"

        @ The handler left ARMED (or was told to disarm while DISARMED)
        event Disarmed(
            reason: DisarmReason @< What caused the disarm
        ) \
            severity activity high \
            format "Driver board DISARMED ({})"

        @ A PULSE frame was sent with these fields
        event PulseStarted(
            durationMs: U16 @< Pulse duration, ms
            dutyPct: U8 @< Duty, percent
            channelMask: U8 @< Coils driven, bits 0-2
            polarityMask: U8 @< Reversed coils, bits 0-2
        ) \
            severity activity high \
            format "Pulse sent: {} ms, {} %, channels {}, polarity {}"

        @ The board answered a PULSE with a non-zero ACK status
        event PulseRefused(
            status: U8 @< Board ACK status (1 NOT_ARMED, 2 FAULT, 3 BAD_ARG, 4 BUSY)
        ) \
            severity warning low \
            format "Pulse refused by the board (status {})"

        @ A command was refused, before sending or by the board's ACK
        event CommandRefused(
            cmd: U8 @< Wire TYPE of the command (3 ARM, 5 PULSE, 7 PING)
            reason: RefuseReason @< Why it was refused
        ) \
            severity warning low \
            format "Driver board command {} refused: {}"

        @ The board sent a FAULT frame
        event BoardFault(
            flags: U8 @< Fault flags (bit 0 overcurrent, 1 overtemp, 2 host timeout, 3 undervoltage, 4 sense fail)
            value: I16 @< Measurement behind the fault, protocol units
        ) \
            severity warning high \
            format "Driver board fault flags {} (value {})"

        @ The board answered PING
        event PongReceived(
            fwVersion: U16 @< Board firmware version
            protoVersion: U8 @< Protocol version the board speaks
        ) \
            severity activity low \
            format "Driver board pong: firmware {} protocol {}"

        @ Status snapshot in response to GET_STATUS
        event StatusReport(
            link: LinkState @< Link supervision state
            driverState: DriverState @< Driver-board state as last known (`state` is an fpp keyword)
            flags: U8 @< Last fault flags
            version: U16 @< Firmware version from the last PONG
            uptime: U32 @< Board uptime from the last HK, ms
        ) \
            severity activity low \
            format "Driver board status: link {} state {} flags {} firmware {} uptime {} ms"

        @ A parameter was out of range or unreadable; its default is in effect
        event ParameterRejected(
            paramId: U32 @< Parameter id (0 PULSE_DURATION_MS .. 4 HK_INTERVAL_S)
        ) \
            severity warning low \
            format "Driver board parameter {} rejected; default in effect" \
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

        @ Port for getting parameter values
        param get port prmGetOut

        @ Port for setting parameter values
        param set port prmSetOut
    }
}
