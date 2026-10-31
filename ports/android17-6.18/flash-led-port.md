One native camera prerequisite, leds-qti-flash.ko, built from six exact core pins
at 10570812a4878b644e31b47c2f0bd052664cf86d. The sole translation unit is
leds-qti-flash.c; the native public flash header is byte-identical. Isolated build4
passes without compiler/modpost diagnostics. All 45 imports match the archived GKI
symbol CRCs; the two native exports, empty dependency list and empty OF alias list
are preserved. Export CRC values naturally change with 6.18 kernel types, so camera
must be rebuilt against this same public header and provider symvers. Patch replay
passes git apply --check and matches every final hash with zero fuzz/offsets.
Checkpatch reports zero errors, warnings and checks.

Register I/O runs under a sleeping mutex through blocking LED callbacks, flash ops,
sysfs handlers and camera exports. Hrtimer callbacks only queue work. Switch commands
drain queued channel brightness before strobe, preserving camera trigger ordering.
A command mutex serializes timer/work cancellation; a cancellation gate prevents
workers from rearming a timer during the drain. Remove withdraws the controller from
the owned-switch registry, gates new operations, drains workers, attempts every
hardware shutdown write and releases the cached battery reference. Managed LED/IRQ
unregistration occurs while the gated controller still exists. Probe parses and
retains all child nodes, initializes hardware and IRQs, then publishes class devices;
callbacks remain gated until publication completes. Failed publication uses the same
stop path. Trigger lookup compares only driver-owned switch objects, preventing raw
foreign LED casts or use after removal.

I/O errors reach callers. Channel-enable state and safety-time codes commit after
successful writes, failed brightness setup restores its candidate fields, and switch
off remains retryable after partial cleanup. Timer-off failures rearm a 100 ms cleanup
retry; remove cancels retries. Persistent bus failure can still prevent physical
shutdown, and removal logs each failed shutdown write. No software test establishes
physical flash current, electrical timing, or successful quiescence on faulty hardware.

The Onyx camera overlay enables the provider. Native channel masks 9/6/15, 300 mA
flash limits, 300 ms flash duration and torch 300 mA up to 5000 ms then 150 mA are
retained. Channel IDs and strobe selectors are checked before narrowing; paired torch
current/time arrays are bounded, sorted as u32 and validated; maximum duration is
validated and quantized down to the hardware step. Timeout code zero remains the
valid 10 ms value. Timer inputs are bounded before conversion to ktime. The native
sysfs unit contract is intentionally retained: on_time/off_time writes are milliseconds,
reads are microseconds. Public flash LED brightness units retain the native behavior.
The all-channel symmetry path indexes the actual switch array rather than walking
past an interior pointer.

Native CONFIG_QTI_BATTERY_CHARGER is disabled. With a battery present, the pinned
battery-resistance getter returns -EINVAL; max-current queries propagate that error.
Without a battery the native 2000 mA fallback remains; secure-VM queries now initialize
the caller's output. This handoff does not fabricate a charger provider or claim full
camera/flash operation. The unchanged native ENABLE_REGULATOR/DISABLE_REGULATOR option
bits remain accepted no-ops; only QUERY_MAX_AVAIL_CURRENT performs work.

Three ASan/UBSan suites exercise the actual production functions with mocked kernel
services: 2000 timeout values, 15 register faults, queued brightness and timer ordering,
10 ms strobe, symmetry with an interior switch pointer, native battery query errors,
13 allocation/IRQ/LED-registration failures, six setup I/O failures, 13 invalid device
tree cases, complete shutdown writes, node/battery ownership, 200 query/remove races
and 200 worker/remove races. Twenty-three deliberately broken mutations are rejected.
Pthread mutexes model serialization; fake work/timer/device/IRQ services do not prove
actual kernel workqueue, driver-core, interrupt, suspend or hardware behavior.

Integration inputs: frozen-source/, patch, imports.json, modules.json, BUILD.fragment,
Kconfig.fragment and defconfig.fragment. Preserve tests/ and verify_flash.py in the
central recipe and run with --source-dir onyx there. No central imports, onyx-dev,
common, baseline, stable kernel or frozen SLIM files were changed by this handoff.
Camera main was not imported, and no ZIP, flashing or phone validation was performed.
