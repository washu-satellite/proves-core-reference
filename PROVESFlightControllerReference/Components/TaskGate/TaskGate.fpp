module Components {

    @ Identifies a schedulable task that TaskGate can enable or disable.
    @ The ordinal IS the schedIn/schedOut port index, so the enum and the port
    @ arrays cannot drift apart. Only these five tasks exist: gating anything
    @ else (watchdog, modeManager, startupManager, burnwire, antennaDeployer,
    @ detumbleManager, telemetryDelay, comms and health plumbing, every 10 Hz
    @ member) is a compile-time impossibility, not a runtime rejection.
    enum SchedTask {
        IMU = 0
        POWER_MONITOR = 1
        ADCS = 2
        THERMAL = 3
        FS_SPACE = 4
    }

    @ Number of gateable tasks; the width of both port arrays.
    constant NUM_TASKS = 5

    @ Generic enable/disable gate for scheduled tasks. Interposed between
    @ rateGroup1Hz.RateGroupMemberOut and the five stateless sensor-poll
    @ components, one port pair per task. A tick for a disabled task is dropped
    @ and counted; a tick for an enabled task is forwarded unchanged.
    @
    @ Passive and sync throughout, so ENABLE_TASK/DISABLE_TASK latch before the
    @ next 1 Hz tick ("effect within one cycle"). State is RAM-only and every
    @ task is enabled after a reboot, which is the safe direction: a gate can
    @ never survive a reset and silence a sensor indefinitely.
    passive component TaskGate {

        @ Rate schedule tick input, one port per task (index == SchedTask ordinal)
        sync input port schedIn: [NUM_TASKS] Svc.Sched

        @ Rate schedule tick output, one port per task; only forwarded while enabled
        output port schedOut: [NUM_TASKS] Svc.Sched

        @ Re-enable a gated task. Idempotent; takes effect on the next tick.
        sync command ENABLE_TASK(task: SchedTask)

        @ Stop a task from being scheduled. Idempotent; takes effect on the next tick.
        sync command DISABLE_TASK(task: SchedTask)

        @ Bit i is set when task i (SchedTask ordinal i) is enabled. Default 0x1F.
        telemetry TasksEnabledMask: U32

        @ Cumulative count of scheduler ticks dropped across all tasks since boot
        telemetry GatedRuns: U32

        @ Emitted when a task is enabled
        event TaskEnabled(task: SchedTask) severity activity high \
            format "Scheduled task {} enabled"

        @ Emitted when a task is disabled
        event TaskDisabled(task: SchedTask) severity activity high \
            format "Scheduled task {} disabled"

        ###############################################################################
        # Standard AC Ports: Required for Channels, Events, and Commands              #
        ###############################################################################
        @ Port for requesting the current time
        time get port timeCaller

        @ Port for emitting telemetry
        telemetry port tlmOut

        @ Port for emitting events
        event port logOut

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
