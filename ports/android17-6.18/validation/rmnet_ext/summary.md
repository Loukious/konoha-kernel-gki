# RMNET extensions for Onyx / Linux 6.18

The seven native modules build with the pinned 6.18 GKI: `rmnet_aps`,
`rmnet_offload`, `rmnet_perf`, `rmnet_perf_tether`, `rmnet_sch`, `rmnet_shs`
and `rmnet_wlan`. They retain all 28 native compilation units, 53 fresh
source/header pins, module parameters, dependencies and empty export/alias sets.
The 403 imported symbol CRCs match GKI, RMNET core and WALT.

The port adapts timer setup/deletion, TCP established-socket lookup and
softnet drop accounting. Internal helpers have declarations or file-local
visibility. Five unexported, unreferenced SHS helpers are removed, with their
reference audit recorded in `unused-helpers.json`.

Existing saved fixes preserve successful PERF initialization, propagate APS
and SHS registration failures, drain deferred flow purges, and withdraw packet
and generic-netlink entry points before teardown. SHS bounds its CPU bitmask
and cancels the last global timer before clearing its state.

SHS memory-pressure pruning is limited to the current online CPU with the 6.18
input and process queue locks. It preserves unrelated packets, retains the
native per-queue threshold conditions, updates the matching drop counters and
reports reclaim progress only after freeing packets. It skips the operation
when RTNL is already held or the current CPU is offline. It does not acquire
CPU-hotplug exclusion or wait for another CPU's work from the OOM notifier.

The genuine upstream `cpus_read_trylock` implementation is trimmed from stock
GKI, and DDK cannot add exports to the fixed common build. Keeping reclaim local
avoids both RTNL/hotplug inversion and recursive hotplug-read acquisition when
OOM is entered under hotplug-write exclusion. Reclaiming remote CPU backlogs is
therefore outside this hook's supported behavior. Normal steering and packet
processing are unchanged. With the selected `CONFIG_RPS=y`, the lock order
matches `process_backlog` in pinned `common/net/core/dev.c`: input queue lock,
then `process_queue_bh_lock`, both inside the owning CPU's BH-disabled region.

## Verification

- `/tmp/onyx-rmnet-ext-build11.log`: seven targets pass without compiler or
  modpost warnings; no warning flags or symbol-version checks are disabled.
- `module-verification.json`: 403 matching imported CRCs; all native units,
  aliases, parameters, dependencies and exports checked against ROM evidence.
- `protocol-verification-staged.json`: production PERF initialization/exit
  and four flow-purge paths pass ASan/UBSan; six compiled negative controls fail
  as expected.
- `oom-verification-staged.json`: 100 production-source cases cover queue
  selection, preserved packets, counters, native thresholds, current/offline
  CPUs, locking order, RTNL contention and hotplug context with modeled services.
- `source-hashes.json`, `shared-header-hashes.json` and `replay.log` record
  exact staged inputs, 35 shared provider headers and zero-fuzz, zero-offset
  replay from verified Git blobs.

The saved source work had reformatted the obfuscated native C units. The full
patch therefore includes formatting as well as API changes. Its unfiltered
checkpatch output is saved in `checkpatch.log`: 109 errors and 910 warnings,
principally macro braces, indentation, blank lines and native hex-encoded
license strings. These are not compiler warnings or a checkpatch pass.

## Integration

Root owns integration into the central recipe. Add the supplied 53 imports,
seven modules, Kconfig and defconfig fragments and `patch`; append
`BUILD.fragment` while replacing `//onyx:` references with local `:` labels.
Provider targets are `rmnet_core` and `sched_walt`; their header targets are
`rmnet_headers` and `walt_headers`. No provider, common or baseline file is changed.

Run the supplied verifiers on the integrated source:

```sh
python3 /tmp/onyx-rmnet-ext-handoff/verify_rmnet_ext.py --source-root /home/loukious/Android/konoha-android17-6.18-port-20261031/onyx --negative-controls --output /tmp/onyx-rmnet-ext-integrated.json
python3 /tmp/onyx-rmnet-ext-handoff/verify_oom.py --source-root /home/loukious/Android/konoha-android17-6.18-port-20261031/onyx --output /tmp/onyx-rmnet-ext-oom-integrated.json
```

Both verifiers are self-contained apart from their companion `tests/*.c` files.
Preserve that relative directory when archiving them.

## Limits

This is build and selected source-path validation. It does not establish live
modem/data-plane operation, throughput, steering or tethering behavior, full
module-removal races, real RCU/workqueue/CPU-hotplug behavior, or successful
memory-pressure reclaim on the phone. OOM reclamation is best effort and local
to the current online CPU. The native SHS queue thresholds and resident
RMNET/IPA provider contracts are retained. No device was flashed.
