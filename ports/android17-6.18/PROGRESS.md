# Onyx 6.18 checkpoint — 2026-11-02

The experimental Onyx images boot Android on slot A with
`6.18.33-android17-5-maybe-dirty-4k`. Both normal boot and return from userspace
fastbootd completed, with 120 seconds of stable Android observations after each.
The flashed `vendor_boot` readback matches the verified candidate. No partition
resize or userdata erase was required.

The latest kernel/module archive is `20261102T044143670048Z`: 318 platform
modules, imported-symbol CRC checks and both board device-tree checks pass.
It changes only `ipam.ko` from the preceding working archive
`20261102T042308959020Z`; GKI and device-tree bytes are identical.
The matched image package is `experimental-images-20261102-ipa-sram-wlan-swr-eva-tx-serial`.
Build archives and device backups remain in the separate local build workspace.

## Latest repairs and observations

- The SCMI SRAM overlay preserves the firmware's 1 KiB window at
  `0x17d09400..0x17d097ff`. The corrected SRAM platform driver binds on hardware.
- Wi-Fi associates and all three gateway pings succeed. The false optional
  data-stall configuration failure message is absent from the captured boot.
- SoundWire bulk transfers acquire their I/O mutex once and propagate backend
  errors. Source concurrency and error-cleanup tests pass; the separate
  `CMD Ignored` hardware interrupt remains unresolved.
- EVA debugfs trees use allocated IDs instead of restricted pointer strings.
  Multiple camera/EVA instances open without the old duplicate directory warning.
- Shared WAN RX NAPI remains owned by the netdev until final removal. Hardware
  testing exposed an additional WAN producer TX NAPI object that pipe teardown
  skipped. Patch 0173 releases that separately owned object. Sixty-four production
  teardown cases pass; the original and three cleanup regressions fail. The
  subsequent modem `AFTER_SHUTDOWN` capture completes without the enabled-NAPI
  deletion warning, and fastbootd and Android return succeed.

The earlier SMMU, CoreSight duplicate-funnel, GPI pointer, WALT delayed migration,
audio IOMMU, WLAN page-pool, modem shared-memory, display, recovery, battery,
fingerprint, camera, microphone and haptic work is preserved in patches 0124–0173,
the import recipe and the accompanying validation directories.

## Limits and remaining work

Proxy execution remains disabled for WALT compatibility. The boot images retain
diagnostic checkpoints and sequential module loading. Production readiness and
complete hardware parity remain unverified.
Android is not rooted; full partition readbacks and live module-note reads are
unavailable from its shell. Image hashes and flashed readback receipts provide
the installation evidence.

SoundWire ignored-command diagnostics, charging-mode selection when powered off,
the bootloader black interval and other entries in `kernel-warning-ledger.json`
remain under investigation. Camera image quality, audible output, firmware/SMP
stress and all possible modem restart paths have not been exhaustively tested.

ROM changes are carried only in `Loukious/crdroid_onyx_patches`:
`packages_modules_adb/0001-fix-functionfs-os-descriptor-bcd-version.patch` and
`device_xiaomi_onyx/0016-enable-device-as-webcam.patch`. Their generated artifacts,
patch application, repeated application and repository preflight pass. The
corrected ADB descriptors and webcam flag require a rebuilt ROM; neither is
deployed by these kernel-only images.
