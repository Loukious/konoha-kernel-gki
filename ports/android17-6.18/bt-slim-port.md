Three native SLIMbus modules from 18 pinned inputs: slimbus, slim-qcom-ngd-ctrl,
and btfm_slim_codec. Final isolated build12 passes without compiler/modpost warnings;
235 imported CRCs match the archived 256-module providers and GKI. Native exported
symbol names and module dependencies are preserved. Replay has zero fuzz/offsets,
and checkpatch reports zero errors, warnings, or checks. The stock GKI SLIMBUS option
is disabled, so this group supplies the real bus and controller. The native Bluetooth
ID table lacked a terminator; adding it exposes both declared aliases
slim:217:220:* and slim:217:221:* under the current modpost implementation.

Core transactions bound reply copies, retain asynchronous heap ownership across
immediate and delayed completion, balance PM votes, and cancel TIDs under the same
lock as replies. Address assignment errors release IDs and initialize successful
outputs. Device lookup no longer holds the publication mutex through device_add;
creation SRCU drains before controller removal. Status callbacks publish only after
probe and drain before driver removal. Retained slave/stream references delay final
controller IDA/IDR destruction. Driver-status callbacks must not synchronously
unregister their own driver/controller from inside the callback.

Streams validate rates, channel counts, port masks and sample width. Port connection,
channel enable, disconnect and vendor register failures propagate; retryable cleanup
retains its state until it succeeds. Bluetooth keeps per-instance channel tables and
per-DAI cleanup state, bounded maps/configuration IDs, and native two-DAI registration.
The native 44.1/88.2 kHz RX disconnect order is preserved. Endpoint unregister drains
through the already-integrated btfmcodec provider. Character files hold krefs, reject
operations after removal, and compare dynamic cdev identity to reject stale inodes
after rebind. Final stream_free follows the native ownership-release contract: on
remove it releases software even when bus cleanup failed; that failure is logged,
and firmware/bus reset may be required before reuse of affected hardware channels.

NGD formats bounded messages without mutating the caller transaction. TX reservations
roll back if submission fails; cancellation revokes stack completion pointers under
the DMA callback lock. RX checks lengths before TID/payload access. TX initializes
before RX can schedule capability work. Partial DMA setup terminates submitted
transfers before freeing buffers; cleanup is idempotent. Remote memory uses validated
fixed offsets and does not mutate DT resources. Child MMIO/work/PM/controller state
exists before SSR/PDR publication. Remove drains restart callbacks/work, disables PM
and IRQ, terminates DMA, drains master work/QMI, then unregisters the SLIM controller.
Parent and child platform drivers register once per module, and the parent owns child
device lifetime. Platform device core owns the child's OF reference.

Ten production-function ASan/UBSan suites and 22 rejecting mutations pass, including
342 transaction cases; 200 response/cancel and 200 asynchronous release races; core
status/removal and creation/removal races; retained-device teardown; 64 DMA allocation
and descriptor faults; NGD framing/late completion; six NGD probe failures; Bluetooth
stream rollback/maps/configuration; and nine Bluetooth probe failures plus stale
files/inodes. Kernel DMA/QMI/PM/IRQ/device services are mocked; pthread tests model
lock/refcount/SRCU ordering. These checks do not prove actual firmware behavior,
DMA/cache ordering, IRQ scheduling, suspend/restart recovery, or audio on hardware.
Saved Onyx DT disables NGD and the active WCN7750 path uses SoundWire; this group
preserves ROM-loaded SLIM dependencies without claiming live SLIM audio validation.
Legacy CONFIG_BT_SLIM and CONFIG_BTFM_SLIM forwarding paths remain disabled.

Integration inputs are frozen-source/, patch, imports.json, modules.json,
BUILD.fragment, Kconfig.fragment and defconfig.fragment. This private group was built
directly in W/onyx_bt_slim; it did not edit central imports, onyx-dev, common or baseline.
Run verify_slim.py with --source-dir onyx after central integration, and preserve the
tests/ fixtures plus verifier in the port recipe. No ZIP or phone validation here.
