# Onyx Android 17 / Linux 6.18 port

This directory contains the first implementation of the POCO F7 / SM8735 port.
The pinned **6.18.33 GKI and 48 vendor modules build successfully**. This is
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
all 45 selected project revisions without syncing. The original ROM checkout
and stable kernel checkout are not build destinations.

`build.py` checks the source pins, stages exact Git blobs, applies the patches,
and builds `//common:kernel_aarch64` plus `//onyx:platform_modules`. It keeps the
stock KMI checks and compiler warnings enabled. Outputs go to
`WORKSPACE/artifacts/TIMESTAMP/`, including:

- GKI Image variants, vmlinux, symbol tables, configuration, headers and modules.
- 48 platform `.ko` files, their symbol tables and DDK configurations.
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
used. The current [validation report](validation-20261031.json) checks 1799
imported symbol CRCs across all 48 modules.

The platform group also builds IPC logging, PDC interrupts, command DB,
CRM v2, RPMh, hardware spinlocks, SMEM, SoC information, the SPMI PMIC arbiter,
Qualcomm regmap debugfs and the PMIC MFD. `modules.json` records the expected
outputs, and `build.py` verifies this set before archiving any successful build.

The adapters retain Onyx RPMh/CRM transactions and the 5000 ms timeout. They
update IRQCHIP probing, trace-string assignment, platform removal callbacks,
the SPMI device lookup and unaligned access headers for 6.18. Private hwspinlock
and regmap headers come from the pinned 6.18 common to match core layouts.
Regmap debug writes remain disabled, and IPC minidump registration still uses
the vendor header's optional fallback until the minidump provider is ported.

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

Vendor Gunyah RM is not yet ported/enabled. SCM therefore uses the pinned
vendor header's existing HLOS fallback, which retains SCM memory assignment
and does not perform secondary-VM identity substitution. This does not
establish support for secondary VMs. The Tuna VM match and soft dependency
are preserved, but its VM IRQ provider is not built yet.

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

1. Complete the Gunyah host/VM and IOMMU dependencies needed for Onyx early boot.
   Test UFS/encryption, probe ordering, GPIO wakeups, SCM wait queues and
   shared-memory operations.
2. Port the device trees and remaining first-stage list; produce matching
   vendor_boot, DTBO and DLKM images with complete dependency/load lists.
3. Port the scheduler, KGSL/display, thermal, audio, camera, touch,
   fingerprint, WLAN and other device modules. The hook mismatches in the
   [feasibility study](../../Documentation/konoha/android17-6.18-feasibility.md)
   still need semantic fixes.
4. Integrate KernelSU/NetHunter after the device baseline works. Perform boot,
   hardware and suspend/resume validation using the resulting test package before
   declaring a stable release.

Compilation, matching CRCs and preserved tables do not validate firmware
protocols, device-tree compatibility, probe ordering or hardware behavior.
No 6.18 image has been flashed or booted on the phone.
