# First experimental boot-test ZIP

## Runtime result — 2026-11-01: BOOT FAILED, rollback verified

The user authorized backup and installation through Windows adb.exe/fastboot.exe.
The original package required a system_dlkm resize that fastbootd refused before
any image write. A device-fit variant used compact EROFS with unchanged file
contents, exact existing DLKM partition sizes, and repeated offline validation.
All five device-fit images flashed successfully to active slot A; vendor_boot
readback matched. The user reported a white flash after the POCO logo followed
by reboot. Android/ADB never appeared on 6.18. Offline checks did not establish
runtime bootability; neither package is a successful boot-tested release.

All five modified partitions were restored from verified preflash backups.
The phone now boots 6.6.144-Kono-Ha-Kernel, sys.boot_completed=1, with root.
Device SHA-256 checks of all nine backed-up active-slot partitions match the
original backups, including unchanged init_boot/recovery/vbmeta partitions.
No userdata erase was performed.

Pstore was empty after rollback; /proc/last_kmsg and recovery last_kmsg were
unavailable. Previous-boot kernel logcat returned no saved log. The captured
current dmesg belongs to restored 6.6 and cannot identify the 6.18 failure.
The cause remains unknown. Next engineering step is to inspect early-boot
logging/persistence and prepare a diagnostic build before another boot test.
Do not repeat the same failing flash or infer the cause from the white flash.

Backup and verification evidence:
`/home/loukious/Android/konoha-android17-6.18-port-20261031/device-backups/5493f3c6-20261101T160034Z`
See rollback-618.json and post-rollback/. ZIPs remain unchanged as test evidence.

The user selected **Experimental boot-test ZIP first** on 2026-11-01. The first
deliverable is a device-specific, matched 6.18 boot-test package. Full hardware
and optional-driver parity follow the boot test. The existing port checks and
source pins remain in force; missing optional modules must be listed explicitly.

## Package contents

- Boot image with the verified 6.18 Image and required
  `android_arch_task_struct_size=512` allocation parameter.
- Vendor boot image preserving the baseline non-module ramdisk contents,
  bootconfig, board layout and compression, with matching module payloads and
  verified Onyx DTBs.
- Matched DTBO, system DLKM and vendor DLKM images, preserving filesystem and
  AVB layout requirements.
- A recovery installer using the existing AnyKernel3 tools, restricted to Onyx,
  with all payload/hash/size checks completed before the first partition write.
- A manifest recording source/build/image hashes, baseline ROM inputs, omitted
  modules, feature limits, expected partitions, and offline validation results.

## Required offline gates

1. Verify the final kernel/module build, release, imported CRCs, source pins,
   transport selections and both final Onyx device-tree variants.
2. Generate matching module trees and dependency metadata. First-stage omission
   decisions retain their device-tree/consumer/init evidence. Preserve applicable
   native blocklists and options. Recovery and later-stage omissions are recorded
   as experimental feature gaps; no 6.6 modules may enter a 6.18 image.
3. Preserve hard dependencies, generate load order and run depmod plus modprobe
   dry runs. Explicitly review missing soft dependencies; the verified first-stage
   closure must remain complete.
4. Build images with pinned baseline parameters and local Android host tools.
   Unpack/check each generated image: module payloads, DTBs/DTBO, command line,
   release, AVB descriptors, filesystem contents and partition size limits.
5. Build the ZIP and verify its contents/hashes. Exercise installer preflight and
   partition-selection logic using mocked recovery/block devices; do not flash a
   connected phone as part of package creation.

KernelSU/NetHunter completion and full optional-driver parity are deferred from
this first boot-test package. Features actually present in the verified build
remain documented individually. A successful offline package audit does not
claim phone boot, working hardware, or a stable release.
