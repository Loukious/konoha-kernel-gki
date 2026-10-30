# Android 17 / Linux 6.18 feasibility for POCO F7

Study date: 2026-10-31. Device: POCO F7 / onyx, Qualcomm SM8735.

**A source-based 6.18 device port is plausible, and there are useful public
sources. Replacing only the current kernel while retaining the installed
6.6 modules is not a viable migration plan.** The next useful milestone is
a reproducible build of the device's existing driver stack, followed by a
6.18 build of its boot-critical components. No 6.18 kernel or driver build,
boot test, or device modification was performed for this study.

## Reference points

| Input | Revision / observation |
| --- | --- |
| Validated Konoha kernel | `d6b6ee8c989834415729c7ca4518a91bf9cd1f7b`, Linux 6.6.144 |
| Current device baseline | Android 17; 542 loaded modules after the KernelSU manager update and reboot |
| Requested upstream branch | [`android17-6.18`](https://android.googlesource.com/kernel/common/+/refs/heads/android17-6.18) |
| Examined upstream snapshot | [`5a54d2ea7de67188274294d346e44a306e36cd03`](https://android.googlesource.com/kernel/common/+/5a54d2ea7de67188274294d346e44a306e36cd03), Linux **6.18.33** |
| Target build constants | KMI generation 5; Clang `r584948c`; Rust `1.91.1.p3` |
| Existing build constants | Clang `r547379`; 4 KiB pages; HZ 300; custom NetHunter configuration |

The phone already runs Android 17 on 6.6.144. Updating Android userspace and
changing the kernel LTS series are separate operations.

## Why the existing modules cannot be the compatibility plan

Android's [stable-KMI policy](https://source.android.com/docs/core/architecture/kernel/stable-kmi)
limits the stability guarantee to the same Android kernel branch and LTS
version, using its specified configuration and toolchain. It does not
promise binary compatibility between `android15-6.6` and `android17-6.18`.
The [GKI architecture documentation](https://source.android.com/docs/core/architecture/kernel/generic-kernel-image)
also distinguishes the common kernel from the SoC and board drivers.

The current custom loader ignores symbol-version mismatches. That explains
why the existing 6.6 modules can load with this customized 6.6 kernel; it
does not fix missing symbols, changed callback arguments, structure
layouts, or driver behavior after a move to 6.18.

This is observable in the actual device dependencies, rather than just a
general warning about major upgrades. The post-reboot log identifies 90
Android hook names imported by currently loaded modules through their
symbol-version messages. Comparing their declarations with all 68 target
hook headers finds:

- **Seven hook names absent** from the target hook declarations and its
  `drivers/android/vendor_hooks.c` exports.
- **Five hooks with changed argument counts**, plus two harmless
  argument-name-only differences excluded from that count.

Examples affecting active device components:

| Current module | Interface | Change in the examined 6.18 tree |
| --- | --- | --- |
| `sched_walt` | `android_rvh_check_preempt_wakeup` | Hook absent |
| `sched_walt` | `android_rvh_find_lowest_rq` | One task pointer becomes separate scheduling/execution task pointers; 4 arguments become 5 |
| `sched_walt` | `android_rvh_replace_next_task_fair` | 6 arguments become 3 |
| `sched_walt` | `android_vh_binder_restore_priority` | 2 arguments become 1 |
| `qcom_iommu_util` | `android_rvh_iommu_setup_dma_ops` | Device/base/limit arguments become device only |
| `msm_kgsl` | `android_rvh_shmem_get_folio` | Adds an `order` argument |
| `qti_thermal_vendor_hooks` | `android_vh_disable_thermal_cooling_stats` | Hook absent |
| `minidump` | `android_vh_ftrace_size_check` | Hook absent |

The other four absent hooks belong to `gzvm`; not every loaded module is
necessary for normal operation. The scheduler, DMA/IOMMU, graphics, and
thermal changes still require a deliberate port. Reintroducing names or
dummy hooks without their original semantics would not solve that.

The current Qualcomm KMI list has 2,526 names; the target's Qualcomm list
has 4,042, with 2,208 in common. Thus 318 old names are absent from that
particular new list. This is a **symbol-list comparison**, not proof that
all 318 are absent from the complete kernel exports, and not a CRC or
layout comparison. Shared names also do not imply compatible types.

The machine-readable [hook audit](android17-6.18-hook-audit.json) records
the source locations, prototypes, and affected modules. Its dependency
sample comes from mismatch messages, so it is not an exhaustive audit of
every module import or private structure access.

## Available device sources

The following public inputs were inspected:

| Source | Examined revision | Use / limitation |
| --- | --- | --- |
| [MiCode Onyx kernel](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/tree/58e48d87fdc02f1e2978547f75ea524962217cc5) | `onyx-v-oss`, `58e48d87fdc02f1e2978547f75ea524962217cc5` | Xiaomi release; Makefile reports 6.6.56 |
| [Onyx maintained kernel](https://github.com/xiaomi-onyx-dev/android_kernel_xiaomi_sm8735/tree/09a5a2714470988d6a84f4b3677754af1479b037) | `lineage-23.2`, `09a5a2714470988d6a84f4b3677754af1479b037` | Device core and Qualcomm additions; 6.6.142 |
| [Onyx modules](https://github.com/xiaomi-onyx-dev/android_kernel_xiaomi_sm8735-modules/tree/556221b539c13b830ff2b8b1600b3dd54be3c9d8) | `556221b539c13b830ff2b8b1600b3dd54be3c9d8` | Audio, Bluetooth, camera, data/IPA, display, DSP, fingerprint, KGSL, secure services, touch, video, WLAN |
| [Onyx device trees](https://github.com/xiaomi-onyx-dev/android_kernel_xiaomi_sm8735-devicetrees/tree/f4fbbed3359e85ffee608a0ff4359fa462735614) | `f4fbbed3359e85ffee608a0ff4359fa462735614` | Includes `onyx-sm8735-overlay.dts`, Onyx charger/fingerprint files, and Tuna platform definitions |
| [PixelOS SM8735 kernel](https://github.com/PixelOS-Devices/android_kernel_xiaomi_sm8735/tree/d0881fd7905872a11cd70bc46b37849135aa9a71) | `seventeen`, `d0881fd7905872a11cd70bc46b37849135aa9a71` | Also 6.6.142; the Android-version branch name does not imply a 6.18 kernel |

These trees make a source port credible. Their exact correspondence to the
phone's current vendor modules and proprietary HALs still needs to be
established. Availability of a camera or GPU directory is not proof that
it contains the precise driver revision and board configuration required
by this ROM.

The target common tree has no Onyx/SM8735/Tuna-named device tree in the
inspected `arch/arm64/boot/dts/qcom` directory. The generic branch therefore
does not by itself supply a complete Onyx platform. Existing firmware and
Android vendor HALs may remain usable if the port preserves their protocols
and UAPI, but each hardware stack must demonstrate that compatibility.

## Useful Qualcomm 6.18 work already exists

[`DiamaneOS/kernel_qcom-6.18`](https://github.com/DiamaneOS/kernel_qcom-6.18/tree/fa6b625ce3a93572875609590bd1d757c7740dc6)
contains a Fairphone 6 port with Qualcomm driver groups and explicit 6.18
adaptations. It is a useful reference, not an Onyx replacement. Examples
inspected in its history:

- [KGSL worker startup](https://github.com/DiamaneOS/kernel_qcom-6.18/commit/9bb8e6e8528e2247ec3be1899872b1035158a8bc)
  changes `kthread_create_worker` to `kthread_run_worker` to account for
  changed startup behavior. This illustrates a runtime issue that merely
  getting code to compile would miss.
- [IOMMU fault-handler adaptation](https://github.com/DiamaneOS/kernel_qcom-6.18/commit/cfbba341bf0c156b4e3788d2f0dbd5d45bd7607b)
  changes the SMMU support and camera/video/display/WLAN callers for the
  newer DMA-domain fault-handler behavior.
- [qcacld port](https://github.com/DiamaneOS/kernel_qcom-6.18/commit/d01bc61cb46d3ca3484746254f88408b0cbd4782)
  adapts timer/network/cfg80211 integration and adds build rules.

These changes need comparison with SM8735 and WCN7750 sources before reuse.
This study did not validate that project's complete hardware support. Its
linked companion build documentation still describes a 6.1 workspace, so
that document cannot establish a working 6.18 release.

## KernelSU, NetHunter, and build changes

KernelSU is comparatively favorable: the revision already used here,
[`7b5fdb7a51600e96fcb4d148f86c4633ddceee63`](https://github.com/KernelSU-Next/KernelSU-Next/tree/7b5fdb7a51600e96fcb4d148f86c4633ddceee63),
explicitly lists `android17-6.18` in its kernel build scripts, handles the
new external-module `M`/`MO` build form, and has 6.18 SELinux adaptations.
This supports attempting integration; it is not a completed local 6.18
build test. Keep kernel and userspace UAPI versions matched.

NetHunter requires separating configuration from device-driver work:

- Rebuild standard USB/network/filesystem modules with the new kernel.
- Port the WCN7750 vendor driver and its injection changes against the
  new cfg80211 interfaces; verify association before monitor/injection.
- Rebuild the external Realtek modules. Their current successful loading
  on 6.6.144 says nothing about 6.18 compatibility.
- Recheck USB gadget ownership. The current script explicitly leaves
  RNDIS to the vendor `usb_f_gsi` driver, which also needs a compatible port.

Use the [common-android17-6.18 manifest](https://android.googlesource.com/kernel/manifest/+/refs/heads/common-android17-6.18/default.xml)
and its Kleaf toolchain as the starting build environment. The examined
[`bazel/constants.scl`](https://android.googlesource.com/kernel/common/+/5a54d2ea7de67188274294d346e44a306e36cd03/bazel/constants.scl)
pins the compiler versions above. The old `build.config.common` and
`build.config.gki.aarch64` now exit with migration instructions; they cannot
be reused as working legacy build configurations.

The target GKI configuration enables Rust, `CONFIG_GENDWARFKSYMS`, and new
scheduler facilities. Its ABI data lives under `gki/aarch64`, rather than
the old `android/abi_gki_aarch64*` layout. Adapt configuration and ABI
tooling deliberately. Start with 4 KiB pages to match the current device;
a page-size migration adds separate compatibility work.

## Proposed implementation and decision gates

1. **Reproduce the device stack on 6.6.** Pin the current ROM's device
   kernel, module, DT, and firmware inputs; map early-boot and normal module
   load lists to source/build targets. Build the required Qualcomm/Xiaomi
   modules and compare their UAPI, dependencies, and layouts with the
   installed stack. Resolve missing source inputs here.
2. **Establish an unmodified 6.18 build.** Build the pinned common tree
   with its own toolchain/configuration and archive its Image, modules,
   configuration, and ABI output. A virtual-device boot can validate the
   generic build, but provides no evidence for Onyx hardware support.
3. **Port the boot-critical platform.** Clocks, regulators/PMIC, pinctrl,
   interrupts, interconnect, IOMMU, UFS/inline crypto, SCM/SMEM, USB, and
   required remote processors come first. Port WALT or deliberately adapt
   the device's performance interfaces to the new scheduler. Check real
   callback signatures and layouts, not just successful symbol resolution.
4. **Produce a matched boot set.** Rebuild the needed system/vendor DLKM
   modules and early vendor-ramdisk modules with the same kernel/toolchain.
   Reconcile DTB/DTBO bindings and bootloader selection metadata. Determine
   the actual image changes required before producing an installer.
5. **Validate the phone progressively.** Reach early userspace and ADB,
   then validate storage/encryption, display/GPU/touch, radios, charging and
   thermal control, audio/camera/biometrics, suspend/resume, and sustained
   workloads. Preserve firmware/HAL interfaces and investigate warnings
   against the working 6.6 baseline.
6. **Reintroduce custom features.** Add KernelSU and NetHunter after the
   device baseline works. Carry intentional Konoha behavior; do not replay
   old LTS/backport fixes already superseded in 6.18. Package a recovery
   path together with the final matched artifacts.

The current AnyKernel3 ZIP replaces the kernel only. A 6.18 device port
needs a matched driver/boot-image plan and may require DT changes; it
cannot assume that keeping the installed `vendor_dlkm`, `system_dlkm`, and
early-boot modules is sufficient.

**Recommendation:** proceed as a separate experimental source port while
keeping the validated 6.6.144 branch as the working device baseline. Source
availability and the Qualcomm reference port justify investigation. Exact
Onyx driver/HAL compatibility and early-boot integration are the principal
unknowns, and no reliable completion estimate follows from this static
study alone.

## Study artifacts and limits

Downloaded source snapshots, dependency comparisons, repository metadata,
and public documentation excerpts are saved locally in
`/home/loukious/Android/konoha-android17-6.18-study-20261031`.
The hook audit covers all target hook headers; the Qualcomm symbol-list
comparison uses the complete Qualcomm lists only. Other partner-list
downloads were incomplete due to rate limiting and were not treated as a
complete export inventory. No firmware extraction, driver decompilation,
full 6.18 build, or hardware test was needed to establish the specific
source-interface incompatibilities documented here.
