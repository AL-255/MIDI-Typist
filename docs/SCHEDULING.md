# Scheduling and the FreeRTOS decision

The shared application has one serialized owner and no dependency on an OS.
The Huntsman port uses its cooperative main loop and interrupt-driven USB/DMA.
The synthetic port supplies a deterministic desktop clock. The application
can be driven from a dedicated task in an RTOS-based board port without
changing its processing modules.
The [porting guide's lifecycle example](PORTING.md#lifecycle-adapter-example)
shows the same owner boundary for a loop or task, with real acquisition
events separated from millisecond service time.

## Why the Huntsman does not require FreeRTOS

SRAMX is limited to 24 KiB; USB has a separate 16 KiB region and the stack
an 8 KiB reservation. Calibration and persistence state use explicitly
initialized application-image RAM. Check the linker's current usage report:
unused image space is not automatically an RTOS heap.

FreeRTOS would require task control blocks and separate task stacks, including
its idle task. Static allocation removes the heap requirement, not those
objects. This follows the kernel's
[static/dynamic allocation documentation](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/09-Memory-management/03-Static-vs-Dynamic-memory-allocation)
and [task-management documentation](https://github.com/FreeRTOS/FreeRTOS-Kernel-Book/blob/main/ch03.md).

The current workload is already serialized: scan-dependent note ordering,
calibration, settings mutations and USB output queues share state. Splitting
them into independent tasks would add synchronization and scheduling work.
An 8 kHz nominal frame period is 125 microseconds, so a millisecond RTOS delay
is not an acquisition scheduler. Existing DMA and USB completion handling
must remain responsive.

A kernel could fit after deliberate memory placement and stack budgeting,
but that needs port-specific linker changes and measured execution/stack
limits. This build does not claim to have measured a FreeRTOS port or its
context-switch cost. Requiring it would add unverified overhead without a
current concurrency requirement. The selected implementation therefore retains
the cooperative Huntsman scheduler.

## Using an RTOS on another board

An RTOS-owning port can serialize all application calls in one processing
task. ADC/DMA and USB ISRs publish completed buffer ownership and wake that
task using the platform's ISR-safe facilities. They must not call the
application's setters, menus or calibration code concurrently.

The port must guarantee:

- Every delivered frame represents a distinct acquisition at the configured
  rate. Signal dropped/invalid frames; do not replay the newest frame to make
  up a task backlog.
- Input and LED/USB buffers remain immutable while another owner uses them.
- USB reset cannot be lost between disconnect and reconfiguration.
- Configuration commands execute in the same owner context as frame handling.
- Priority, critical-section duration and stack sizes are measured under
  maximum simultaneous key activity and storage operations.
- Watchdog refresh covers the board's real failure modes rather than hiding
  a blocked processing task.

No FreeRTOS source, headers, heap, scheduler or task stack is included in the
Huntsman or synthetic build. A future RTOS board provides its kernel and
vendor integration in its own build manifest, while linking the same
`midi_typist_app` sources.
