`onyx_relay.ko` is frozen and ready for WLAN integration. This is the actual pinned 6.18.33 common `kernel/relay.c` (`5a54d2ea7de67188274294d346e44a306e36cd03`) compiled as one external module, with the unchanged current kernel `include/linux/relay.h`. The native ROM enabled CONFIG_RELAY=y; the stock GKI does not. No common, baseline, central, or earlier frozen source was edited.

The normal 12-job / 20000-memory build2 passed without compiler or modpost warnings/errors. Its lock-protected stage ran after the final source changes. All 48 imported CRCs match the pinned GKI. It has no module dependencies or aliases. Eight native exports are preserved: `relay_buf_full`, `relay_file_operations`, `relay_open`, `relay_close`, `relay_flush`, `relay_reset`, `relay_subbufs_consumed`, and `relay_switch_subbuf`. None already exists in stock GKI. `relay_prepare_cpu` and `relay_stats` retain their native non-exported status.

Changes:

- Register `relay_prepare_cpu` through dynamic `CPUHP_BP_PREPARE_DYN` at module initialization, propagate setup failures, and remove that exact state at exit. The stock kernel's fixed relay callback is not built.
- Add `THIS_MODULE` file ownership. Hold one balanced module reference per live channel until final channel destruction; file and VMA references retain buffers/channels and provider code. Closing ordinary channels/files balances all references, allowing unload.
- Hold `cpus_read_lock()` across the online CPU snapshot, channel-buffer construction, and channel publication. This excludes CPU preparation that could otherwise miss a new channel and leave an online CPU without a buffer.
- Set the buffer CPU before file creation, and clear its per-CPU slot only when the slot still equals that buffer. A failed CPU1 buffer can no longer clear CPU0's buffer pointer.
- Initialize buffer kref, reader wait queue, IRQ work, data pointer, and padding before `create_buf_file()` may publish/open the file. Reset no longer reinitializes the buffer kref and loses an early reader reference. The initial `subbuf_start()` callback keeps its native ordering after file creation.
- Reject initial nonzero mmap offsets and fault offsets beyond the allocation. Exact allocation-size mappings and valid split-VMA fault offsets remain supported.

Validation:

`verify_relay.py WORKSPACE --source-dir onyx --output FILE` compiles the entire actual source plus actual pinned kernel relay.h using host services and ASan/UBSan. Ten groups pass 52 cases: module setup/exit and unavailable-module paths, allocation failures and invalid channel inputs, failed CPU preparation/retry, global buffers, file/VMA retention and fault bounds, ring/flush/reset/IRQ draining, an open during file publication, pthread CPU-hotplug serialization, file read/poll after channel close, and partial channel-creation failure retaining an already-open file.

`--negative` compiles and rejects eleven deliberate regressions: wrong buffer CPU, legacy wrong-slot clearing on failure, missing hotplug read lock, masked CPUHP error, missing CPUHP removal, missing file owner, missing channel module reference, missing IRQ work draining, losing the early reader reference, missing fault bound, and missing mmap offset check. Strict checkpatch is 0 errors / 0 warnings / 0 checks. Exact pinned blob replay uses zero fuzz/offset; development, staged, replayed, and frozen source bytes match. Reports bind the exact production source, kernel header, and harness hashes.

Patch SHA256: `2c2f387dd25083d0da88b4917f4aee5374bf0ff9c6c0a8662785964b7a6d703b`.
Module SHA256: `c6b668af25df97e0b4ae5d39ed3403fa1cc5abeab815235893650cb6fc17528f`.

Integration inputs are `imports.json`, `modules.json`, `BUILD.fragment`, `Kconfig.fragment`, `defconfig.fragment`, **`relay_defconfig`**, `patch`, `tests/relay.c`, `verify_relay.py`, reports, and `frozen-source/`. `relay_defconfig` deliberately contains only CONFIG_RELAY=y and is applied only to the provider target; the standalone build prepends the central platform settings. The central integrator should make the dedicated relay configuration include the central platform settings as appropriate. Do not turn CONFIG_RELAY on globally: other modules continue to use their real native configuration. WLAN's seven relay imports resolve by adding a dependency on this provider.

Lifetime contract and limits:

Clients must quiesce producers before `relay_close()` and must not use the channel afterward. `relay_reset()` retains the native requirement that the channel be idle. `remove_buf_file()` must withdraw the file and drain new opens before returning, as native debugfs removal does; open files/VMAs may outlive channel close. Readers and faults after close use retained buffer/channel data and do not call client callbacks. File publication must not leave a usable file behind when the creation callback reports failure. Module init/exit and forced-unload bypasses are not simulated. Host allocation, VFS, page mapping, module references, IRQ work and hotplug services are mocks; no actual Linux module load, CPU-hotplug, userspace mmap, WLAN transport, phone boot or hardware test is claimed.
