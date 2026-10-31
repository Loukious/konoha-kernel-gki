# Onyx Android 17 / Linux 6.18 port

This directory contains the first implementation of the POCO F7 / SM8735 port.
The pinned **6.18.33 GKI and 115 vendor modules build successfully**. This is
an initial platform build, **not a flashable kernel or a boot-tested port**.

The branch stores the port recipe in this directory. Its top-level kernel is
still the validated 6.6 tree; the commands below create and build a separate
6.18 workspace. Do not use the top-level 6.6 build scripts for this port.

## Build

Requirements: Linux x86-64, Python 3.11+, `repo`, Git, GNU patch, and kmod with
extended-modversions support (validated with kmod 34.2). Kleaf downloads are
pinned in the manifests, including Clang `r584948c` and Rust `1.91.1.p3`.
Allow roughly 40 GiB for this initial workspace; a full device build will need
more. The validated build used 12 jobs and a 20,000 MB Bazel memory allowance.

From this repository:

```sh
python3 ports/android17-6.18/prepare.py /path/to/new-6.18-workspace
python3 ports/android17-6.18/build.py /path/to/new-6.18-workspace
```

`prepare.py --reference /path/to/existing/repo/workspace` can reuse local Git
objects. Existing workspaces must have the expected manifest revision and no
unexpected local manifests or tracked source changes. `--verify-only` checks
all 46 selected project revisions without syncing. The original ROM checkout
and stable kernel checkout are not build destinations.

`build.py` checks the source pins, stages exact Git blobs, applies the patches,
and builds `//common:kernel_aarch64` plus `//onyx:platform_modules`. It keeps the
stock KMI checks and compiler warnings enabled. Outputs go to
`WORKSPACE/artifacts/TIMESTAMP/`, including:

- GKI Image variants, vmlinux, symbol tables, configuration, headers and modules.
- 115 platform `.ko` files, their symbol tables and DDK configurations.
- Merged Tuna/Tuna7 DTBs, the Onyx board overlay, `dtb.img` and `dtbo.img`.
- Input manifests, source/blob pins, patches and staged-file hashes.
- A build log and `report.json` with SHA-256 hashes, imported-CRC checks and
  duplicate-provider rejection. The build/staging/verification tools are archived too.

The default Kleaf build uses `6.18.33-android17-5-maybe-dirty-4k` without SCM
stamping. The common source is clean; exact revisions are recorded separately.
This suffix is not evidence of unrecorded source changes.

For direct iteration after preparation:

```sh
python3 ports/android17-6.18/stage.py /path/to/new-6.18-workspace
cd /path/to/new-6.18-workspace
tools/bazel build --jobs=12 --local_resources=memory=20000 //onyx:platform_modules
```

Edit the tracked recipe/patches when changing the port. `stage.py` refuses to
replace a staged tree with local edits. Preserve such edits as a patch before
restaging. The imported pin-control private headers come from the exact 6.18
common revision, not the older vendor kernel.

## Platform driver groups

| Output | Implementation | Adaptation |
| --- | --- | --- |
| `qcom-scm.ko` | Pinned Onyx SCM, SMC/legacy call paths and QTEE shared-memory bridge | 6.18 removal callback; internal helper visibility; DDK config and vendor headers |
| `pinctrl-msm.ko` | Pinned Qualcomm vendor pin-control core | GPIO setter return type, pinconf enum rename, DT property iterator, exported debug declarations |
| `pinctrl-tuna.ko` | Pinned Tuna pin/function/PDC/I3C tables | 6.18 removal callback; remove an overwritten initializer while retaining its effective value |
| `hwid.ko` | Pinned Xiaomi hardware-ID parameters and exports | Module description |

The original four appear in the ROM's first-stage module lists. SCM depends on GKI
exports, pinctrl-msm depends on SCM, and pinctrl-tuna depends on pinctrl-msm.
The vendor wakeup, eGPIO ownership, reserved-GPIO and I3C helper behavior is
retained. No dummy secure-monitor functions or symbol-version bypasses are
used. The current [validation report](validation-20261031.json) checks 5264
imported symbol CRCs across all 115 modules.

The platform group also builds IPC logging, PDC interrupts, command DB,
CRM v2, RPMh, hardware spinlocks, SMEM, SoC information, the SPMI PMIC arbiter,
Qualcomm regmap debugfs and the PMIC MFD. `modules.json` records the expected
outputs, and `build.py` verifies this set before archiving any successful build.

The adapters retain Onyx RPMh/CRM transactions and the 5000 ms timeout. They
update IRQCHIP probing, trace-string assignment, platform removal callbacks,
the SPMI device lookup and unaligned access headers for 6.18. Private hwspinlock
and regmap headers come from the pinned 6.18 common to match core layouts.
Regmap debug writes remain disabled. IPC logging and UFS link to the minidump
provider and enable their vendor crash-buffer registrations.

The regulator group builds the vendor RPMh regulators plus debug/proxy-consumer
libraries. Legacy proxy cleanup remains disabled, matching the Onyx baseline;
proxy votes are released by the driver's sync-state callback.

The clock group includes the vendor common composite (with GDSC), GDSC
regulators, RPMh and dummy clock providers, and all nine Tuna controllers:
GCC, TCSR, GPU, display, camera, camera BIST MCLK, video, EVA and debug.
It retains the vendor PLL, RCG, voltage-voting and CRM behavior. Internal helpers
are made static; trace strings and DT iterators use 6.18 interfaces. The critical
clock walk now keeps node references alive through parent traversal, releases
parsed clock references on every path, and skips failed phandle parses.

The interconnect group builds the BCM voter, RPMh provider, QoS and debug
libraries, and the complete Tuna NoC topology. It uses the stock GKI translation
helper (whose behavior matches the vendor implementation) and its 6.18
header. The private interconnect core layout also comes from pinned common.

The storage PHY and PMIC group builds the vendor UFS PHY core and Sun PHY
calibration tables, SPMI SDAM NVMEM, PM8xxx RTC, power-on logs, reboot reasons,
PON and stub-regulator driver. The reboot-mode library comes from 6.18 common.
The RTC retains its `rtc_us_val` attribute using device sysfs APIs exported by
GKI, with managed removal before RTC teardown. Onyx's reboot-reason DT node
uses the NVMEM cell path; the legacy QPNP power-on fallback remains disabled.

The UFS host group adds `ufs-qcom`, `qcom_ice` and `ufshcd-crypto-qti`.
It retains the Onyx hardware-wrapped-key profile, vendor power negotiation,
Sun PHY integration and MCQ register layout. The adapters use 6.18 power-mode,
block-crypto and platform-MSI interfaces. ESI teardown unregisters CPU-hotplug
callbacks and frees IRQ handlers before MSI descriptors; DT affinity accesses
are bounded. A failed secure key-programming call now releases its shared
buffer. Clock scaling retains the baseline min/max frequency tables. These
build checks do not establish storage or decryption operation on the phone.
Checkpatch reports no errors and one advisory for the intentional `NR_CPUS`
bounds check on firmware CPU indices before fused-CPU remapping.

`CONFIG_QCOM_SCM=m` and `CONFIG_QTEE_SHM_BRIDGE=y` are set in the DDK module
configuration. Common Kconfig also selects `QCOM_TZMEM`; the vendor composite
module uses its own included QTEE allocator and does not link the upstream
TZMEM implementation. These module options do not change the stock GKI config.

The Gunyah group builds `gh_arm_drv`, `gh_dbl`, `gh_msgq`, `gh_rm_drv`,
`gh_irq_lend`, `gh_mem_notifier` and `gh_ctrl`. It retains the Onyx hypercall
wrappers and RPC payloads while using the pinned 6.18 resource-manager API.
The vendor RM obtains its context from the auxiliary device's platform data
and looks up the Onyx `/hypervisor` device-tree children explicitly. Probe
failure and removal unregister the RM notifier and drain a dedicated workqueue
before releasing context; VM-status and guest-power cleanup track successful
registration. The auxiliary aliases are included in module metadata.

SCM now declares its dependency on `gh_rm_drv` and enables the real VM identity
and memory-assignment helpers instead of the optional header fallbacks.
Firmware negotiation and secondary-VM operation remain untested.

The QRTR group builds the pinned Onyx core, name service and Gunyah transport,
including the vendor routing, wakeup and process-deletion interfaces. The node
ID and wakeup defaults remain 1 and 0 ms, with the baseline DT override. Trace
strings use the 6.18 API, and the node-ID walk releases each node exactly once.
The secure-VM group builds `mdt_loader`, `secure_buffer` and `gunyah_loader`,
including the Onyx proxy scheduler and virtio backend. The vendor firmware
loader, wrapped memory assignments and `/dev/qgunyah` UAPI remain intact.
QRTR host transport and IRQ lending now link to the real VM lifecycle notifier.
The IRQ eventfd registration holds its file descriptor only during setup and
retains an eventfd context for asynchronous notifications, using the 6.18 fd
and eventfd APIs. Loader initialization propagates errors and unwinds earlier
registrations. Proxy callbacks are installed atomically after initialization;
removal waits for in-flight resource callbacks before releasing the proxy data.

Guest-power operations stay disabled, matching the recorded Onyx HLOS config.
The TLMM VM IRQ-chip remains disabled, matching the recorded HLOS config. VM firmware loading, proxy
scheduling, memory assignment and virtio operation have not been tested on the
phone. Checkpatch finds no code-style errors or warnings in these adaptation
patches (commit-message and sign-off checks do not apply to source diffs).

The IOMMU group builds `arm_smmu`, `qcom_iommu_util`,
`msm_dma_iommu_mapping` and `iommu-logger`. It retains the Onyx SMMU/TBU,
secure page-table assignment, fast mapping, fault registers and DMA-BUF mapping
implementations. Paging allocation and per-domain page sizes use 6.18 APIs;
identity and blocked domains have separate stream-map operations. Qualcomm
extensions look up registered domain operations instead of treating every
core domain as a vendor object. Controller initialization precedes IOMMU
registration, which can synchronously attach consumers in 6.18.

DMA-domain fault handlers use private storage because 6.18 shares core handler
storage with the DMA cookie. Removal waits for active threaded callbacks.
Unrecoverable per-device faults retain their 6.6 payload through the new
`qcom_iommu_*device_fault*` interfaces, with device references and SRCU lifetime
protection. Later WLAN imports must use these interfaces. Fast mappings retain
unmanaged domains, reserve MSI space, and select their DMA ops explicitly.
Out-of-aperture reserved ranges cannot underflow the allocation bitmap.
Shrinker setup uses the 6.18 allocation API; restricted DMA hook registration
follows fallible initialization. The full build has no compiler or modpost
warnings. Checkpatch has one advisory for the required `-ENOSYS` fault-handler
return convention used by the IOMMU core. Hardware faults, memory isolation,
GPU translation and suspend/resume remain untested.

The cache and interrupt group adds `llcc-qcom`, `llcc_perfmon`,
`cpu_phys_log_map`, `qcom_llcc_pmu` and `gic_intr_routing`. LLCC configuration
and event tables retain the Onyx implementation. CPU mapping bounds-checks
cluster/core indices and keeps DT nodes alive through traversal. LLCC PMU
registration follows CPU-index and MMIO validation, with managed unregister
on removal; perf events reject unsupported CPU and sampling requests. The
perfmon timer uses `hrtimer_setup`, and sysfs is exposed after initialization
and removed before timer teardown.

Interrupt routing uses IRQ private headers from pinned 6.18 common and the
exported IRQ-count accessor. Registration unwinds ordinary trace and CPU-hotplug
hooks on failure, installs the permanent affinity hook last, and keeps the
module resident. This group passes checkpatch without errors, warnings or
style checks. Cache behavior, counter accuracy and IRQ balancing still require
phone testing.

The AOSS/mailbox group builds `qcom_aoss`, `msm_qmp` and `qcom-ipcc`.
It preserves the Onyx shared-memory protocol and the QMP shim used by Tuna's
AOP mailbox. Removal uses 6.18 callbacks, stops IRQ handlers before deleting
IRQ mappings, and drains queued work before unregistering mailbox channels.
The AOSS shim does not operate on the absent legacy RX IRQ. QMP handle/channel
references are managed, and failed setup releases the resources it acquired.
Transmit shutdown is synchronized with the submission lock to prevent work
from being queued during cleanup. Literal message payloads use bounded string
formatting, and logs show the formatted message. The adaptation passes
checkpatch without errors, warnings or style checks. Firmware acknowledgments
and power-state transitions require hardware validation.

The memory-sharing group adds `mem_buf_dev`, `mem_buf_msgq`, `mem_buf`,
`qcom_dma_heaps` and `deferred-free-helper`. The system heap, automatic page-pool
refill, secure system heaps, CMA and carveout heaps use the recorded Onyx
configuration. Heap registration waits for the memory-permissions driver's
VM identity initialization. DMA-BUF and DMA-heap namespace imports, page-order
bounds and shrinker allocation use 6.18 interfaces. Pool objects are fully
initialized before reclaim can see them, and failed allocations unwind their
worker and device resources.

The vendor `/sys/kernel/dma_heap/total_pools_kb` statistic and
`dma_heap_try_get_pool_size_kb` export remain available through a module-side
registry, preserving the baseline's per-heap accounting, including aliases.
The memory-buffer ioctl and VM-device UAPI are unchanged. VM registration avoids
double frees and releases device references on failure/removal. Memory grants
can outlive local file descriptors, so the memory-sharing service remains
resident, as do the registered heaps.

Gunyah message queues now support explicit shutdown: blocked sends/receives
wake, receive threads stop, and queued memory work drains before descriptors
are freed. The deferred-free worker also supports stop and drain. Unknown queue
names fail cleanly, and receiver allocation/thread failures unwind registration.
These adaptations pass checkpatch with no errors, warnings or style checks;
the full build passes 4308 imported-symbol CRC checks without compiler/modpost
warnings. Cross-VM sharing, secure-display allocation and memory pressure still
require device testing. `ZONE_DEVICE` and reserved-IPA support remain disabled,
matching the Onyx HLOS baseline; guest-only mapping paths are not validated.

The CPU power-control group adds `qcom_cpucp`, `qcom_scmi_vendor` and
`qcom_scmi_client`. Tuna uses the original two-channel CPUCP mailbox and
vendor SCMI protocol 0x80. The Sun FAST mailbox client is not required by
Tuna's device tree. Interrupt handlers wait for channel initialization and
are freed before controller teardown; v2 shared receivers acknowledge only
their channels and serialize shared register updates.

The vendor SCMI protocol uses private definitions from pinned 6.18 common and
the required `Qualcomm` vendor identity/module alias. Requests retain their
16-byte vendor header and command IDs. Payload sizes are bounded before buffer
allocation, and oversized replies release the transfer before returning an
error. The client remains resident with bind attributes suppressed because
consumers retain its handle; the built-in SCMI parent also suppresses unbind.
This group builds and passes checkpatch without warnings. Firmware replies,
DCVS decisions and suspend/resume still require phone validation.

The bus frequency-control group adds `dcvs_fp` and `qcom-dcvs`. It retains
Tuna's DDR, LLCC, L3 and DDR-QoS votes and frequency tables. Services remain
resident; consumers wait until root, hardware and path initialization complete.
Probe failures unwind child devices, sysfs, interconnect handles and fast-path
registrations. Voter names are owned by the service, and RCU protects fast-path
lookups during voter removal.

L3 voting state and mappings belong to each path. Register offsets and table
reads are bounded; Tuna's 0xa0-byte L3 table region holds all 40 four-byte rows.
Per-CPU votes update the per-CPU frequency cache. RPMh fast votes use MMIO and
bounded busy waits, so the RCU-protected call path does not sleep. This group
passes the full build, CRC verification and checkpatch without warnings;
frequency transitions and workload response still need hardware testing.

The PMU group adds `qcom-pmu-lib`, preserving Onyx's AMU/perf counters,
LLCC reads and optional CPUCP counter cache. CPU indices and event tables are
validated before use. Counter setup and hotplug are synchronized; idle/PM
callbacks and active readers drain before perf events are freed. The service
and its sysfs interface remain resident, and idle-client removal waits for
RCU readers. GKI's exported CPU-hotplug wrapper handles state removal outside
the caller's CPU lock. Tuna's five configured events and eight-CPU mask are
retained; its DT has no CPUCP PMU shared-memory region. Counter accuracy,
hotplug, idle transitions and firmware behavior still need device testing.
The group passes the full build, imported-symbol checks and checkpatch without
warnings.

The CPU-frequency group adds `qcom-cpufreq-hw` and
`qcom-cpufreq-thermal`. It retains Tuna's four EPSS domains, vendor LUTs,
per-core votes and cycle-counter export. Hardware pressure uses 6.18's kHz
interface; boost and standard frequency attributes use the current cpufreq
core. Policy mappings, IRQs and sysfs files unwind on failure and exit.
Interrupt enable/disable state is balanced across polling and hotplug, and
RCU protects cycle-counter readers during policy teardown. Mailbox thermal
receivers stop before releasing policy references.

The QFPROM group adds `nvmem_qfprom` and `qfprom-sys`. The vendor read alignment,
Tuna keepout ranges and read-only device-tree configuration are retained.
Binary sysfs callbacks use the 6.18 signature, respect read offsets and remove
files before dropping their NVMEM cells. Both groups pass checkpatch and the
full build without warnings. Frequency transitions, thermal notifications and
fuse-cell values remain untested on the device.

The crash-symbol service adds `debug_symbol`. It uses the pinned 6.18
`debug_kinfo` structure and unsigned relative kallsyms offsets, validates the
snapshot checksum, and publishes fully initialized lookup tables to readers.
Reserved-memory setup releases DT references and preserves a mapping already
populated by the core. The service remains resident because the core and crash
consumers retain its pointers. An [offline validation](debug-symbol-validation-20261031.json)
decoded all 146,574 linked kernel symbols and matched them to `System.map`,
including all 11 names used by the diagnostic cache. The reserved-memory
handoff and live crash capture still need device testing.

## Baseline inventory

The sources in `sources.json` match the local crDroid 17.0 ROM lineage. Capture
the ROM's actual local changes and existing outputs without modifying them:

```sh
python3 ports/android17-6.18/inventory.py \
  --rom-root /path/to/custom-rom \
  --output /path/to/empty-input-snapshot \
  --extra-module /path/to/custom-rom/kernel/xiaomi/sm8735-modules/qcom/opensource/wlan/qcacld-3.0/qca_cld3_wcn7750.ko
```

An optional `--runtime-modules /path/to/proc-modules.txt` compares a captured
runtime module list. The local inventory found 469 core outputs, 106 external
outputs and the explicit WCN7750 output. All required entries in the 116
first-stage, 271 second-stage and 81 vendor-DLKM lists are accounted for.
The existing outputs were inventoried, not rebuilt. Local WLAN and haptic
changes are captured for later porting; they are not in this initial group.

## Next implementation gates

1. Complete the remaining first-stage dependencies, including CPU thermal controls.
   Test UFS/encryption, probe ordering, GPIO wakeups, SCM wait queues and
   shared-memory operations.
2. Port the device trees and remaining first-stage list; produce matching
   vendor_boot, DTBO and DLKM images with complete dependency/load lists.
3. Validate WALT scheduling and port KGSL/display, thermal, audio, camera, touch,
   fingerprint, WLAN and other device modules. The hook mismatches in the
   [feasibility study](../../Documentation/konoha/android17-6.18-feasibility.md)
   still need semantic fixes.
4. Integrate KernelSU/NetHunter after the device baseline works. Perform boot,
   hardware and suspend/resume validation using the resulting test package before
   declaring a stable release.

Compilation, matching CRCs and preserved tables do not validate firmware
protocols, device-tree compatibility, probe ordering or hardware behavior.
No 6.18 image has been flashed or booted on the phone.

The crash-dump group adds `minidump`, `qcom_logbuf_vendor_hooks` and
`qcom_logbuf_boot_log`. It preserves the Onyx 200-entry SMEM table and panic,
ftrace, CPU-context, CPU-frequency, memory and boot-log capture configuration.
Private printk, scheduler, time and MM layouts come from pinned 6.18 common.
Slab diagnostics use the exported object/track iterators, and DMA-BUF iteration
retains references and copies names under their lock. WALT-specific fields
remain conditional until the scheduler module is ported.

Registration publishes completed tables under the pending-list lock. Pending
SMEM entries receive distinct slots and matching ELF headers. Region updates
serialize with registration/removal, reject mismatched indices/sizes, and keep
physical and virtual addresses distinct. ELF allocation accounts for metadata
headers at full capacity; failed removals preserve existing tables. The RM
workqueue is ordered and allocates pending requests before firmware registration.
The minidump and boot-log services remain resident while firmware and callbacks
retain their data; log-buffer probing defers until debug symbols are available.

`verify_minidump.py WORKSPACE/onyx` compiles the actual table helpers into a host
harness and exercises 400 SMEM/RM full-capacity, update, removal and refill
scenarios with AddressSanitizer and UndefinedBehaviorSanitizer. The recorded
result is in [minidump-validation-20261031.json](minidump-validation-20261031.json).
The harness also rejects the former pending-slot indexing bug. These checks
cover table arithmetic, not locking, firmware, panic handling or phone operation.
This adaptation passes checkpatch without errors, warnings or style checks.

The watchdog group adds `qcom_wdt_core` and `gh_virt_wdt`, retaining the Onyx
11,000 ms bark interval, 9,360 ms pet interval, CPU pings, IRQ statistics and
panic bite behavior. Timer callbacks use current APIs. Both worker waits can
be stopped, and failed setup unwinds only resources that were registered.
Per-CPU IRQ data is initialized for every possible CPU; hotplug teardown runs
before releasing IRQ storage. IRQ statistics initialize their ranking table
and look up descriptors again under RCU before displaying names.

Sysfs operations wait for completed setup, and suspend/resume serialize with
permanent disable. The worker cannot reset hardware or rearm its timer during
suspend. Firmware errors are propagated on enable/disable and PM operations;
panic timeouts are bounded by the Gunyah protocol's 16-bit limit. The recovery
service remains resident because firmware and minidump retain its data.
The adaptation passes checkpatch without errors, warnings or style checks.
Watchdog firing, suspend/resume and firmware behavior still require phone tests.

The vendor-hook group adds `qcom_cpu_vendor_hooks` and
`qti_thermal_vendor_hooks`. CPU-stop diagnostics, printk hotplug behavior,
timer-wheel adjustment and KASLR offset recording retain the Onyx behavior.
The KASLR IMEM mapping is sized and established during initialization; resume
only writes the existing mapping. The 6.18 GKI export `kimage_vaddr` retains its
original type and passes the normal symbol CRC check. Hook failures unwind
registered callbacks; exit drains trace readers and unregisters syscore and
optional probe callbacks before releasing mappings.

Thermal suspend notification remains enabled. The removed cooling-statistics
hook is unnecessary with the pinned GKI's `CONFIG_THERMAL_STATISTICS=n`, which
the module Kconfig requires. Both adapters pass checkpatch without errors,
warnings or style checks. Runtime CPU-stop, resume and thermal behavior remains
unverified on hardware.

The temperature-sensor group adds `qcom_tsens`, retaining Onyx calibration,
thresholds, cold interrupts and persistent minimum/maximum readings. Thermal
private headers come from pinned 6.18 common. Shared helpers use per-trip
cooling-instance lists and leave reached-trip accounting to the current core.
Writable hysteresis uses the core implementation, replacing the old no-op
callback. Cold-status reads write a complete integer value.

Each controller owns its feature flags. Sensor counts are bounded, probe
deferral propagates, and IRQ wake references unwind with their handlers.
Every thermal zone is registered before its IRQ handlers so teardown stops
handlers before releasing zones, logs and register maps. Debugfs cleanup is
per controller. Temperature decoding distinguishes signed readings from
register errors and rejects unstable samples without overwriting the result.

`verify_tsens.py WORKSPACE/onyx` exercises the actual conversion helpers under
AddressSanitizer and UndefinedBehaviorSanitizer: all 4096 signed 12-bit values,
plus ten stability, injected-error and ADC-calibration cases. The recorded
result is in [tsens-validation-20261031.json](tsens-validation-20261031.json).
This adaptation passes checkpatch without errors, warnings or style checks.
Register access, IRQ wakeup, thermal policy and temperature accuracy still
require testing on the phone.


The PMIC battery-monitor group adds `bcl_pmic5`, retaining Onyx's current,
voltage, low-battery IRQ levels and PMIC-specific register/scale tables. The
voltage trip callback uses the 6.18 trip descriptor and bounds-checks register
indices. ADC multiplication widens before arithmetic; CCM thresholds saturate
before narrowing. BPM snapshots decode little-endian bytes into aligned local
storage, release the hardware hold after read failures, and leave caller output
unchanged on error.

Controllers have managed IDs, logs, notifiers and debugfs directories. All
thermal zones precede every IRQ registration; disabled IRQs remember the
threshold selected during zone setup and arm only after their handlers exist.
Zone disable and device teardown balance IRQ/wake state, and late callbacks
cannot rearm released interrupts. NVMEM and IRQ probe deferrals propagate.
Notifier callbacks are initialized before registration and drained before
storage is freed. Statistics snapshots serialize with IRQ updates.

`verify_bcl.py WORKSPACE/onyx` checks 196,608 signed ADC conversions, integer
boundaries, CCM saturation, injected snapshot failures and IRQ/wake lifetimes
with AddressSanitizer and UndefinedBehaviorSanitizer. The harness also rejects
the former 32-bit multiplication overflow. The [recorded result](bcl-validation-20261031.json)
covers host helpers; it does not exercise real IRQ concurrency or the PMIC.
The adapter passes checkpatch and the full platform build without warnings.
Battery accuracy, voltage-threshold propagation and mitigation need phone tests.


The state-of-charge group adds `bcl_soc`. The sensor retains the Onyx
`100 - battery capacity` reading and uses the 6.18 core's writable hysteresis.
Each device owns its callbacks and work. Battery references are acquired and
released per read; unavailable or invalid readings report an error without
inventing a charge level. Battery notifications retry initial reads when the
power-supply device appears after the thermal driver.

Work starts only after thermal-zone setup and managed cleanup are ready.
Teardown prevents further submissions, unregisters the power-supply notifier,
and cancels work before unregistering the zone. The module includes its OF
autoload alias. The adaptation passes checkpatch and the full platform build
without warnings; capacity notifications and low-charge cooling still need
phone validation.


The PMIC temperature-alarm group adds `qcom-spmi-temp-alarm`. It retains the
Onyx GEN1, GEN2, TEMP_DAC and TEMP_LITE tables, stage-2 shutdown requirements,
optional IIO readings and hardware-monitor sysfs interface. Trip callbacks use
6.18 descriptors, and initialization bounds the hardware trip count before
writing thresholds. The early GEN2 revision uses the same legacy threshold
table as pinned common.

Initialization takes the thermal-zone lock before the driver lock, matching
runtime callbacks. Temperature reads serialize with setup, failed reads leave
the result untouched, and the initialized flag is set only after the alarm
enable write succeeds. IRQ handlers are registered after the thermal zone and
hwmon setup, so managed teardown stops handlers first. The Onyx main/Lite
three-trip tables remain unchanged. The adapter passes checkpatch and the full platform
build without warnings; register access, ADC readings and hardware shutdown
need phone tests.


The PMIC GPIO and ADC group adds `pinctrl-spmi-gpio`, `qcom-vadc-common`
and `qcom-spmi-adc5-gen3`. GPIO writes return register errors and defer until
the parent IRQ domain exists. ADC channel tables, timing and scale constants
retain the Onyx Gen3/Gen4 variants. Thermistor resistance stays 64-bit through
interpolation, fixing a cold-to-hot wrap near the Gen4 open-circuit limit.
Scale dispatch validates its inputs before accessing tables.

ADC conversion IRQs are available during zone registration, while thermal work
starts only after all zones exist. Teardown stops new submissions, completes
pending waits, removes IRQ handlers, drains work and disables hardware without
holding a mutex across joins. Hibernation freeze, thaw and restore balance IRQ
state. DT addresses, channel types and interrupt indices are bounded.
Nonthermal clients own copied registrations, can rearm or remove themselves
from callbacks, and drain callbacks when released. Failed requests remove their
registration. Managed device links acquired under the supplier device lock
prevent retained ADC handles from outliving their consumer; concurrent supplier
probe or unbind defers acquisition.

`verify_adc_scaling.py WORKSPACE/onyx` builds the actual scaling source with
AddressSanitizer and UndefinedBehaviorSanitizer. It checks 3,145,728 scale/code
cases, thermistor monotonicity over each generation's raw range, and 330,002
threshold pairs at every integer millidegree from -40000 to 125000. The
[recorded result](adc-scaling-validation-20261031.json) covers host arithmetic;
ADC calibration, callbacks, IRQ delivery and hardware behavior need phone tests.
Both patches pass checkpatch and the full module build without warnings.


The crash-capture group adds `memory_dump_v2`, `dcc_v2` and `qcom-dload-mode`.
Memory-dump tables retain Onyx's firmware layout and SCM/IMEM handoff.
Registration is serialized and bounded to 336 entries, with client access
published only after setup succeeds. Static regions are sized before mapping,
and one mapping covers the whole minidump range. The IMEM fallback validates
the size-word address against the parent window. CPU register-table arithmetic
widens before multiplication; reset and configuration serialize. Dynamic dumps
remain disabled, matching the ROM configuration. The provider remains resident
because firmware has no API to release all retained descriptors.

DCC retains the hardware register maps and descriptor encoding. Byte-range
reads use aligned SRAM accesses. Open handles retain their character-device
object and return an error after removal; removal drains reads and sysfs before
releasing hardware state. DT list lengths, register offsets and PM snapshot
bounds are checked. Freeze/restore serialize with configuration, and an aborted
hibernation tolerates an inactive controller. Download mode retains its boot,
panic and reboot policies while giving the sysfs object a release callback and
unwinding notifier registrations on failure.

`verify_crash_capture.py WORKSPACE/onyx` checks concurrent full-capacity tables,
malformed table offsets/counts, CPU count overflow and 4,757 DCC byte-range cases,
including allocation/copy failures and read/removal serialization. The
[recorded result](crash-capture-validation-20261031.json) uses AddressSanitizer
and UndefinedBehaviorSanitizer on the actual helpers. Checkpatch and the full
build pass without warnings. Firmware handoff, crash capture and PM behavior
still need device tests.


The Xiaomi diagnostics group adds `miev` and `boottime`, completing the four
entries in the device-specific first-stage list. The event device retains its
record-oriented userspace interface and oldest-record eviction policy using
6.18's record FIFO helpers. Undersized reads return `EMSGSIZE` without consuming
the record; user-copy failures preserve the queue. Initialization publishes
file operations only after their resources exist, and file ownership pins
module storage.

Event formatting has no shared mutable length counter. Each event is bounded
to 4096 output bytes, escapes JSON keys/strings, handles integer extremes, and
propagates allocation or output failures while retaining unsubmitted payloads.
Boot logging publishes complete immutable entries, bounds the 960-entry table,
and retries failed block allocations without replacing existing storage. Proc
removal and tracepoint synchronization precede buffer release. Trace registration
errors unwind, and an explicit stop drains callbacks even after the log filled.

`verify_xiaomi_diagnostics.py WORKSPACE/onyx` runs the actual event code and 6.18
FIFO helpers, checks wrapped indices, whole-record eviction, short reads and
copy failures, and decodes 127 escaped JSON key/value pairs. It also checks
exact output limits, allocation failures and concurrent boot-log capacity under
AddressSanitizer and UndefinedBehaviorSanitizer. The
[recorded result](xiaomi-diagnostics-validation-20261031.json) covers host data
paths; module lifecycle, tracepoints and phone operation remain untested. The
patch passes checkpatch and the full platform build without warnings.


The scheduler group adds `sched-walt` and reconnects UFS storage boosting.
Task data uses GKI's dynamic task allocation, including the special `init_task`
pad, rather than the six inline vendor words in 6.18. **Boot images must include
`android_arch_task_struct_size=512` in the kernel command line.** Module loading
checks the allocator's actual size through the read-only debug-symbol service
before installing hooks. The module rejects CPU maps beyond its eight-CPU
storage. Minidump reads WALT task data only after scheduler initialization.

Capacity accounting mirrors current hardware and cpufreq pressure while
preserving WALT's internal frequency caps. Delayed CFS entities are excluded
from runnable accounting; fair task replacement leaves entity transitions to
6.18 core code and rejects delayed or throttled candidates. MVP runtime uses
the scheduling donor, and proxy RT migrations retain the core's selection.
Binder boosts restore only when the transaction actually inherited a boost.

Hook errors keep the scheduler disabled. Top-task buffers are allocated before
`stop_machine`, topology/counter waits are bounded, and counter drivers start
after cluster discovery. Counter reads use modular subtraction at wraparound;
removal drains readers before register mappings disappear. Sysctl registration
uses const counted tables, removes the obsolete null sentinel and unwinds
partial failures. Input-boost setup releases policy references and unwinds QoS
requests on failure. Restricted scheduler hooks cannot be unregistered, so the
module remains resident, including on asynchronous initialization failure.

`verify_walt.py WORKSPACE` tests actual staged helpers under AddressSanitizer
and UndefinedBehaviorSanitizer: 4,097 task-storage cases, 11,023,650 capacity
cases, 531,441 utilization-clamp cases, 1,000,006 counter updates and 42 sysctl
registration/failure cases. The [recorded result](walt-validation-20261031.json)
does not exercise the full scheduler, hardware counters, CPU hotplug or phone
boot. Checkpatch and the 108-module build pass without warnings, with 4,957
imported-symbol CRCs verified. WALT behavior and thermal coordination still
require device testing before release.


The CPU cooling group adds `thermal_pause` and `cpu_hotplug`, both used by
Tuna's thermal device tree. Pause requests are combined before applying per-CPU
WALT votes, so overlapping cooling groups cannot release each other's votes.
State reads report actual applied cooling; failed operations retry a bounded
number of times. Workqueue operations and notifier callbacks run outside the
request mutex, and generation counters preserve requests received during work.

Hotplug cooling vetoes online requests with `-EBUSY` while cooling is requested.
It restores only CPUs it offlined, preserving CPUs already offline for another
reason. Removal stops callback producers and drains work before releasing CPU
votes, DT references and workqueues. WALT's drain thread uses a wait queue and
captures pending requests before processing, preserving requests arriving during
an earlier drain. Protected-CPU checks precede changes to the halt mask.

`verify_cpu_thermal.py WORKSPACE` exercises the staged workers and drain helpers
under AddressSanitizer and UndefinedBehaviorSanitizer: 65,025 overlapping mask
pairs, 256 initial online masks, 65,536 drain-request pairs, plus operation
failures, bounded retries, teardown and reentrant requests. The
[recorded result](cpu-thermal-validation-20261031.json) uses simulated kernel
services; it does not test real workqueues, thermal hardware or CPU hotplug.
The full 110-module build and both adaptation patches pass without warnings.
Build archives now include `boot-parameters.txt` for image assembly.


## Device-tree assembly and first-stage inventory

`build.py` now invokes `build_devicetrees.py`. Standalone device-tree builds use:

```sh
python3 ports/android17-6.18/build_devicetrees.py /path/to/6.18-workspace
```

`devicetrees.json` records the ROM's ID-based overlay selection: two Tuna SoC
bases, multimedia/service overlays, and the Onyx board overlay with its camera,
audio, display, WLAN and Bluetooth additions. The builder uses pinned vendor
binding headers, Clang and DTC, plus a pinned LineageOS libfdt with Qualcomm's
`fdtoverlaymerge` implementation. The additional tools source is a separate
project; it does not replace Kleaf's DTC. The host C compiler builds these two
small tools, and their binaries and source hashes are archived.

The builder preserves platform, board, PMIC and Xiaomi IDs; validates FDT
structure and phandle uniqueness; applies the board overlay to both SoC bases;
and compares every hardware node/property produced by libfdt and Android's
libufdt. Their compiler symbol tables differ as expected: libufdt does not add
new overlay labels. The DTBO container's header, padding, payload and partition
size are checked. Output hashes and active-device inventories are in the
[device-tree report](devicetree-validation-20261031.json).

The [ROM parity check](devicetree-parity-20261031.json) also compares each final
tree with the captured ROM build: all 4,560/4,559 nodes and their properties,
including labels, match. It can be repeated with `verify_devicetrees.py
ARTIFACT BASELINE_DTB_OBJ_OUT`. Device-tree compiler warnings remain visible:
1,084 warnings originate in the unchanged vendor sources. This establishes
assembly parity, not hardware compatibility of the new drivers.

The SCSI-generic driver now builds unmodified from pinned 6.18 common as
`sg.ko`; it is disabled in the stock GKI config and needed as an external module
for the baseline load list. The full build passes 5,121 imported-symbol CRC
checks across 111 modules, without kernel compiler or modpost warnings.

The remaining names in the shared first-stage list are other-platform
pinctrl/clock/NoC/UFS PHY variants, Sun's FAST CPUCP client, and SD-card support.
Both assembled Onyx trees keep `/soc/sdhci@8804000` disabled, so `sdhci-msm` and
its `cqhci` helper are not required. The builder asserts that disabled state.
The port still needs the applicable second-stage and peripheral modules,
matching boot/vendor_boot/DLKM images, custom features and installer validation;
the current DT images alone are not a flashable package.


## USB PHY and repeater

The USB foundation adds the vendor repeater framework, PMIC eUSB2 repeater,
Synopsys eUSB2 PHY and pinned common generic PHY. Managed repeater references
retain device/module ownership snapshots and a supplier link; registration and
lookup serialize. Power helpers retain individual load, voltage and enable
votes, unwind in reverse order, and preserve failed cleanup for retry.

The drivers propagate clock, reset, register I/O and initialization failures.
DT register sequences validate counts, values, alignment and MMIO bounds. APB
polling tests the returned status bit and reports timeouts. Debugfs input is
bounded and terminated. Removal unregisters the PHY before draining charging
work and releasing its power-supply reference; current updates reject teardown
and overflow.

`verify_usb_phy.py WORKSPACE` exercises actual staged helpers with simulated
services under AddressSanitizer and UndefinedBehaviorSanitizer. The
[recorded result](usb-phy-validation-20261031.json) covers 579 power failure
pairs, 787,969 DT sequence cases, 312 repeater I/O/status cases, clock failures,
APB polling, charging-work teardown and managed-reference release. It does not
execute kernel device links, workqueues, USB enumeration or hardware.

The full 115-module build passes 5,264 imported-symbol CRC checks without
kernel compiler or modpost warnings; the adaptation passes checkpatch. Both
device-tree overlay checks also pass, with the unchanged vendor DTC warnings
recorded separately. The USB controller, its charging services and other
peripheral drivers still need porting before image assembly.
