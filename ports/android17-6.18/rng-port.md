Native qcom-rng and qrng_dlkm port from eight exact pinned inputs, retaining both
single translation units selected by the ROM. Core drivers/crypto/qcom-rng.c,
Kconfig and Makefile use revision 10570812a4878b644e31b47c2f0bd052664cf86d.
Vendor securemsm-kernel/qrng/msm_rng.c, Kbuild, Makefile and the two selected
configuration .conf files use revision 8500748dae32f10507c02f1c94bc971553cb525b.
The existing secure_headers provider supplies byte-identical qrng.h and the native
configuration-header evidence. No additional secure source provider is invented.

Normal strict isolated build3 passes, with zero compiler/modpost diagnostics. Both
native dependency lists are empty and the 86 imported GKI CRCs match the recorded
archive. Both modules export no symbols and remain unloadable. qcom-rng keeps its
platform:qcom_rng, ACPI QCOM8160 and qcom,prng / prng-ee / trng aliases. qrng_dlkm
keeps the native empty alias list, including its native lack of MODULE_DEVICE_TABLE.
The selected DDK config uses existing GKI HW_RANDOM and CRYPTO_RNG plus module
CRYPTO_DEV_QCOM_RNG and HW_RANDOM_MSM_LEGACY. Checkpatch has zero errors/warnings/checks;
exact git/patch replay matches all final hashes with zero fuzz and zero offsets.

qcom-rng retains stdrng / qcom-rng, priority 300, the native no-op seed function,
register masks/initialization sequence, partial-tail copy behavior, polling timing,
and rejection of zero data words. Its qcom_hwrng provider retains native quality 1024
for TRNG only. Crypto algorithms now belong to their controller and retain that
allocation until the final transform releases it; cra_destroy handles algorithm
unregistration and both early/late registration failures without double release.
Existing transforms remain bound to their original controller across rebind. Removal
drains operations and sets a stop gate before devres unmaps MMIO or releases clocks;
stale transforms return -ENODEV. This also removes the global-controller overwrite
hazard. Crypto and hwrng reads serialize clock enable, MMIO and clock disable under
one mutex. Zero-length reads touch no hardware; crypto sizes above INT_MAX return
-EOVERFLOW. Nonwaiting reads do not poll for unavailable data. The ACPI match now
uses the real prng-ee descriptor through device_get_match_data; platform remove uses
the 6.18 void signature. No upstream/common RNG code was edited.

qrng_dlkm retains qrng / fips_hw_qrng, priority 300, the no-op seed, native quality
1024, register masks/barriers, native cumulative five-delay retry budget and complete
32-bit-word reads. Copies support unaligned caller buffers. Crypto still requires an
exact result and returns -EFAULT for short/non-word or suspended reads. Zero data
words remain valid here as in the native legacy driver. Clock and ICC failures now
propagate rather than being swallowed; successful high votes are followed by a low
vote even on clock failure. Requests shorter than four bytes perform no I/O, and
nonwaiting reads stop immediately when data is unavailable. Both reads and ioctl
hold the same I/O mutex; suspend drains reads and blocks new I/O. The hwrng and ioctl
paths report -EAGAIN during suspend; removed controllers report -ENODEV.

The legacy /dev/msm-rng node keeps major 256, minor 0 and the unchanged reset-bandwidth
ioctl number; unknown live-device ioctls retain native success. An embedded cdev/device
pair supplies actual device-core lifetime management, replacing the unused native
cdev_init/cdev_del mismatch. Files have module ownership and retain the original
device; a stale inode cannot attach to a replacement controller, and stale file ioctls
cannot use freed ICC resources. A static registry mutex serializes publication,
crypto generation, removal and duplicate probes. Ordinary probe failures release
independent resources and preserve deferred-probe errors. Failed module crypto
registration unregisters the platform driver. File opens remain gated until the final
module registration succeeds, preventing live files escaping a failed module init.

Two extracted-production ASan/UBSan suites pass 16,416 size/alignment cases, 200
concurrent read/removal cases (including qcom PRNG without hwrng and TRNG), twenty
probe/resource fault cases, module-registration faults, initialization-time open
rejection, polling/clock/ICC balancing, native register configuration, suspend/error
semantics, and live crypto contexts/files across removal and rebind. Thirty-one
independent regressions are rejected by assertions or sanitizers. Kernel crypto,
VFS/device-core reference accounting, clocks, ICC and register I/O are mocked;
pthread mutexes exercise serialization. These tests do not prove actual device-core,
PM, entropy quality, firmware, HAL, hardware or phone behavior.

The native Sun/Onyx tree selects qcom,trng at 0x10c3000 with no Onyx override found;
legacy qcom,msm-rng nodes were found only in other SoC trees. Therefore qrng_dlkm
preserves ROM module coverage but no legacy RNG hardware binding is claimed on Onyx.

Integration inputs: frozen-source/, patch, imports.json, modules.json, BUILD.fragment,
Kconfig.fragment and defconfig.fragment. The BUILD fragment reuses //onyx:secure_headers;
convert that label to :secure_headers for central integration. Preserve tests/ and
verify_rng.py, which accepts --source-dir onyx for central verification. All sources
were staged privately under onyx_rng, never onyx-dev or onyx-imports. Stable/common,
baselines, central files and prior frozen handoffs remain untouched. No ZIP or flashing.
