Native cameralog and cameramsger from sixteen exact files at modules revision
8500748dae32f10507c02f1c94bc971553cb525b. All six native translation units are retained:
cam_log plus cam_msger, cam_sched, cam_pid, cam_binder and cam_msger_common. The remaining
inputs are their six headers and native Kbuild/Makefile/Kconfig/config/sun.mk evidence.
Normal strict isolated build3 passes without compiler/modpost diagnostics. All 81
imported CRCs match GKI; both native dependency and alias lists remain empty. Logger
exports camlog_send_message, messenger exports nothing. The logger public header and
its native logging-only exported function are preserved; no camera-main sources were
imported. The native binder file contains only its original reserved placeholder.

Logger retains /dev/camlog, native ioctl numbers, queue byte-count reporting, complete
oldest-record eviction, capped text writes, and the native final-byte-to-newline read
format. FIFO allocation still rounds the native requested six-record capacity up to
8192 bytes (seven 1032-byte records). All FIFO/debug/ioctl access is serialized. Short
read buffers return -EMSGSIZE, and user-copy faults leave records queued; failed writes
return -EFAULT. Last-message text is always terminated and records carry the actual
writer identity. Locks/waitqueue exist before registration; init failures unwind FIFO,
char device and class. The logger remains unloadable with fops module ownership.

Messenger retains /dev/cam_msger, native reg/snd/swh/ctl/ctp command families, ioctl
numbers, seven reader slots and fifteen queued messages per reader with oldest-record
eviction. Readers receive sender|payload. Messages and field lengths are validated
before copying, allocation failures propagate, short reads and copy faults retain the
record, and unknown-reader reads release the mutex. Reader slot zero participates in
duplicate detection and release; release checks/removes the same file while holding
the registry mutex, including when COC is disabled. Native death notifications remain.
Identity checks use the initial user namespace so a nested namespace cannot impersonate
a privileged system UID. PID queries retain task references across sleeping command-line
allocation, handle allocation failure and trim an optional newline rather than dropping
an arbitrary final character. Candidate matching retains the native prefix policy.

Scheduler preserves the native eight-CPU control format, allow/neverallow policies,
five PID selectors, runtime feature switch and all four restricted GKI hooks. Command
parsing rejects invalid masks/modes; each configuration is zero-initialized and replaced
as an immutable RCU object. Hooks use one snapshot per operation and bound CPU indices,
choose only active CPUs allowed by task affinity, preserve prior core-scheduler denials,
and keep a runnable CPU if the control mask covers all of a task's active affinity.
Old configurations are freed after an RCU grace period. Default runtime feature bits
remain COC only; scheduler/PID features require their native switch commands.

Messenger remains resident, as in the ROM: it intentionally has no module_exit because
restricted hooks cannot be unregistered. All ordinary fallible device setup occurs
before hook publication. If allocation or the first hook registration fails, initialization
unwinds completely. After a later hook registration fails, existing callbacks remain
resident but inactive, the error is logged and ordinary messaging remains available.
A subsequent swh command enabling scheduling retries only missing registrations and
returns an error until all four hooks are installed. It never reports an unloaded or
unregistered hook that still points into module text. No common tracepoint code changes.

Three production-source ASan/UBSan suites and twenty-two rejecting mutations pass:
2050 logger lengths, FIFO/copy/init failures and 1000 concurrent read/write operations;
141 malformed messenger name cases, reader-zero/duplicate handling, FIFO eviction and
copy/allocation failures; scheduler allocation and all four hook-registration failures,
partial-hook retry, 96 CPU indices, malformed commands, task reference balance, 200 RCU
configuration updates with 10000 concurrent callbacks, and runnable-CPU/core-denial
preservation. Device/VFS/user-copy/FIFO/hook services are mocked; pthread locks model
mutex and RCU ordering. These tests do not prove real scheduler, tracepoint, process
exit, Android permissions, HAL, CameraMind or hardware integration behavior.

Checkpatch is zero errors/warnings/checks. Exact pinned replay passes git apply --check
and matches final hashes with zero fuzz/offsets. Integration inputs are frozen-source/,
patch, imports.json, modules.json and the three build/config fragments. Preserve tests/
and verify_camera_aux.py; its --source-dir onyx option supports central verification.
The four camera-kernel build/config evidence files may overlap later main-camera imports
and must be deduplicated by exact blob. No central imports/onyx-dev/common/baseline/stable
kernel, frozen flash LED or frozen SLIM artifacts were modified. No ZIP or phone test.
