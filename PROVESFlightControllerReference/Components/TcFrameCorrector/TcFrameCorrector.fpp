module Components {

    @ Repairs single-bit errors in uplinked CCSDS TC transfer frames on the LoRa
    @ link. Sits between lora.dataOut and ComCcsdsLora.frameAccumulator.dataIn,
    @ ahead of the accumulator, because CcsdsTcFrameDetector verifies the FECF
    @ itself: a CRC-failing frame is reported NO_FRAME_DETECTED and the
    @ accumulator silently rotates one byte, so no component downstream of the
    @ accumulator ever sees a corrupt frame. LoRa delivers exactly one radio
    @ packet per buffer, so the whole buffer is one frame here.
    @
    @ Pass-through by default: with CORRECTION_ENABLED false (the flight setting
    @ until the board procedure runs) the same Fw.Buffer is forwarded with no
    @ copy and no byte touched. Uncorrectable frames are also forwarded
    @ unchanged, so the accumulator drops them exactly as it does today.
    passive component TcFrameCorrector {

        @ Enables in-place single-bit correction. Default false: the component
        @ is a byte-identical pass-through until this is set.
        param CORRECTION_ENABLED: bool default false id 0

        @ Port receiving raw uplink packets from the LoRa driver
        guarded input port dataIn: Svc.ComDataWithContext

        @ Port forwarding packets (repaired or untouched) to the frame accumulator
        output port dataOut: Svc.ComDataWithContext

        @ Port returning ownership of buffers back upstream to the LoRa driver
        output port dataReturnOut: Svc.ComDataWithContext

        @ Port receiving back ownership of buffers sent to dataOut (frame accumulator)
        sync input port dataReturnIn: Svc.ComDataWithContext

        @ Emitted when a single-bit error was located and repaired in place
        event FrameCorrected(bitIndex: U16, frameLength: U16) severity activity high \
            format "Corrected single-bit error at bit {} of a {}-byte TC frame"

        @ Emitted when a frame's FECF does not check out and no single-bit error
        @ explains it; the frame is forwarded unchanged and dropped downstream
        event FrameUncorrectable(frameLength: U16) severity warning low \
            format "Uncorrectable error in a {}-byte TC frame" throttle 5

        @ Cumulative count of frames repaired since boot
        telemetry CorrectedFrames: U32

        @ Cumulative count of frames whose errors could not be repaired since boot
        telemetry UncorrectableFrames: U32

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
