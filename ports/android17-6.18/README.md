# Onyx Android 17 / Linux 6.18 port

This directory contains the first implementation of the POCO F7 / SM8735 port.
The pinned **6.18.33 GKI and 238 vendor modules build successfully**. This is
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
- 238 platform `.ko` files, their symbol tables and DDK configurations.
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
used. The current [validation report](validation-20261031.json) checks 13,024
imported symbol CRCs across all 238 modules.

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

1. Finish applicable second-stage buses, remote processor/GLINK services, USB
   controller/ADB and charging. Applicable first-stage modules and device-tree
   assembly now build; storage and other hardware still need device validation.
2. Port GPU/display, audio, camera, touch, fingerprint, WLAN and remaining device
   modules. Semantic fixes to the vendor interfaces remain necessary.
3. Produce matching boot/vendor_boot, DTBO and DLKM images with complete load
   lists. Preserve the required WALT kernel command-line allocation parameter.
4. Integrate KernelSU/NetHunter and validate the installer. Perform boot,
   hardware and suspend/resume validation with the resulting test package before
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


## GENI I2C/SPI and GPI DMA

The bus group adds `msm_gpi`, `i2c-msm-geni` and `spi-msm-geni`. It uses pinned
common GENI interfaces and DMA private headers. Wrapper diagnostics use the
actual common wrapper layout instead of treating it as a serial-engine object.
SPI uses the current controller/target API and explicit chip-select accessors;
trace assignments follow the current tracepoint API.

GPI validates DT channel masks, static channel indexes and event-ring expansion.
Ring sizes use wide multiplication, power-of-two rounding and the hardware's
16-bit length bound. SG entries are checked before copying into rings. Failure
unwind addresses each initialized channel, diagnostics exist before publication,
and removal preserves other controllers' debugfs directories. Reset IRQs and
event/descriptor tasklets drain before their backing storage is freed. A DMA
synchronization callback drains IRQ, event and relevant descriptor callbacks
following successful termination.

I2C removes clients before turning off the bus. SPI's managed controller storage
survives unregister and cleanup. Probe errors unwind runtime PM and bandwidth
votes; channel acquisition errors propagate. Clock references belong to the
consumer rather than accumulating on its parent. DMA mapping failures unwind
earlier mappings, including successful DMA address zero, and unmapping is
idempotent. Resume errors stop before accessing unclocked registers. SPI tracks
IRQ enable state explicitly, and timeout calculations use wide arithmetic.

`verify_geni.py WORKSPACE` executes staged helpers under AddressSanitizer and
UndefinedBehaviorSanitizer: 4,263,779 ring cases, 111,537 mapping-failure cases,
four clock sequences, 131,072 IRQ sequences and eight DMA synchronization
sequences. It also compares the private wrapper declaration with pinned common.
The [recorded results](geni-validation-20261031.json) model kernel services;
they do not exercise bus transfers, real interrupts, runtime PM or firmware.
The full 118-module build passes 5,568 imported-symbol CRC checks with no kernel
compiler or modpost warnings. Device-tree checks pass with the existing 1,084
vendor DTC warnings recorded separately. The adaptation passes checkpatch.


The remote-service group adds QMI, PDR, SMP2P/sleepstate, GLINK/SMEM, SMD and
remoteproc SSR support. RPMSG/remoteproc private layouts come from pinned common.
Socket callback teardown, indication-list locking, shared-memory bounds, DMA
ranges and SSR hook/timer unwinding are checked. ROM notification policies are
preserved: 20,000 ms SSR and 9,000 ms PDR timeouts, panic-on-timeout and dual dumps.
`verify_remote.py` passes 764,136 extracted-helper cases with ASan/UBSan, twelve
source-order checks and configuration checks.

Twelve charging modules provide MCA helpers, PMIC GLINK, ADSP/QBG communication
and UCSI. Callbacks detach and drain before client release, malformed/short
messages fail, and firmware replies must match their requests. UCSI uses the
current core operations and blocking accessory notifiers; timed-out requests
require reconnect because the firmware protocol has no transaction cookie.
UCSI 1.x is supported by its 48-byte firmware window; a reported 2.x version
returns an explicit unsupported error. MCA sysfs/debugfs unwind partial creation
and expose teardown APIs. `verify_charging.py` exercises message sizes, response
matching, event/SMEM boundaries and rollback with ASan/UBSan. Battery power-supply
registration, charge policy and hardware drivers remain separate work.

The high-speed GENI UART uses current serial-core FIFOs and immutable DMA TX
snapshots with flush generations. Channel requests run during startup, callbacks
are drained before successful cancellation releases mappings, and clock/PM/probe
failures unwind their resources. The common RX cancel bit 14 matches upstream
fix c657243ae12000dc57e3648b0ddd30da9ffd1f14. `verify_uart.py` passes 131,076 TX,
327,680 packing, 576 power, 225 RX and 16 cancellation cases. Failed GPI hardware
STOP/RESET during final channel release still does not prove DMA quiescence;
this hardware-failure recovery limitation remains explicit.

The WCD939x USB switch uses a protected per-device context, initializes producers
before publication and drains callbacks/work before freeing resources. Paged I2C
rejects partial transfers, checks page/length boundaries and writes all bytes.
Register errors propagate while successful vendor register sequences are retained.
`verify_wcd.py` passes 20,163 transport, 72 short-transfer, 32,768 differential
register-trace, 637 failure, 13 PM and 256 cleanup cases with ASan/UBSan.

The 140-module checkpoint passed 6,542 imported-symbol CRCs. A later log audit
found a missing SPI module-description warning, fixed in the 157-module checkpoint. Device-tree checks pass with the same 1,084
recorded vendor DTC warnings. The checkpoint covers 139 of 466 distinct vendor
load-list names (29.8%); the additional module is a heap helper. This count includes
shared-list alternative hardware and is not an overall completion percentage.
Host tests simulate kernel services and do not validate live IRQ/DMA/SRCU,
firmware, charging, USB operation or device boot. Image assembly and the remaining
driver stack are still required before a flashable ZIP exists.


Five remote-processor modules add PIL information, ramdump, sysmon, Q6 and PAS.
Tuna ADSP/CDSP/modem/WPSS descriptors remain unchanged. IRQ/work cleanup, firmware
address arithmetic, regulator rollback, PIL records and ELF dump bounds are
checked. PAS owns and drains its recovery-policy hook. Sysmon preserves the ROM's
20,000/10,000 ms notification/shutdown policy. `verify_rproc.py` passes 1,877,291
ASan/UBSan helper cases, fourteen lifetime contracts and descriptor/config checks.

Nine charging platform/IC modules add the FG, buck, charge-pump, load-switch,
wireless and strategy registries, I2C base, BQ27Z561 fuel gauge and SC8581 charge
pump. Onyx selects BQ27Z561 at 0x55 and SC8581 at 0x6f. SRCU-protected callback
registries and the MCA logging follow-up drain callbacks before unregister
returns. Short I2C transfers, gauge packet length/checksum errors, class-creation
rollback and protection-register failures propagate. `verify_chargehw.py` runs
actual extracted helpers with ASan/UBSan and a modeled sleeping-reader/unregister
concurrency test. Actual battery/USB power-supply integration and charge policies
still require further modules and hardware validation.

USB auxiliary support adds EUD, the redriver framework and QMP/DP PHY. EUD uses
current serial-core FIFOs and bounded hardware packets; interrupt masks, pending
events, retry accounting and teardown are checked. QMP keeps the exact vendor
register operations with resource failure rollback and DT bounds checks. Redriver
binding is exclusive and callbacks drain before removal. The final Onyx DT enables
EUD, sets DWC to high speed, selects eUSB2 plus NOP PHY, and disables QMP; QMP is
optional support retained from the shared ROM module configuration. No Onyx
external-redriver phandle was found. `verify_usbaux.py` verifies the 330-cell QMP
sequence, extracted I/O/resource helpers and callback-removal behavior under
ASan/UBSan with simulated kernel services.

The 157-module combined build passes 7,200 imported-symbol CRC checks with no
compiler or modpost warnings, including the corrected GENI SPI description.
Device-tree checks retain the 1,084 existing vendor DTC warnings. Coverage is
156/466 distinct vendor load-list names (33.5%); 310 remain to assess or port.
This raw count includes alternative hardware and does not measure overall ZIP
readiness. All runtime and device-testing limitations above still apply.


Seven charging protocol modules add the protocol/BC1.2/PD/QC registries, ADSP PD,
sub-PMIC proxy and charger partition. SRCU dispatch protects provider removal;
firmware errors, short notifications, VDM/PDO bounds and voltage/power arithmetic
are checked. The persistent-state driver uses exported block-device/bio APIs,
preserves the 4 KiB record and firmware ownership handshake, and reports storage
errors. Raw transactions require same-task cleanup. Three unused QTI compatibility
exports are omitted: the actual ROM haptic/flash modules do not import them and
CONFIG_QTI_BATTERY_CHARGER is unset. MCA firmware operations remain implemented.
`verify_chargepolicy.py` passes 65,536 partition request combinations plus
allocation/I/O faults, notification lengths, wire-format and callback-lifetime
checks with ASan/UBSan. Battery/USB business components and policies remain.

`msm_sysstats` and `msm_performance` add the real GPU statistics/event providers.
Their public/UAPI headers are unchanged. Parsing/QoS/PMU errors, ring bounds,
8,000 concurrent producer events, callback replacement/unregister, FD locking
and netlink attributes are checked by `verify_perf.py` with ASan/UBSan. The ROM
MSM_SYSSTATS_STUB_NONEXPORTED_SYMBOLS configuration is retained: existing
swap/vmalloc fields remain zero. QTI_PLH is absent from the ROM configuration.

The 166-module combined build passes 7,509 imported-symbol CRC checks with no
compiler or modpost warnings. Device-tree checks pass with 1,084 existing vendor
DTC warnings. Coverage is 165/466 distinct vendor load-list names (35.4%); 301
remain to assess or port, including alternative hardware. This is module
coverage, not overall completion or ZIP readiness. The extracted host tests
use simulated kernel services and do not establish firmware, charging, live
SRCU/IRQ/workqueue behavior or device boot. No flashable 6.18 ZIP exists yet.


Panel events, PM8941 power keys and Xiaomi fingerprint add three input modules.
Panel callbacks use immutable SRCU entries with validated inputs and drained
unregister. The power-key display callback setter drains prior callbacks; its
exported raw notifier head remains the original API. Debounce arithmetic uses
64-bit cycles, IRQs start after input/wakeup registration, and wake errors retain
accurate state. Checkpatch has one advisory for the explicit prototype of the
existing exported display setter, whose consumer declares it directly.

Fingerprint preserves the Onyx FOD/primary-display path and 30-byte device-info
ABI. Netlink allocates header plus payload, zeroes all payload bytes, validates
headers, uses the socket sender ID and rejects delayed callbacks from old
sockets. Device operations serialize with removal. Separately allocated cdevs
and identity checks reject stale opens; outstanding files prevent singleton
reuse, and registered input devices unregister correctly. GPIO/regulator/IRQ
errors propagate, reset waits are bounded, and staged cleanup drains callbacks,
work and IRQs before releasing resources. The absent side/ultrasonic paths are
not part of this configured port. `verify_input.py` passes four million debounce
cases, power/reset/wake errors, 4,096 netlink header cases, 256 payload commands,
14 probe failure points, callback draining and file/removal/rebind scenarios
with ASan/UBSan and simulated kernel services.

Battery business, FG strategy interface and MCA panel bridge add three modules.
Battery properties propagate provider errors, support USB without a wireless
supply, bound SOC/unit conversions and own model-name copies. The FG callback
registry drains before removal. Named vote queries use SRCU; legacy callers
retaining raw voter pointers still require owner coordination. Battery events,
log callbacks, the thermal zone and power supply unwind in order. Panel bridge
notifications permit listeners to query state again and balance OF references.
`verify_chargebusiness.py` passes property/registration failures, 101 SOC values,
wireless-absent paths, callback draining and panel reentry with ASan/UBSan. The
actual FG strategy and charger policies remain required to provide readings
and charging behavior; these interfaces do not establish functioning charging.

The 172-module combined build passes 7,740 imported-symbol CRC checks with no
compiler or modpost warnings. Device-tree verification passes with the same
1,084 existing vendor DTC warnings. Coverage is 171/466 distinct vendor module
names (36.7%); 295 remain to assess or port, including alternative hardware.
Host tests do not establish live kernel/VFS/IRQ/SRCU, hardware or boot behavior.
Matching images, remaining drivers and installer verification are still needed
before a flashable ZIP exists.


DWC3-MSM adds the Onyx USB controller with real redriver, clock, repeater, WCD
and MCA providers. Register/hook APIs match pinned common; all sixteen hook
symbols and signatures are verified against source and the built System.map.
Probe/remove and host-role failures unwind resources; GSI DMA allocation is
transactional and bounded. A core-remove wrapper frees GSI buffers after core
quiescence and before platform-bus IOMMU detachment. Role-switch handles detach
from their owner; extcon callbacks clear/drain owner access before removal.

Published PM/remove/audio callback storage and module text remain resident
because their call sites have no replacement grace period. Extcon raw-notifier
storage remains as detached tombstones for the same reason. These are deliberate
lifetime limits; the module cannot unload after permanent publication. Onyx
continues to use high-speed eUSB2/NOP PHY with QMP/DP disabled. Gadget transports
and their userspace configuration remain separate work. Checkpatch has three
CONST_STRUCT advisories for runtime-initialized PM tables/pointers.

`verify_dwc.py` passes extracted ASan/UBSan allocation/IRQ/clock/power cases,
callback withdrawal, PM/hook scoping, core-removal ordering and bounded debug
indices. It does not test actual kprobe registration, DMA/electrical behavior,
live kernel timing or boot. The 173-module combined build passes 7,949 imported
symbol CRCs with no compiler/modpost warnings; device-tree verification passes
with the existing 1,084 DTC warnings. Coverage is 172/466 vendor names (36.9%),
with 294 remaining to assess or port. Image assembly and the remaining driver
stack are still required for a flashable ZIP.


Smart charging, SOC limiter and charging MIEV telemetry add three modules.
SOC failures retain the active limit for retry; wired/wireless state is independent,
DT current tables use their actual row counts, and control/voltage bounds and
provider failures are checked. Immutable provider callbacks drain before removal.
Voters publish after initialization, reject duplicate ownership, and retry failed
election callbacks. Telemetry uses real MIEV exports and only consumes an allowance
after successful report delivery. `verify_smartcharge.py` passes ASan/UBSan control,
SOC, telemetry, callback and eight probe-failure scenarios; the battery interface
checks were rerun against the changed voter support.

The later BASP overlay replaces the inferred layout with a bounded decoder based
on static evidence from the exact stock producer binary. JEITA and wired quick
charge now provide transactional consumers. The [BASP ABI record](basp-abi.md)
describes observed fields, implementation choices and unresolved runtime evidence.
The actual producer payload has not been captured or exercised on the phone.
The earlier [compatibility snapshot](smartcharge-compatibility-20261031.json)
records the pre-overlay state.

The 176-module combined build passes 8,034 imported-symbol CRCs with no compiler/
modpost warnings; device-tree checks pass with 1,084 existing vendor DTC warnings.
[Module build coverage](module-coverage-20261031.json) is 175/466 vendor names
(37.6%), leaving 291 names to assess or port. The report preserves the exact
remaining names and hashes of its ROM load-list inputs. This counts built names,
including alternative hardware and modules with limitations above; it does not
measure working hardware or overall ZIP completion. No flashable ZIP exists.


The Onyx GPU and synchronization stack adds seven modules: KGSL, SYNX,
IPCLite, hardware fence, software sync fence, and both Adreno governors.
Linux 6.18 API changes preserve the ROM hardware tables, firmware structures
and userspace ABI. Initialization and IOMMU failures unwind their resources;
firmware/HFI buffers and shared-memory rings reject malformed sizes and offsets.
The hardware-fence listener now exits when stopped while idle. The integrated
[GPU helper checks](gpu-validation-20261031.json) pass 3,082,819 ASan/UBSan
cases and 35 source contracts; the negative control reproduces the original
listener shutdown hang. Active restricted hooks and firmware mappings retain
module references because their interfaces have no live revocation protocol.
IPCLite and hardware-fence sysfs unbind are disabled while those mappings exist.

The combined 183-module build passes 8,924 imported-symbol CRC checks without
compiler or modpost warnings. Device-tree compilation and overlay checks pass
with the same 1,084 existing vendor DTC warnings. ROM module build coverage is
182/466 (39.1%), leaving 284 names to assess or port. This includes alternative
hardware and does not measure working hardware or ZIP completion. The combined
artifact is `artifacts/20261031T111820072627Z`; no flashable 6.18 ZIP exists.


NXP NFC adds `nxp-nci.ko` using the ROM non-secure peripheral configuration,
SN controller GPIOs, and unchanged NFC/eSE userspace headers. Probe initializes
locks, IRQ ownership and hardware before publishing the character device.
Removal wakes blocked reads and cold-reset transactions, withdraws publication,
and drains active SRCU operations; existing file descriptors retain memory until
close and stale cdev identities cannot open a rebound instance. GPIO and regulator
failures unwind owned resources. Power-off wakes readers and IRQ wake failures
propagate. Zero-length I/O returns without waiting behind another operation.

NCI metadata reads validate packet lengths before indexing. Cold reset publishes
its pending state before transport submission, validates request envelopes and
firmware status, bounds waits, and protects the command lifetime while a response
races cleanup. [NFC host checks](nfc-validation-20261031.json) pass ASan/UBSan
packet, reset and fault cases, 23 probe failure points, 20 regulator/power/wake
cases, retained descriptor/rebind tests, and 14 source contracts. These simulated
checks do not establish live kernel concurrency, firmware or phone operation.

The combined 184-module archive `artifacts/20261031T113444670332Z` passes
9,004 imported-symbol CRC checks without compiler/modpost warnings. Device-tree
checks pass with 1,084 existing vendor DTC warnings. ROM module build coverage
is 183/466 (39.3%), leaving 283 names to assess or port. No flashable 6.18 ZIP
exists; display, audio, camera, wireless and matching images remain incomplete.


USB CCID, serial CDEV, and FunctionFS IPC logging add three modules. CCID and
CDEV retain character devices for open files, drain user-copy operations before
freeing controller requests, and unwind partial allocation/endpoint setup. CDEV
connection generations keep old requests out of a newly connected endpoint pool.
CDC notifications serialize pending ownership and preserve modem bit operations.
FunctionFS logging matches five private structures from the pinned 6.18 kernel;
return probes retain stable log contexts through unregister and work draining.
The 26 probe symbols exist in the built kernel; runtime probe admissibility
requires device validation. Vendor USB descriptors and imported headers remain
unchanged. [Integrated gadget host checks](gadget-validation-20261031.json) pass
ASan/UBSan copy/disconnect, notification, ioctl, file-reference, allocation-fault,
probe-registration and context-lifetime cases. QDSS/GSI and IPA remain outside
this group. No live USB/configfs or phone checks have been performed.

The combined 187-module archive `artifacts/20261031T114139294447Z` passes
9,161 imported-symbol CRC checks without compiler/modpost warnings. Device-tree
checks pass with 1,084 existing vendor DTC warnings. ROM module build coverage
is 186/466 (39.9%), leaving 280 names to assess or port. This is build coverage
including alternative hardware, not working-device or ZIP completion. No
flashable 6.18 ZIP exists.


## Type-C alternate mode and switch

The port builds `altmode-glink.ko` and `fsa4480-i2c.ko` from exact Onyx
source pins. GLINK requests serialize their response state before transmission;
notifications use an owned FIFO. Replies validate frame bounds, port, owner,
opcode and session. A timed-out request requires a new GLINK UP session before
another request because the vendor wire format has no transaction IDs. Client
withdrawal drains callbacks before freeing state. FSA register operations
serialize and propagate errors; UCSI work balances wake references and retries
unchanged modes after failed register operations. Provider teardown withdraws
public access and drains callbacks/work before managed resources disappear.
The DIO4485 register range includes its retained initialization table at 0x51.

[Integrated host validation](altmode-validation-20261031.json) passes ASan/UBSan
transport, allocation, concurrent request, immediate reply, timeout/restart,
FIFO, switch-register fault, notifier reentry and wake-balance cases. Four
pinned inputs replay exactly, with both public headers and switch tables
unchanged. These tests mock kernel services; firmware, Type-C/DP electrical
behavior and phone operation remain untested.

The combined 189-module archive `artifacts/20261031T120312428524Z` passes 9,242 imported-symbol CRC checks with no compiler/modpost warnings. Device-tree checks pass with 1,084 existing vendor DTC warnings. ROM module build coverage is 188/466 (40.3%), leaving 278 names to assess or port. No flashable 6.18 ZIP exists.


## Virtual-address minidump

`qcom_va_minidump.ko` retains its coherent DMA pool until removal, publishes
readiness after resources and panic hooks initialize, and balances subsystem
kobject references. Panic collection rejects wrapped or overlapping ranges,
checks ELF sizing before writes, accepts a fitting prefix, and uses nofault
copies for direct-memory regions. Collection is restricted to its panic CPU
and phase; a published dump is not rewritten. Unregister drains both panic
phases. Failed firmware-region withdrawal retains the advertised buffer and
rejects rebinding.

[Integrated host validation](vamd-validation-20261031.json) passes 16,384 range
cases, 24,576 decoded ELF geometries, seven panic-phase cases, eight probe
failure stages and five registration failure stages under ASan/UBSan. Exact
Git-blob replay preserves the existing public and private headers. These checks
mock DMA, kobjects, notifiers, RCU and firmware; live panic capture and firmware
dump extraction remain untested. KGSL now explicitly depends on this provider
when the minidump configuration enables its VA callbacks.

The combined 190-module archive `artifacts/20261031T122433629874Z` passes
9,304 imported-symbol CRC checks without compiler/modpost warnings. Device-tree
checks pass with 1,084 existing vendor DTC warnings. ROM module build coverage
is 189/466 (40.6%), leaving 277 names to assess or port. No flashable 6.18 ZIP
exists.


## Secure services and display

Seven native providers add QSEE proxy, SI-core, memory objects, smcinvoke,
QSEECOM compatibility, HDCP and SMMU proxy. The ROM's SI-core implementation,
QSEECOM_COMPAT choice and nine unchanged vendor UAPI headers are retained.
Message, callback, firmware ELF and split-image sizes are checked before access.
Invocation and callback replies reserve file descriptors before constructing
files, install them after both reply copies succeed, and unwind pending exports
on allocation or copy failures. The heap overlay publishes a coherent immutable
map/unmap callback pair. Failed setup unwinds resources; failed firmware mapping
withdrawal retains the advertised mapping and its references.

Four display support modules provide native 6.18 DRM helpers, DP AUX, MMRM and
external display. The main `msm_drm.ko` retains the Onyx display tables, panel
commands and seven unchanged UAPI inputs. Its dependency-name set matches the
stock ROM. Upstream DRM_MSM stays disabled and DRM_MSM_HELPER supplies the native
MDSS provider. GEM uses a private mutex, shared reservation state and correct
read/write fence usage; page and DMA-map failures unwind. Last close retains
the DRM device and serializes concurrent opens. Xiaomi asynchronous requests
own their TX buffers, client publication follows initialization, and worker
startup and debugger teardown drain prior work. Procfs writes and log-ring
formatting/read bounds are checked.

Successful secure singleton services, display, MMRM and external-display
bindings retain module references because firmware or consumers hold handles
without a revocation protocol. Their manual unbind interfaces are suppressed.
Probe failures still unwind. Dynamic removal, live VFS/RCU/SRCU, secure firmware,
memory isolation, DRM atomic commits, display hardware and suspend remain
unverified.

[Secure host checks](secure-validation-20261031.json) pass eight ASan/UBSan groups,
including 901 invocation/callback export fault cases and concurrent heap-callback
publication. [Display checks](display-validation-20261031.json) and
[support checks](display-support-validation-20261031.json) pass seven groups.
The retained negative-control runners reproduce six failures from the original
source bodies. Exact pinned-source replay, unchanged UAPI and ROM configuration
evidence are recorded alongside the verifiers.

## Fuel-gauge strategy and BASP wired consumers

The FG strategy uses per-device smoothing/history, an ordered workqueue and
serialized sampling. Failed gauge reads preserve output state, SOC arithmetic
uses bounded 64-bit calculations, malformed tables are rejected, and probe
failure/removal withdraw callbacks and drain all eight works. Named voter
operations avoid retaining raw owner pointers. The actual single-gauge Onyx
topology is retained. [FG host checks](fgstrategy-validation-20261031.json)
cover sampling, smoothing, event and lifecycle behavior.

The BASP byte decoder uses statically evidenced stock header/record offsets and
command 0xd0001001. Each open owns a zeroed mmap page and immutable transaction
snapshot. Ioctl commits synchronously; the final VMA release retains mmap commit
behavior. Prepare/commit/abort callbacks support atomic replacement of JEITA and
wired quick-charge data, and detach drains persistent file/VMA contexts.
JEITA validates the selected stock temperature/voltage tables and owns its
hysteresis history. Wired quick charge prepares curves and stage data before
commit, defers election stops to work, and unwinds owned voters and tables.
The wired prepare uses trylock when JEITA holds its transaction lock, returning
EBUSY and aborting prepared groups to avoid a reverse lock-order cycle.
Charging-interface withdrawal drains active callbacks before owner teardown.

[BASP checks](basp-validation-20261031.json) and
[wired-consumer checks](chargeconsumers-validation-20261031.json) pass decoder,
allocation, mmap lifetime, malformed-table, transactional rollback, 200 concurrent
lock-order iterations and callback-withdrawal cases under ASan/UBSan. Earlier
charging, hardware, protocol, battery-business and smart-charge host checks also
pass against the integrated support. The actual producer payload and live kernel
workqueue/SRCU, gauge calibration, firmware and phone charging remain untested.
Onyx wireless DT stays disabled; a nonempty wireless update requires a live
consumer. Buck, charger business, thermal, authentication and protection policy
components remain to port.

The combined 205-module archive `artifacts/20261031T124045154059Z` passes
10,979 imported-symbol CRC checks without compiler/modpost warnings. Device-tree
and overlay checks pass with 1,084 existing vendor DTC warnings. ROM module build
coverage is 203/466 (43.6%), leaving 263 names to assess or port. The denominator
includes alternative hardware; this measures build coverage. No flashable 6.18
ZIP or phone validation exists yet.


## Audio foundation and service notifications

Four native audio modules add sound-event coordination, PDR registration,
audio event notifications and audio service restart notifications. Thirteen
exact source/header inputs preserve the ROM object lists, dependency names,
notifier exports, aliases and the unchanged forced `sunautoconf.h` configuration.
The unused blanket include of a private 6.6 regmap header is omitted.
Sound-event master/client setup holds device references, clears stale slots,
detaches surviving clients and rolls back partial enable failures. PDR handles
reject duplicate ownership and release drains active callbacks. Service
notifications validate domains and event/source combinations, serialize source
registration and recover failed setup. Removal withdraws access before draining
PDR/SSR callbacks and freeing owned registry state.

[Foundation validation](audio-foundation-validation-20261031.json) and
[notifier validation](audio-notifier-validation-20261031.json) pass eighteen
ASan/UBSan groups, including allocation/enable rollback, repeated partial-master
removal, registration races and four-thread notifier/registry operations.
Sixteen original-source negative controls fail their targeted scenarios.
Callbacks retain the framework's nonreentrant contract; PDR withdrawal occurs
outside its own callback. The tests use simulated devices and firmware services;
live kernel callback scheduling, audio firmware, IRQs, suspend and phone audio
remain untested. DSP runtime, codecs, amplifiers and the machine driver are
separate remaining work.

## Xiaomi and NT36532 touch

The two ROM-selected touch modules retain THP, TDDI, FOD and Novatek debug
features, the native panel-ID selection, GPIO/SPI configuration and three-second
boot-update delay. Knock, stylus, sensorhub and the legacy DRM_MSM notifier
branch stay disabled. NT36532 uses the real display lockdown and gesture exports.
The unchanged touch UAPI and all eighteen source/header pins replay exactly.
Proc SPI operations, all 23 diagnostic readers, CSV dimensions, ring entries,
report slots, mmap selectors/lengths/permissions and firmware partition/CRC
arithmetic are bounded. Failed setup unwinds resources. Controller removal
drains framework, notifier and proc callbacks and workers before releasing IRQ,
GPIO, input and buffers, and balances IRQ wake.

Published framework pages and raw device/class pointers remain resident until
reboot; successful module bindings retain references and suppress manual unbind.
Existing mappings therefore keep valid storage after access is withdrawn.
Hot removal, unload and rebind remain outside this fixed-device lifecycle.
[Integrated touch validation](touch-validation-20261031.json) passes nine
ASan/UBSan groups. All four actual ROM normal/MP firmware files produce the same
partition maps and CRCs under software and hardware CRC paths; their hashes
are recorded while the firmware stays outside Git. The tests mock Linux
services and do not establish live VFS/IRQ/SPI behavior, controller execution,
timing or phone touch operation.

The combined 211-module archive `artifacts/20261031T130449999212Z` passes
11,287 imported-symbol CRC checks without compiler/modpost warnings. Device-tree
and overlay checks pass with 1,084 existing vendor DTC warnings. ROM module build
coverage is 209/466 (44.8%), leaving 257 names to assess or port. This includes
alternative hardware and measures built names. No flashable 6.18 ZIP or phone
validation exists yet.


## GPR transport and buck charging policy

The native GPR module uses per-transport notifier ownership, referenced service
lookups and complete 32-bit service IDs. Packet version, optional header and
payload bounds are validated while the wire structures and export names remain
unchanged. Removal withdraws transport access, drains callbacks and unregisters
owned services; failed setup unwinds only acquired resources. The
[integrated validator](audio-gpr-validation-20261031.json) passes seven
ASan/UBSan groups with 149,548 cases. Seven original-source negative controls
fail their targeted cases. DSP firmware and live kernel callbacks remain untested.

The native buck policy creates its eleven voters and nine delayed works before
publishing access. Failure and stop paths withdraw callbacks, drain queued work,
release owned voters and quiesce charging. A bounded event FIFO keeps buck work
outside the wired policy lock; controls preserve hardware errors and validate
currents, booleans, battery readings and device-tree dimensions. The
[integrated tests](buckstrategy-validation-20261031.json) cover 24 probe faults,
3,554 current-bound cases, FIFO overflow/wrap, 200 lock cycles and 4,000 concurrent
producer attempts, using the actual 35-property Onyx profile. Wireless-reverse
coordination remains disabled until its real provider is ported. These tests use
mocked hardware, providers and workqueues and do not establish charging safety
or operation on the phone.

The combined 213-module archive `artifacts/20261031T131938959744Z` passes
11,459 imported-symbol CRC checks without compiler/modpost warnings. Device-tree
and overlay checks pass with 1,084 existing vendor DTC warnings. Built ROM module
coverage is 211/466 (45.3%), leaving 255 names to assess or port, including
alternative hardware. There is no flashable 6.18 ZIP or phone validation yet.

## WLAN source selection

The source pin is `6cc986b6e08783b8a14b155c1531c86959a1398a` from
`Loukious/vendor_qcom_opensource_wlan`, with manifest upstream
`onyx-v-oss-monitor-direct-konoha-abi`. It matches the ROM checkout and preserves
the wide-channel/CFR selections and build compatibility changes. The
[branch comparison](wlan-source-comparison-20261031.json) checks the user-selected
`onyx-v-oss-monitor-direct` head `fa5f0aa8bb5a1353bcb767cd0c5e4716a59b69f9`:
all seventeen imported WLAN support C/header files and ten core monitor/direct
injection source files are identical. Other differences include configuration,
regulatory source, build files and userspace Sigma tools. The main Wi-Fi driver
and its monitor/injection operation on 6.18 remain separate pending work.


## WLAN base and charger protection

Native WLAN generic-netlink and preallocation providers preserve the ROM family,
groups, exports and pool sizing tables. Command IDs and duplicate registrations
are validated; immutable callback/context pairs are withdrawn through SRCU before
release. Pool creation unwinds partial failures. Failed tracker growth preserves
old allocations, borrowed buffers retain module/pool storage, and a different
device cannot replace storage with outstanding borrowers. Deinit stops new
allocations while allowing existing buffers to return. A later deinit releases
retained pools once all borrowers return. Buffers are explicitly zeroed, including
reserve allocations. [Integrated tests](wlan-base-validation-20261031.json) cover
15 setup failures, failed tracker growth, 4,000 concurrent borrow attempts,
100 lifecycle cycles, all 256 command IDs and 200 callback-withdrawal drains.
They use mock allocators/netlink and pthread SRCU and do not establish live
reserve allocator, IRQ, module teardown, monitor/injection or WLAN operation.
Status callbacks unregister outside their own callback.

The connector-temperature and liquid-detection modules retain their Onyx
properties and aliases. Getter withdrawal drains active users and reports
-ENODEV for absent providers. Connector protection owns its MOS GPIO and queue,
keeps rate history per device, validates temperature readings and packed boost
dimensions, and inhibits software charging on sensor errors. Removal/shutdown
withdraw callbacks and drain policy work. Failed recovery leaves protection
latched. The [connector tests](antiburn-validation-20261031.json) and
[liquid-detection tests](lpd-validation-20261031.json) cover probe failures,
callback/worker drains, sensor/property errors, single-sensor operation,
hysteresis, failed GPIO recovery, long uptime and the actual 18-property Onyx
profile. Native cached thermal-zone lifetimes and the adapter VBUS helper remain
unverified; mocked tests do not establish electrical protection or phone safety.

The combined 217-module archive `artifacts/20261031T132957264826Z` passes
11,592 imported-symbol CRC checks without compiler/modpost warnings. Device-tree
and overlay checks pass with 1,084 existing vendor DTC warnings. Built ROM module
coverage is 215/466 (46.1%), leaving 251 names to assess or port, including
alternative hardware. The main WLAN and remaining transport review, PCIe/MHI,
audio, camera/video, other device interfaces and image/installer packaging are
still in progress. There is no flashable 6.18 ZIP or phone validation yet.


## PCIe/MHI providers and SPF audio commands

The ROM-selected PCIe, PDC and MHI modules retain the native Tuna CESTA
instructions, power maps, firmware structures and ITS topology. The port checks
configuration access, register and ring bounds, image capacities and 64-bit RDDM
sizes; initialization failures unwind acquired resources. Removal drains IRQs,
work and child devices before releasing host/controller storage. Clients must
still quiesce controller operations before unregistering. Public PCIe/MHI layouts
remain unchanged. [Integrated PCIe/MHI tests](pcie-mhi-validation-20261031.json)
pass ten ASan/UBSan groups covering bounds, allocation failures and teardown.
[Pinned DT checks](pcie-mhi-dt-validation-20261031.json) verify register spans,
CESTA tuples and the native PDC mapping. These checks use mocked kernel services
and do not execute device firmware, physical PCIe links or WLAN.

SPF requests use native GPR headers, nonzero tokens retained across service
rebinds, and validated response sizes, opcodes and token identities. Provider
withdrawal wakes command waiters and drains work, child devices and service
references before cleanup. The clock manager records its own requested and
enabled votes; reset preserves unrelated votes and permits retry after partial
restoration. Clients must retain their clock handles through the last requested
vote. [SPF tests](audio-spf-validation-20261031.json) and
[clock ownership tests](audio-votes-validation-20261031.json) pass full-source
ASan/UBSan response, failure, rebind and concurrent-operation cases. Five original
SPF and four original clock groups reject their targeted negative controls;
restoring per-service token reset also rejects the stale-reply regression.
Live DSP firmware, scheduling, suspend/resume and hardware remain unverified.

The combined 221-module archive `artifacts/20261031T134316988014Z` passes
11,983 imported-symbol CRC checks without compiler/modpost warnings. Device-tree
and overlay checks pass with 1,084 existing vendor DTC warnings.
Built ROM module coverage is 219/466 (47.0%), leaving 247 names to assess or
port, including alternative hardware. Main WLAN/Bluetooth, remaining audio,
camera/video and device interfaces, boot/DLKM images and the installer still need
completion. No flashable 6.18 ZIP or phone validation exists yet.


## ADSP loading and remaining charging interfaces

The native ADSP loader owns its boot votes, optional firmware handle and sysfs
work. Stop drains commands and releases only acquired resources. Native core SSR
recovery retains its power votes and uses the existing remoteproc recovery path.
[Full-source tests](audio-adsp-validation-20261031.json) pass thirteen ASan/UBSan
groups; ten original-source controls reject their targeted faults, including a
partial firmware-name array. Live DSP recovery and firmware remain untested.

Charger business uses an owned bounded FIFO, six notifier registrations and
per-device supply descriptors. Stop withdraws callbacks, drains work and tears
down supplies. The pinned Onyx profile disables wireless support; reverse-wireless
coordination remains behind a disabled configuration option. Four extension
controls return EOPNOTSUPP until their real HANDLE_ALLOW_CHARGE,
START_QUICK_REVCHG and REVCHG_BCL handlers exist. Notification-only forwarding
retains nontransactional sink semantics. [Business tests](businesscharger-validation-20261031.json)
cover probe failures, concurrent enqueue, supply ownership, controls and provider
errors against the actual Onyx properties.

The BMD provider retains the native [3,1] IIC/GPIO sensor scheme and underscore
GPIO binding, checks sensor failures and continues monitoring while connected.
[BMD tests](bmd-validation-20261031.json) cover eight probe faults, 2,014 sensor
cases and worker removal. PD authentication preserves native VDM commands and
uses validated, aligned payloads; state changes follow successful provider calls.
[PD interface tests](pdauth-validation-20261031.json) cover 105 hex boundaries,
ten invalid commands, errors and an active sysfs reader during removal.

Thermal policy validates the actual table dimensions and levels, retains desired
controls for deferred voters, propagates vote errors and drains exported setters
and workers during removal. [Thermal tests](thermal-validation-20261031.json)
cover nineteen probe faults, 61 control/provider cases and nine malformed schemas
with the pinned fifteen-row wired profile. The existing raw voter-pointer
lifetime contract remains, and multiple votes can partially apply on failure.
These tests mock kernel and provider services; they do not establish electrical
protection, live authentication, charging or phone operation.

The combined 226-module archive `artifacts/20261031T140821184889Z` passes
12,244 imported-symbol CRC checks without compiler/modpost warnings. Device-tree
and overlay checks pass with 1,084 existing vendor DTC warnings.
ROM module build coverage is 224/466 (48.1%), leaving 242 names to assess or
port, including alternative hardware. WLAN/Bluetooth, remaining audio,
camera/video, device interfaces and image/installer work remain. No flashable
6.18 ZIP or phone validation exists yet.


## QMI files, USB functions and charging path

CNSS QMI file transfers retain locked references through each response and drain
active transfers during service shutdown. They validate peer, direction, segment
index, lengths and completion metadata, preserve transport errors and advance
only after successful replies. Callback withdrawal drains readers; configuration
records publish after successful replies and remain immutable for retained
callers. [QMI tests](wlan-qmi-validation-20261031.json) cover transfer boundaries,
allocation errors, shutdown and concurrent callback/file withdrawal. Native QMI
service schemas and exports are preserved. Main WLAN remains to be ported.

GSI and QDSS retain the native USB descriptors and data layouts. GSI bounds its
event queue, rolls back partial channel setup and retires callback tokens before
provider teardown. QDSS drains requests before releasing clients; open files and
clients retain removed instances. [USB tests](gsi-qdss-validation-20261031.json)
cover malformed messages, endpoint failures, removal, provider withdrawal and
late callbacks. The native IPA transport provider still needs completion.

The audio buffer driver tracks DMA-buffer identity, shared mappings and each
client's ownership. Failed secure release retains the mapping and provider
references for retry. [Full-source tests](audio-ion-validation-20261031.json)
pass fifteen ASan/UBSan groups; fourteen original-source controls reject their
targeted faults. Real SCM transactions, IOMMU and DMA behavior remain untested.

Charging path control validates the native DT schemas, distinguishes source
ordinals from condition bitmasks and acts only on configured roles. Initial
provider snapshots and failed changes preserve errors and allow retry. Stop
drains exported setters, sysfs and workers. [Path tests](pathcontrol-validation-20261031.json)
cover 192 probe failures, 27 invalid schemas and 31 provider/bitmask cases using
the actual Onyx properties. The central integration keeps the existing API
header and omits a comment-only handoff change. Live electrical and gate behavior
remain untested. Three charger-business event types still lack native strategy
handlers, as recorded in module coverage.

The combined 231-module archive `artifacts/20261031T142154214564Z` passes
12,551 imported-symbol CRC checks without compiler/modpost warnings. Device-tree
and overlay checks pass with 1,084 existing vendor DTC warnings.
ROM module build coverage is 229/466 (49.1%), leaving 237 names to assess or
port, including alternative hardware. WLAN/Bluetooth, remaining audio,
camera/video, device interfaces and image/installer work remain. No flashable
6.18 ZIP or phone validation exists yet.


## WLAN mailbox, audio packets and haptics

The native WLAN utility, firmware-schema and SMEM mailbox modules preserve the
modem descriptor/TLV layouts and generated QMI schemas. SMEM validates client
ranges, ring pointers and complete frames, reserves space to distinguish a full
ring, initializes padding and drains IRQs and callbacks before removal. Changed
descriptor layouts return ESTALE and require driver rebind. Urgent signal errors
can occur after a payload is committed. Stop must run outside urgent callbacks.
WLAN utility callback withdrawal drains readers; MAC data publishes once. The
utility retains its module because native getters return raw MAC storage.
[Utility tests](wlan-utils-validation-20261031.json) cover 2,048 MAC publications,
8,075 parser/copy boundaries, 3,104 ring/wire cases, probe faults and callback
withdrawal. Three mutation controls reject unchecked descriptors, missing ring
reservation and omitted callback drain. Live modem protocol and MMIO remain
untested; the main WLAN driver still needs completion.

Audio packets validate native GPR headers and message lengths, retain per-open
regions and queues, and serialize send/read/withdrawal. Open files retain removed
instances and old sessions stay disconnected after rebind. [Full-source tests](audio-pkt-validation-20261031.json)
pass twenty-five ASan/UBSan groups; sixteen original-source controls and one
cleanup-lock mutation reject their targeted faults. Tests use the integrated GPR
headers and mock VFS, ION and DSP services. Live DMA and DSP behavior remain untested.

The Awinic composite retains both AW8697 and AW86927 backends and native miev
integration. Owned delayed work loads RAM firmware; atomic timer callbacks queue
sleeping I2C operations. Stop drains work and disconnects retained misc files.
Firmware/I2C bounds and actual transfers are checked; the ring reports bytes
copied and cancels blocked writers on reset/removal. [Haptic tests](haptic-validation-20261031.json)
cover thirty-four probe faults, firmware waits, 492 malformed RAM cases and 4,410
I2C cases. [Ring tests](haptic-ring-validation-20261031.json) cover 300 wraps and
65,536 concurrent bytes. Six compiled negative controls reject their faults.
Physical timing, firmware compatibility and kernel IRQ behavior remain untested.

The combined 236-module archive `artifacts/20261031T144453214130Z` passes
12,796 imported-symbol CRC checks without compiler/modpost warnings. Device-tree
and overlay checks pass with 1,084 existing vendor DTC warnings.
ROM module build coverage is 234/466 (50.2%), leaving 232 names to assess or
port, including alternative hardware. Main WLAN/Bluetooth, remaining audio,
camera/video, device interfaces and image/installer work remain. No flashable
6.18 ZIP or phone validation exists yet.


## Wireless framework and audio resource manager

The cfg80211 framework uses thirty exact inputs from the pinned 6.18 common
revision, matching its public kernel headers and all nineteen enabled native
composite units. It carries forward Konoha's existing monitor-channel dispatch
policy, including the new device argument and driver error propagation. All other
wireless source remains unchanged. Signed regulatory database verification stays
enabled; both shipped signing certificates match the generated source and the
compiled module. [Framework checks](wireless-framework-validation-20261031.json)
cover one hundred dispatch cases and reject the original policy as a regression
control. Main CNSS2/WLAN driver adaptation and live monitor operation remain.

The native audio resource manager retains its five public APIs and twelve native
request/release frame layouts. Correlation tokens and service epochs reject stale
DSP replies; readiness failures never become cached. Provider withdrawal wakes
commands before draining them. [PRM checks](audio-prm-validation-20261031.json)
pass fourteen full-source sanitizer groups; ten original-source controls and a
token-reset mutation reject their targeted faults. The public API header remains
byte-identical. DSP firmware, clock hardware and suspend/resume remain untested.

The combined 238-module archive `artifacts/20261031T150648247040Z` passes
13,024 imported-symbol CRC checks without compiler/modpost warnings. Device-tree
and overlay checks pass with 1,084 existing vendor DTC warnings.
ROM module build coverage is 236/466 (50.6%), leaving 230 names to
assess or port, including alternative hardware. Main WLAN/Bluetooth, remaining
audio, camera/video, device interfaces and image/installer work remain. No
flashable 6.18 ZIP or phone validation exists yet.
