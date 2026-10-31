# Native RMNET extensions

The seven modules are `rmnet_aps`, `rmnet_offload`, `rmnet_perf`,
`rmnet_perf_tether`, `rmnet_sch`, `rmnet_shs` and `rmnet_wlan`. They retain all
28 native compilation units, 53 source/header pins, dependencies and parameters.
Their native export and alias sets are empty. All 403 imported CRCs matched the
GKI, RMNET core and WALT providers in the isolated build.

The port adapts timers, established TCP-socket lookup and softnet drop accounting.
It preserves successful PERF initialization, propagates registration failures,
drains flow-purge work and withdraws packet/netlink entry points before teardown.
SHS bounds its CPU mask and cancels the final timer before clearing global state.
Five unexported and unreferenced SHS helpers were removed; the
[reference audit](validation/rmnet_ext/unused-helpers.json) records them.

During memory pressure, SHS prunes only the current online CPU's queues. It uses
BH exclusion, the 6.18 input/process queue locks and a nonblocking RTNL lookup,
preserves unrelated packets and native thresholds, and reports progress only
after freeing packets. Remote-CPU reclaim is unavailable through this hook:
stock GKI trims `cpus_read_trylock`, and a blocking hotplug lock plus synchronous
CPU work could deadlock when OOM is entered during CPU hotplug. Normal packet
steering and data paths are unchanged by this bounded reclaim fallback.

The production-function lifetime tests cover PERF setup/exit and four purge
paths and reject six compiled regressions. A separate sanitizer suite passes
100 queue, counter, threshold, CPU-state and RTNL-contention cases. The source
pins, shared headers, patch replay and native module metadata are recorded in
the [validation directory](validation/rmnet_ext/).

```sh
python3 ports/android17-6.18/validation/rmnet_ext/verify_rmnet_ext.py --source-root WORKSPACE/onyx --negative-controls --output rmnet-lifetime.json
python3 ports/android17-6.18/validation/rmnet_ext/verify_oom.py --source-root WORKSPACE/onyx --output rmnet-oom.json
```

The patch contains earlier formatting of the obfuscated native source as well as
API and lifetime changes. Its unfiltered checkpatch log reports 109 style errors
and 910 warnings, mainly macro braces, indentation, blank lines and native
hex-encoded license strings. The kernel builds without compiler/modpost warnings;
checkpatch is not claimed to pass.

Host tests use modeled kernel services. Live modem/data-plane operation,
throughput, tethering, real RCU/workqueue/CPU-hotplug behavior and memory-pressure
reclaim on the phone remain unvalidated. The existing resident RMNET/IPA provider
contracts remain in effect.
