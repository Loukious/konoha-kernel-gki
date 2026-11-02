# Live boot compatibility fixes

6.18 remained alive for 425.99 seconds after adding native modem storage UIO,
with Android boot completion still unset. Kernel logs showed repeated VIDC
metadata queue failures and a permanently deferred sound card. The normal
Android USB path worked; fastbootd under 6.18 has not yet connected.

The VIDC metadata queues are zero-allocated and lacked both a queue lock and
custom wait callbacks. The 6.18 vb2 core rejects this at line 2661. Queue setup
now gives every queue the existing instance queue lock and device.

The board declares seven BT SoundWire ports with hardware IDs 1, 2, 3, 5, 6,
7, 8. Probe incorrectly treated the count as the maximum ID and rejected 8.
Mapping now validates against array capacity and expands the lookup bound to
include mapped IDs. Invalid IDs, masks and channel overflow remain rejected.

The machine driver reports its failed stage with dev_err_probe so a deferred
sound-card probe names its remaining dependency. This diagnostic does not
claim to resolve that dependency.

verify.py compiles extracted production queue setup and SoundWire parsing/
lookup with ASan and UBSan. The candidate passes; each original regression
fails the corresponding scenario. Existing common-state and card-lifetime
checks pass. Archive 20261101T212516346613Z passes all 316 platform module CRC
checks (20,155 imports) and DT composition checks. GKI and DT bytes are
unchanged; only machine_dlkm, swr_ctrl_dlkm and msm_video modules change.

Runtime validation of these repairs remains pending. Audio/video operation,
modem functionality and a completed Android boot remain unproven.
