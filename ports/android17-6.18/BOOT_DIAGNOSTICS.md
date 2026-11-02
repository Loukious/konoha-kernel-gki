# Onyx 6.18 boot failure and persistent logs

The failed 20261101T150526062786Z boot is now identified by a recovered Qualcomm
rawdump console, its 6.18 kernel banner, and matching GKI build ID
`c71015b1e0fbc19fb46b7b584d619f7c1c1cf92c`. First-stage init times out waiting for
storage partitions at 10.461 seconds, exits 127, and triggers
`Kernel panic - not syncing: Attempted to kill init! exitcode=0x00007f00`.
Repeated Apps/GPU SMMU registration failures precede that timeout. See
`validation/rawdump/device-validation.json` for provenance and selected lines.

The source defect is in `arm_smmu_get_by_fwnode`: its driver-bound lookup cannot
find the SMMU while the SMMU's own probe is registering the IOMMU. Linux 6.18's
IOMMU registration probes client devices synchronously. Patch 0137 uses the
platform bus lookup, reads driver data before releasing the reference, clears
failed-probe publication, and logs the registration errno. Five host scenarios
and three compiled regression controls pass. Archive 20261101T170423790381Z
passes the strict 314-module build, 20,074 import CRC checks, and DT/overlay
checks. Only `arm_smmu.ko` changed; all GKI files match the failed candidate.
This fixes a demonstrated source defect. The corrected hardware test now confirms successful SMMU probes, UFS
enumeration, metadata mount and init second stage at 0.678251 seconds.
Full Android boot still fails later; see the new runtime result below. Other warnings in the failed console
are retained as evidence, not assigned causal significance yet.

## Why the initial collection missed the logs

Both kernels enable ramoops. Both DTs reserve the same 4 MiB at 0xa3500000,
split between 2 MiB console and 2 MiB userspace pmsg. No dedicated panic record
space remains. `logcat -L` reads prior userspace pmsg, not the kernel console.
The OEM `oops` partition contained old reports; 6.18 disables MTD and does not
package its block2mtd/mtdoops logger.

Live and early-archived pstore were empty after a normal 6.6 reboot and after
the approved controlled 6.6 panic. That panic temporarily disabled download
mode to avoid EDL. Those tests cannot establish retention for every reset
reason. Intervening recovery boots could also replace a RAM console. The
precise firmware clearing/consumption mechanism is unknown.

The initial collection omitted `/dev/block/by-name/rawdump`. Qualcomm firmware
had copied the failed 6.18 console there. That disk dump survived rollback and
later 6.6 boots; the controlled panic did not replace it. Two independent log
sections contain the same 29,052 console bytes. Do not interpret the most
recent host capture time as the crash time.

## Capture the next failure

Preserve a host copy before the next boot attempt. On the first available
rooted Android or recovery ADB connection, run the collector from this folder:

```sh
python3 collect_boot_logs.py /absolute/new/private-evidence-directory
```

Use `--recovery` when the shell is already root and `su` is unavailable. The
collector first saves pstore, then reads only bounded kernel-log sections from
rawdump. It validates packed headers, section bounds, chronological console
rings, hashes, and an unchanged header snapshot after capture. It does not
clear logs or write dump partitions. It rejects unsupported formats. Archive
validation distinguishes empty pstore from missing optional archive paths.

The existing early Android hook saves pstore under
`/data/adb/onyx-boot-logs/<boot-id>`. The installed service captures at most
16 MiB for 120 seconds and syncs each second; its marked disk output survived
the controlled reboot. These KernelSU hooks run on restored 6.6. KernelSU is
not integrated in this 6.18 candidate, so these hooks do not provide early
6.18 capture. Storage/userland availability is also required for disk logging.

An offline-audited rescue recovery is available at
`packaging/recovery-logging-20261101-v2/recovery.img` in the build workspace.
Its init hook copies pstore to `/metadata/ota/onyx-boot-logs` before userdata
decryption. It is not flashed or hardware validated and uses the current boot
partition's kernel. Missing source records cannot be recreated by archiving.
Collect from the first rescue connection before any further reboot.

The corrected diagnostic images add `ignore_loglevel initcall_debug` while
preserving `android_arch_task_struct_size=512`, the matched module closure,
bootconfig, and exact device partition sizes. Qualcomm minidump/download-mode
modules remain in the verified first-stage load list. Leave their original
settings intact; changing download mode changes the crash evidence path.
Another dump-producing crash may replace rawdump, so host collection is the
durable archive. No additional controlled panic is authorized by the earlier
single-panic approval.

The phone was last verified on restored 6.6 with Android boot complete. Backups
and private evidence are in the build workspace under
`device-backups/5493f3c6-20261101T160034Z`. A diagnostic package passing offline
checks does not establish successful boot or hardware operation.

## Corrected hardware attempt and next isolation

The SMMU diagnostic candidate flashed successfully, with matching vendor_boot
readback. The phone still showed a white flash and reboot. After restoring
the five original images, Android boots on 6.6.144 and their device hashes
match the backups. New evidence is stored privately under
`logging-investigation/after-smmu-diagnostic` in the backup directory.

The new 498,659-byte console contains the diagnostic command line. GPU and
Apps SMMU probes return 0; UFS enumerates its SKhynix disks and partitions;
metadata mounts; SELinux loads; and init second stage starts at 0.678251 s.
No old registration failures or first-stage partition timeout occur.

The console ends at 1.384110 s without a panic/oops message. Panic context,
runqueue and task-stack sequence buffers are zero, and watchdog in_panic is
zero. Eight saved CPU context prefixes contain PCs but zero general registers
and stack fields, preventing a kernel backtrace. The last console entry is
not a precise reset timestamp. The selected state/table captures exclude
firmware/key sections and are bound to the unchanged rawdump header.

Exactly two module initcalls are unfinished at the captured end:
`coresight_funnel` and `qcom_spmi_adc5_gen3`. Blocking both temporarily through
a recorded diagnostic `module_blacklist` will test whether execution moves
past that point. This is an isolation experiment, not proof either module
is faulty. The built module files and coverage remain unchanged.

## Volume Down-only capture repeat — 2026-11-01

The user authorized repeating the module-isolation build to capture at an automatic reboot using Volume Down without Power. FASTBOOT appeared and the host watcher restored all five original images, rebooted 6.6.144, collected evidence on the first rescue Android connection, and verified all restored hashes and completed Android boot.

The new stable-header rawdump contains 140 sections, including a 566,404-byte / 6,820-line kernel console with the diagnostic command line and both blacklist confirmations. Init second stage starts at 0.722373 s; the last entry is at 1.584209 s without a recorded panic/oops. The SPMI transaction warning and device-link warning during qti_devfreq_cdev probe recur from the previous SMMU diagnostic. i2c_msm_geni and ipam initcalls are unfinished at the console end; neither is proven causal. The two-module blacklist did not yield a successful boot.

A successful Volume Down-only entry is now observed for this device, but this does not prove the previous forced reset caused missing logs or identify the exact reset represented in the dump. Evidence: `/home/loukious/Android/konoha-android17-6.18-port-20261031/device-backups/5493f3c6-20261101T160034Z/logging-investigation/volume-down-capture-20261101T175009Z`. The phone remains on restored 6.6.
